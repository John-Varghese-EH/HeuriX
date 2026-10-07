#pragma once

#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <grpcpp/grpcpp.h>
#include "heurix.grpc.pb.h"
#include "heurix/types.hpp"

namespace heurix {

// Hooks the server uses to talk to the engine without owning it.
struct EngineHooks {
    // Apply a validated config. Return an empty string on success, otherwise
    // a human-readable reason the config was rejected.
    std::function<std::string(const EngineConfig&)> apply_config;
    // Return the config the engine is currently running with.
    std::function<EngineConfig()> current_config;
};

class HeurixDaemonServiceImpl final : public heurix::api::HeurixDaemon::Service {
public:
    explicit HeurixDaemonServiceImpl(EngineHooks hooks) : hooks_(std::move(hooks)) {}

    grpc::Status StreamTelemetry(grpc::ServerContext* context,
                                 const heurix::api::StreamRequest* request,
                                 grpc::ServerWriter<heurix::api::TelemetryStreamResponse>* writer) override;

    grpc::Status UpdateConfig(grpc::ServerContext* context,
                              const heurix::api::EngineConfig* request,
                              heurix::api::ConfigResponse* response) override;

    grpc::Status GetConfig(grpc::ServerContext* context,
                           const heurix::api::GetConfigRequest* request,
                           heurix::api::EngineConfig* response) override;

private:
    EngineHooks hooks_;
};

class GrpcServer {
public:
    GrpcServer(std::string address, EngineHooks hooks);
    ~GrpcServer();

    // Returns false if the server could not bind.
    bool Start();
    void Stop();

private:
    std::string server_address_;
    HeurixDaemonServiceImpl service_;
    std::unique_ptr<grpc::Server> server_;
    std::thread server_thread_;
};

} // namespace heurix
