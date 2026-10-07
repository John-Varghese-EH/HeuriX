#include "heurix/grpc_server.hpp"
#include "heurix/event_bus.hpp"
#include <chrono>
#include <iostream>

namespace heurix {

namespace {

void to_proto(const FsEvent& in, api::FsEvent* out) {
    out->set_path(in.path);
    out->set_event_type(std::string(event_type_str(in.type)));
    out->set_timestamp_ms(in.timestamp_ms);
}

void to_proto(const Alert& in, api::Alert* out) {
    out->set_severity(std::string(severity_str(in.severity)));
    out->set_description(in.description);
    out->set_entropy(in.entropy);
    out->set_pid(in.pid);
    out->set_process_name(in.process_name);
    out->set_action(std::string(action_str(in.action)));
    out->set_quarantine_path(in.quarantine_path);
    for (int pid : in.killed_pids) out->add_killed_pids(pid);
    out->set_threat_score(in.threat_score);
    out->set_timestamp_ms(in.timestamp_ms);
}

void to_proto(const SystemStats& in, api::SystemStats* out) {
    out->set_cpu_percent(in.cpu_percent);
    out->set_mem_percent(in.mem_percent);
    out->set_mem_used_mb(in.mem_used_mb);
    out->set_mem_total_mb(in.mem_total_mb);
    out->set_io_read_mb(in.io_read_mb);
    out->set_io_write_mb(in.io_write_mb);
}

void to_proto(const EngineConfig& in, api::EngineConfig* out) {
    out->set_watch_dir(in.watch_dir);
    out->set_entropy_threshold(in.entropy_threshold);
    out->set_burst_count(static_cast<uint32_t>(in.burst_count));
    out->set_burst_window_ms(static_cast<uint64_t>(in.burst_window_ms));
    out->set_auto_kill(in.auto_kill);
    out->set_auto_mitigate(in.auto_mitigate);
    out->set_mitigation_action(in.mitigation_action);
}

// Validates and converts. Returns an error string, or empty on success.
std::string from_proto(const api::EngineConfig& in, const EngineConfig& base, EngineConfig& out) {
    if (!(in.entropy_threshold() > 0.0 && in.entropy_threshold() <= 8.0))
        return "entropy_threshold must be in (0, 8]";
    if (in.burst_count() < 1 || in.burst_count() > 100000)
        return "burst_count must be in [1, 100000]";
    if (in.burst_window_ms() < 100 || in.burst_window_ms() > 600000)
        return "burst_window_ms must be in [100, 600000]";
    const std::string& act = in.mitigation_action();
    if (act != "suspend" && act != "terminate" && act != "quarantine" && act != "isolate")
        return "mitigation_action must be one of suspend|terminate|quarantine|isolate";

    out = base;  // keep fields the API does not expose (safelist, canaries, ...)
    out.entropy_threshold = in.entropy_threshold();
    out.burst_count = static_cast<int>(in.burst_count());
    out.burst_window_ms = static_cast<int>(in.burst_window_ms());
    out.auto_kill = in.auto_kill();
    out.auto_mitigate = in.auto_mitigate();
    out.mitigation_action = act;
    // watch_dir is intentionally NOT hot-swapped; see UpdateConfig.
    return {};
}

} // namespace

grpc::Status HeurixDaemonServiceImpl::StreamTelemetry(
        grpc::ServerContext* context,
        const api::StreamRequest* /*request*/,
        grpc::ServerWriter<api::TelemetryStreamResponse>* writer) {
    auto sub = EventBus::instance().subscribe();
    std::cerr << "[gRPC] telemetry client connected: " << context->peer() << std::endl;

    api::TelemetryStreamResponse resp;
    resp.set_status("connected");
    writer->Write(resp);

    while (!context->IsCancelled() && !sub->closed()) {
        auto msg = sub->pop(std::chrono::milliseconds(250));
        if (!msg) continue;   // timeout: re-check cancellation

        resp.Clear();
        std::visit([&](auto&& m) {
            using T = std::decay_t<decltype(m)>;
            if constexpr (std::is_same_v<T, FsEvent>)          to_proto(m, resp.mutable_event());
            else if constexpr (std::is_same_v<T, Alert>)       to_proto(m, resp.mutable_alert());
            else if constexpr (std::is_same_v<T, SystemStats>) to_proto(m, resp.mutable_stats());
            else                                               resp.set_status(m.status);
        }, *msg);

        if (!writer->Write(resp)) break;   // client went away
    }

    EventBus::instance().unsubscribe(sub);
    std::cerr << "[gRPC] telemetry client disconnected (dropped "
              << sub->dropped() << " low-priority msgs)" << std::endl;
    return grpc::Status::OK;
}

grpc::Status HeurixDaemonServiceImpl::UpdateConfig(grpc::ServerContext* /*context*/,
                                                   const api::EngineConfig* request,
                                                   api::ConfigResponse* response) {
    EngineConfig current = hooks_.current_config();
    EngineConfig next;
    std::string err = from_proto(*request, current, next);
    if (err.empty()) err = hooks_.apply_config(next);

    if (!err.empty()) {
        response->set_success(false);
        response->set_message(err);
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, err);
    }

    std::string msg = "Configuration applied.";
    if (!request->watch_dir().empty() && request->watch_dir() != current.watch_dir)
        msg += " watch_dir changes take effect after a daemon restart.";
    response->set_success(true);
    response->set_message(msg);
    return grpc::Status::OK;
}

grpc::Status HeurixDaemonServiceImpl::GetConfig(grpc::ServerContext* /*context*/,
                                                const api::GetConfigRequest* /*request*/,
                                                api::EngineConfig* response) {
    to_proto(hooks_.current_config(), response);
    return grpc::Status::OK;
}

GrpcServer::GrpcServer(std::string address, EngineHooks hooks)
    : server_address_(std::move(address)), service_(std::move(hooks)) {}

GrpcServer::~GrpcServer() { Stop(); }

bool GrpcServer::Start() {
    grpc::ServerBuilder builder;
    int bound_port = 0;
    // TODO(security): replace with a Unix domain socket / named pipe with
    // peer-credential checks. See walkthrough "Known gaps".
    builder.AddListeningPort(server_address_, grpc::InsecureServerCredentials(), &bound_port);
    builder.RegisterService(&service_);

    server_ = builder.BuildAndStart();
    if (!server_ || bound_port == 0) {
        std::cerr << "[gRPC] failed to bind " << server_address_ << std::endl;
        server_.reset();
        return false;
    }
    std::cerr << "[gRPC] HeuriX daemon listening on " << server_address_ << std::endl;
    server_thread_ = std::thread([this] { server_->Wait(); });
    return true;
}

void GrpcServer::Stop() {
    if (!server_) return;
    EventBus::instance().close_all();   // unblock streaming handlers
    server_->Shutdown(std::chrono::system_clock::now() + std::chrono::seconds(2));
    if (server_thread_.joinable()) server_thread_.join();
    server_.reset();
}

} // namespace heurix
