#pragma once

#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include "heurix/types.hpp"

// Forward declaration of httplib::Server to avoid including httplib.h in the header
namespace httplib {
    class Server;
}

namespace heurix {

struct EngineHooks {
    std::function<std::string(const EngineConfig&)> apply_config;
    std::function<EngineConfig()> current_config;
};

class ApiServer {
public:
    ApiServer(std::string address, EngineHooks hooks);
    ~ApiServer();

    bool Start();
    void Stop();

private:
    std::string server_address_;
    EngineHooks hooks_;
    std::atomic<bool> running_{false};
    std::unique_ptr<httplib::Server> srv_;
    std::thread server_thread_;
};

} // namespace heurix
