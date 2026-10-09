#include "heurix/heuristic_engine.hpp"
#include "heurix/platform.hpp"
#include "heurix/event_bus.hpp"
#include "heurix/api_server.hpp"
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <signal.h>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>

std::atomic<bool> g_running{true};

void sig_handler(int) { g_running = false; }

static void usage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " [options]\n"
              << "  --watch <dir>               Directory to protect (default ./canary/)\n"
              << "  --listen <addr>             Listen address (default 127.0.0.1:50051)\n"
              << "  --auto-kill <true|false>     Kill malicious processes (default false)\n"
              << "  --auto-mitigate <true|false> Enable mitigation (default true)\n"
              << "  --entropy-threshold <f>      Shannon entropy threshold (default 7.5)\n"
              << "  --burst-count <n>            Burst event count (default 15)\n"
              << "  --burst-window-ms <n>        Burst window (default 2000)\n"
              << "  --enable-ml <true|false>     Enable ML classifier (default auto)\n"
              << "  --ml-model <path>            Path to HXRF1 model file\n"
              << "  --ml-threshold <f>           ML classification threshold (default 0.85)\n"
              << "  --log-features <path>        Log feature vectors to CSV for training\n"
              << "  --log-label <label>          Label for logged features (benign|malicious)\n";
}

