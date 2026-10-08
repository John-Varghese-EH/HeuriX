#pragma once

#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include "heurix/types.hpp"

namespace heurix {

struct EngineHooks {
    std::function<std::string(const EngineConfig&)> apply_config;
    std::function<EngineConfig()> current_config;
};

class GrpcServer {
public:
    GrpcServer(std::string address, EngineHooks hooks);
    ~GrpcServer();

    bool Start();
    void Stop();

private:
    std::string server_address_;
    EngineHooks hooks_;
    std::atomic<bool> running_{false};
    int server_fd_{-1};
    std::thread server_thread_;

    void run_listen_loop();
};

} // namespace heurix
