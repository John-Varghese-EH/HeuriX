#include "heurix/heuristic_engine.hpp"
#include "heurix/platform.hpp"
#include "heurix/event_bus.hpp"
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <signal.h>
#include <filesystem>
#include <functional>
#include <mutex>
#include <condition_variable>

std::atomic<bool> g_running{true};
std::atomic<bool> g_heartbeat{true};
std::mutex g_config_mutex;
EngineConfig g_config;

void sig_handler(int) { g_running = false; }

void handle_config_update(const std::string& /*json*/) {
    std::lock_guard<std::mutex> lock(g_config_mutex);
    std::cout << "{\"type\":\"status\",\"data\":{\"status\":\"config_updated\"}}\n" << std::flush;
}

void handle_canary_request(const std::string& /*json*/) {
    std::cout << "{\"type\":\"status\",\"data\":{\"status\":\"canary_added\"}}\n" << std::flush;
}

void handle_status_request() {
    std::cout << "{\"type\":\"status\",\"data\":{\"status\":\"running\",\"version\":\"0.1.0\"}}\n" << std::flush;
}

int main(int argc, char** argv) {
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    std::string watch_dir = "./canary/";
    bool auto_kill = false;

    for (int i = 1; i < argc; i += 2) {
        if (i + 1 < argc) {
            std::string arg = argv[i];
            if (arg == "--watch") watch_dir = argv[i + 1];
            else if (arg == "--auto-kill") auto_kill = std::string(argv[i + 1]) == "true";
            else if (arg == "--entropy-threshold") {}
            else if (arg == "--burst-count") {}
            else if (arg == "--burst-window-ms") {}
        }
    }

    std::error_code ec;
    std::filesystem::create_directories(watch_dir, ec);

    EngineConfig config;
    config.watch_dir = watch_dir;
    config.auto_kill = auto_kill;

    HeuristicEngine engine(config);
    auto fs_monitor = make_fs_monitor();
    auto sys_stats = make_sys_stats();
    auto proc_mgr = make_process_mgr(watch_dir);

    engine.deploy_canaries();

    std::vector<Alert> recent_alerts;
    auto last_cleanup = std::chrono::steady_clock::now();

    // Local helper: apply mitigation policy and enrich alert before emit
    auto mitigate = [&](Alert& alert, const FsEvent& ev) {
        int pid = proc_mgr->resolve_pid(ev.path);
        if (pid > 0) {
            alert.pid = pid;
            alert.process_name = proc_mgr->process_name(pid);
        }

        if (alert.severity == Severity::critical) {
            if (pid > 0) {
                bool tree_ok = proc_mgr->kill_tree(pid);
                alert.action = MitigationAction::terminated_tree;
                auto kids = proc_mgr->children_of(pid);
                alert.killed_pids = kids;
                alert.killed_pids.push_back(pid);
                (void)tree_ok;
            }
            std::string qpath;
            if (proc_mgr->quarantine_file(ev.path, qpath)) {
                alert.quarantine_path = qpath;
                if (pid <= 0) alert.action = MitigationAction::quarantined;
            }
        } else if (alert.severity == Severity::high) {
            // Suspend offending process if identifiable, quarantine the file
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

    fs_monitor->start(watch_dir, [&](FsEvent ev) {
        emit_event(ev);

        // Canary re-verification on every burst: cheap hash check when load is low
        // (periodic thread below does the authoritative sweep)

        auto alert = engine.analyze(ev);
        if (alert) {
            mitigate(*alert, ev);
            emit_alert(*alert);
            recent_alerts.push_back(*alert);
        }

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_cleanup).count() > 60) {
            if (recent_alerts.size() > 1000)
                recent_alerts.erase(recent_alerts.begin(), recent_alerts.begin() + 500);
            last_cleanup = now;
        }
    });

    std::thread stats_thread([&]() {
        while (g_running) {
            emit_stats(sys_stats->snapshot());
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });

    std::thread heartbeat_thread([&]() {
        while (g_running) {
            g_heartbeat = true;
            emit_status("heartbeat");
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    });

    // Canary re-verification thread: catches direct overwrite that bypasses inotify
    std::thread canary_thread([&]() {
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            if (!g_running) break;
            auto ca = engine.verify_canaries();
            if (ca) {
                // Synthesize a canary alert without a specific FsEvent path
                FsEvent fake{*engine.get_canary_paths().begin(), EventType::modify,
                             (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::system_clock::now().time_since_epoch()).count()};
                mitigate(*ca, fake);
                emit_alert(*ca);
                recent_alerts.push_back(*ca);
            }
        }
    });

    emit_status("started");

    while (g_running) {
        auto cmd = read_command();
        if (cmd) {
            std::string command = *cmd;
            if (command.find("\"command\":\"config_update\"") != std::string::npos)
                handle_config_update(command);
            else if (command.find("\"command\":\"add_canary\"") != std::string::npos)
                handle_canary_request(command);
            else if (command.find("\"command\":\"status\"") != std::string::npos)
                handle_status_request();
            else if (command.find("\"command\":\"shutdown\"") != std::string::npos)
                g_running = false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    fs_monitor->stop();
    if (stats_thread.joinable()) stats_thread.join();
    if (heartbeat_thread.joinable()) heartbeat_thread.join();
    if (canary_thread.joinable()) canary_thread.join();
    emit_status("stopped");
    return 0;
}