int main(int argc, char** argv) {
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    EngineConfig config;
    config.watch_dir = "./canary/";
    std::string listen_addr = "127.0.0.1:50051";

    try {
        for (int i = 1; i < argc; i += 2) {
            std::string arg = argv[i];
            if (arg == "-h" || arg == "--help") { usage(argv[0]); return 0; }
            if (i + 1 >= argc) { usage(argv[0]); return 2; }
            std::string val = argv[i + 1];
            if (arg == "--watch")                  config.watch_dir = val;
            else if (arg == "--listen")            listen_addr = val;
            else if (arg == "--auto-kill")         config.auto_kill = (val == "true");
            else if (arg == "--auto-mitigate")     config.auto_mitigate = (val == "true" || val == "1");
            else if (arg == "--entropy-threshold") config.entropy_threshold = std::stod(val);
            else if (arg == "--burst-count")       config.burst_count = std::stoi(val);
            else if (arg == "--burst-window-ms")   config.burst_window_ms = std::stoi(val);
            else if (arg == "--enable-ml")         config.enable_ml = (val == "true" || val == "1");
            else if (arg == "--ml-model")          { config.ml_model_path = val; config.enable_ml = true; }
            else if (arg == "--ml-threshold")      config.ml_threshold = std::stod(val);
            else if (arg == "--log-features")      { config.feature_log_path = val; config.enable_feature_logging = true; }
            else if (arg == "--log-label")         config.feature_log_label = val;
            else { std::cerr << "Unknown option: " << arg << "\n"; usage(argv[0]); return 2; }
        }
    } catch (const std::exception& e) {
        std::cerr << "Invalid argument value: " << e.what() << "\n";
        return 2;
    }

    // Auto-detect ML model if not explicitly set
    if (config.ml_model_path.empty()) {
        // Search common locations
        for (const auto& candidate : {"model.hxrf1", "./model.hxrf1", "../model.hxrf1",
                                       "/usr/share/heurix/model.hxrf1"}) {
            if (std::filesystem::exists(candidate)) {
                config.ml_model_path = candidate;
                config.enable_ml = true;
                std::cerr << "[ML] Auto-detected model: " << candidate << std::endl;
                break;
            }
        }
    }

    std::error_code ec;
    std::filesystem::create_directories(config.watch_dir, ec);

    // The engine is shared by the fs-monitor callback, the canary thread and
    // API config updates, so every access goes through engine_mu.
    std::mutex engine_mu;
    HeuristicEngine engine(config);
    EngineConfig live_config = config;   // guarded by engine_mu

    auto fs_monitor = make_fs_monitor();
    auto sys_stats = make_sys_stats();
    auto proc_mgr = make_process_mgr(config.watch_dir);

    engine.deploy_canaries();

    std::cerr << "[Engine] HeuriX v0.1.0 starting\n"
              << "[Engine] Watching: " << config.watch_dir << "\n"
              << "[Engine] Entropy threshold: " << config.entropy_threshold << "\n"
              << "[Engine] Burst count: " << config.burst_count << " in " << config.burst_window_ms << "ms\n"
              << "[Engine] Auto-mitigate: " << (config.auto_mitigate ? "true" : "false") << "\n"
              << "[Engine] Auto-kill: " << (config.auto_kill ? "true" : "false") << "\n"
              << "[Engine] ML classifier: " << (engine.is_ml_active() ? "ACTIVE" : "inactive") << "\n"
              << "[Engine] Canary files deployed: " << engine.get_canary_paths().size() << "\n";

    if (config.enable_feature_logging) {
        std::cerr << "[Engine] Feature logging to: " << config.feature_log_path
                  << " (label=" << config.feature_log_label << ")\n";
    }

    // Apply mitigation policy and enrich the alert before emitting.
    // Caller must hold engine_mu (reads live_config).
    auto mitigate = [&](Alert& alert, const FsEvent& ev) {
        int pid = proc_mgr->resolve_pid(ev.path);
        if (pid > 0) {
            alert.pid = pid;
            alert.process_name = proc_mgr->process_name(pid);
        }

        if (!live_config.auto_mitigate) {
            proc_mgr->write_evidence(alert);   // detect-only mode
            return;
        }

        if (alert.severity == Severity::critical) {
            if (pid > 0) {
                alert.killed_pids = proc_mgr->children_of(pid);  // before they die
                alert.killed_pids.push_back(pid);
                proc_mgr->kill_tree(pid);
                alert.action = MitigationAction::terminated_tree;
            }
            std::string qpath;
            if (proc_mgr->quarantine_file(ev.path, qpath)) {
                alert.quarantine_path = qpath;
                if (pid <= 0) alert.action = MitigationAction::quarantined;
            }
        } else if (alert.severity == Severity::high) {
            if (pid > 0) {
                proc_mgr->suspend(pid);
                alert.action = MitigationAction::suspended;
            }
            std::string qpath;
            if (proc_mgr->quarantine_file(ev.path, qpath)) {
                alert.quarantine_path = qpath;
                if (alert.action == MitigationAction::none)
                    alert.action = MitigationAction::quarantined;
            }
        } else if (alert.severity == Severity::medium) {
            if (pid > 0) {
                proc_mgr->suspend(pid);
                alert.action = MitigationAction::suspended;
            }
        }

        proc_mgr->write_evidence(alert);
    };

    auto event_cb = [&](FsEvent ev) {
        emit_event(ev);
        std::lock_guard<std::mutex> lock(engine_mu);
        auto alert = engine.analyze(ev);
        if (alert) {
            mitigate(*alert, ev);
            emit_alert(*alert);
        }
    };

    fs_monitor->start(config.watch_dir, event_cb);

    // System stats thread — 1 Hz (UI smooths via EMA on the frontend)
    std::thread stats_thread([&]() {
        while (g_running) {
            emit_stats(sys_stats->snapshot());
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    });

    std::thread heartbeat_thread([&]() {
        while (g_running) {
            emit_status("heartbeat");
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    });

    // Canary re-verification: catches direct overwrites that bypass the fs sensor.
    std::thread canary_thread([&]() {
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            if (!g_running) break;
            std::lock_guard<std::mutex> lock(engine_mu);
            auto ca = engine.verify_canaries();
            if (ca && !engine.get_canary_paths().empty()) {
                FsEvent fake{*engine.get_canary_paths().begin(), EventType::modify,
                             (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::system_clock::now().time_since_epoch()).count()};
                mitigate(*ca, fake);
                emit_alert(*ca);
            }
        }
    });

    heurix::EngineHooks hooks;
    hooks.current_config = [&]() {
        std::lock_guard<std::mutex> lock(engine_mu);
        return live_config;
    };
    hooks.apply_config = [&](const EngineConfig& next) -> std::string {
        std::lock_guard<std::mutex> lock(engine_mu);
        bool watch_changed = (live_config.watch_dir != next.watch_dir);
        live_config = next;
        engine.update_config(next);
        
        if (watch_changed) {
            fs_monitor->stop();
            proc_mgr = make_process_mgr(next.watch_dir);
            fs_monitor->start(next.watch_dir, event_cb);
        }
        
        emit_status("config_updated");
        return {};
    };

    heurix::ApiServer api_server(listen_addr, hooks);
    const bool server_ok = api_server.Start();
    if (!server_ok) {
        std::cerr << "Fatal: could not start API server on " << listen_addr << "\n";
        g_running = false;
    } else {
        emit_status("started");
    }

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    emit_status("stopped");
    fs_monitor->stop();
    if (stats_thread.joinable()) stats_thread.join();
    if (heartbeat_thread.joinable()) heartbeat_thread.join();
    if (canary_thread.joinable()) canary_thread.join();
    api_server.Stop();
    return server_ok ? 0 : 1;
}
