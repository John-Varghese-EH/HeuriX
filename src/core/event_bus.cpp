#include "heurix/event_bus.hpp"
#include <iostream>
#include <poll.h>
#include <unistd.h>

void emit_event(const FsEvent& event) {
    std::cout << "{\"type\":\"event\",\"data\":" << event.to_json() << "}\n" << std::flush;
}

void emit_alert(const Alert& alert) {
    std::cout << "{\"type\":\"alert\",\"data\":" << alert.to_json() << "}\n" << std::flush;
}

void emit_stats(const SystemStats& stats) {
    std::cout << "{\"type\":\"stats\",\"data\":" << stats.to_json() << "}\n" << std::flush;
}

void emit_status(const std::string& status) {
    std::cout << "{\"type\":\"status\",\"data\":{\"status\":\"" << status << "\"}}\n" << std::flush;
}

std::optional<std::string> read_command() {
    struct pollfd pfd = { STDIN_FILENO, POLLIN, 0 };
    if (poll(&pfd, 1, 0) > 0) {
        if (pfd.revents & POLLIN) {
            std::string line;
            if (std::getline(std::cin, line)) {
                return line;
            }
        }
    }
    return std::nullopt;
}

EngineConfig parse_config_update(const std::string& /*json*/) {
    // simplified parser for this example
    return EngineConfig{};
}
