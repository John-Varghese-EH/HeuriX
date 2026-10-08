#include "heurix/grpc_server.hpp"
#include "heurix/event_bus.hpp"
#include <iostream>
#include <sstream>
#include <chrono>
#include <cstring>
#include <vector>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  using socket_t = SOCKET;
  #define IS_INVALID_SOCKET(s) ((s) == INVALID_SOCKET)
  #define CLOSE_SOCKET(s) ::closesocket(s)
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <fcntl.h>
  using socket_t = int;
  #define IS_INVALID_SOCKET(s) ((s) < 0)
  #define CLOSE_SOCKET(s) ::close(s)
#endif

namespace heurix {

GrpcServer::GrpcServer(std::string address, EngineHooks hooks)
    : server_address_(std::move(address)), hooks_(std::move(hooks)) {}

GrpcServer::~GrpcServer() {
    Stop();
}

bool GrpcServer::Start() {
    if (running_) return true;

#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    std::string ip = "127.0.0.1";
    int port = 50051;

    size_t colon = server_address_.find(':');
    if (colon != std::string::npos) {
        ip = server_address_.substr(0, colon);
        try {
            port = std::stoi(server_address_.substr(colon + 1));
        } catch (...) {
            port = 50051;
        }
    }

    server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (IS_INVALID_SOCKET(server_fd_)) {
        std::cerr << "[Server] Failed to create socket\n";
        return false;
    }

    int opt = 1;
#ifdef _WIN32
    ::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
    ::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    if (::bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[Server] Failed to bind socket to " << ip << ":" << port << "\n";
        CLOSE_SOCKET(server_fd_);
        server_fd_ = -1;
        return false;
    }

    if (::listen(server_fd_, 10) < 0) {
        std::cerr << "[Server] Failed to listen on socket\n";
        CLOSE_SOCKET(server_fd_);
        server_fd_ = -1;
        return false;
    }

    running_ = true;
    std::cout << "[Server] HeuriX daemon listening on " << ip << ":" << port << std::endl;

    server_thread_ = std::thread(&GrpcServer::run_listen_loop, this);
    return true;
}

void GrpcServer::Stop() {
    if (!running_) return;
    running_ = false;

    if (!IS_INVALID_SOCKET(server_fd_)) {
        CLOSE_SOCKET(server_fd_);
        server_fd_ = -1;
    }

    if (server_thread_.joinable()) {
        server_thread_.join();
    }
}

void GrpcServer::run_listen_loop() {
    auto sub = EventBus::instance().subscribe();

    while (running_) {
        struct sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        
        socket_t client_fd = ::accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
        if (IS_INVALID_SOCKET(client_fd)) {
            if (!running_) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        std::cout << "[Server] Telemetry client connected: " << client_ip << ":" << ntohs(client_addr.sin_port) << std::endl;

        // Send initial greeting header
        std::string welcome = "{\"status\":\"started\",\"version\":\"0.1.0\",\"engine\":\"HeuriX\"}\n";
        ::send(client_fd, welcome.c_str(), (int)welcome.size(), 0);

        // Client handling loop
        while (running_) {
            auto msg = sub->pop(std::chrono::milliseconds(200));
            if (!msg) {
                // Heartbeat ping
                std::string ping = "{\"status\":\"heartbeat\"}\n";
                if (::send(client_fd, ping.c_str(), (int)ping.size(), 0) <= 0) {
                    break; // Client disconnected
                }
                continue;
            }

            std::stringstream ss;
            std::visit([&](auto&& m) {
                using T = std::decay_t<decltype(m)>;
                if constexpr (std::is_same_v<T, FsEvent>) {
                    ss << "{\"type\":\"event\",\"path\":\"" << m.path << "\",\"event_type\":\""
                       << event_type_str(m.type) << "\",\"timestamp_ms\":" << m.timestamp_ms << "}\n";
                } else if constexpr (std::is_same_v<T, Alert>) {
                    ss << "{\"type\":\"alert\",\"severity\":\"" << severity_str(m.severity)
                       << "\",\"description\":\"" << m.description << "\",\"entropy\":" << m.entropy
                       << ",\"threat_score\":" << m.threat_score << ",\"pid\":" << m.pid
                       << ",\"process_name\":\"" << m.process_name << "\",\"action\":\""
                       << action_str(m.action) << "\",\"timestamp_ms\":" << m.timestamp_ms << "}\n";
                } else if constexpr (std::is_same_v<T, SystemStats>) {
                    ss << "{\"type\":\"stats\",\"cpu_percent\":" << m.cpu_percent
                       << ",\"mem_percent\":" << m.mem_percent << ",\"mem_used_mb\":" << m.mem_used_mb
                       << ",\"mem_total_mb\":" << m.mem_total_mb << "}\n";
                } else {
                    ss << "{\"type\":\"status\",\"status\":\"" << m.status << "\"}\n";
                }
            }, *msg);

            std::string payload = ss.str();
            if (::send(client_fd, payload.c_str(), (int)payload.size(), 0) <= 0) {
                break; // Client disconnected
            }
        }

        CLOSE_SOCKET(client_fd);
        std::cout << "[Server] Telemetry client disconnected: " << client_ip << std::endl;
    }

    EventBus::instance().unsubscribe(sub);
}

} // namespace heurix
