#pragma once
#include "types.hpp"
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <variant>
#include <vector>

// A telemetry message flowing from the engine to any connected client.
struct StatusMsg { std::string status; };
using TelemetryMsg = std::variant<FsEvent, Alert, SystemStats, StatusMsg>;

// Per-client bounded queue. Producers never block: when full, low-value
// messages (events/stats) are dropped first so alerts always get through.
class Subscription {
public:
    explicit Subscription(size_t capacity) : capacity_(capacity) {}

    void push(TelemetryMsg msg);

    // Waits up to `timeout` for a message. Returns nullopt on timeout/close.
    std::optional<TelemetryMsg> pop(std::chrono::milliseconds timeout);

    void close();
    bool closed() const;
    uint64_t dropped() const;

private:
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<TelemetryMsg> q_;
    size_t capacity_;
    uint64_t dropped_ = 0;
    bool closed_ = false;
};

// Process-wide broadcaster. Thread-safe.
class EventBus {
public:
    static EventBus& instance();

    std::shared_ptr<Subscription> subscribe(size_t capacity = 8192);
    void unsubscribe(const std::shared_ptr<Subscription>& sub);
    void publish(const TelemetryMsg& msg);
    void close_all();

private:
    std::mutex mu_;
    std::vector<std::shared_ptr<Subscription>> subs_;
};

// Convenience wrappers used by the engine.
void emit_event(const FsEvent& event);
void emit_alert(const Alert& alert);
void emit_stats(const SystemStats& stats);
void emit_status(const std::string& status);
