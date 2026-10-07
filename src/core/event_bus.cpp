#include "heurix/event_bus.hpp"
#include <algorithm>
#include <iostream>

// ---------------------------------------------------------------- Subscription

void Subscription::push(TelemetryMsg msg) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (closed_) return;

        if (q_.size() >= capacity_) {
            const bool incoming_is_alert = std::holds_alternative<Alert>(msg);
            // Try to evict the oldest non-alert message.
            auto victim = std::find_if(q_.begin(), q_.end(), [](const TelemetryMsg& m) {
                return !std::holds_alternative<Alert>(m);
            });
            if (victim != q_.end()) {
                q_.erase(victim);
            } else if (incoming_is_alert) {
                q_.pop_front();            // queue is all alerts: drop oldest alert
            } else {
                ++dropped_;                // queue is all alerts: drop incoming noise
                return;
            }
            ++dropped_;
        }
        q_.push_back(std::move(msg));
    }
    cv_.notify_one();
}

std::optional<TelemetryMsg> Subscription::pop(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mu_);
    if (!cv_.wait_for(lock, timeout, [this] { return closed_ || !q_.empty(); }))
        return std::nullopt;
    if (q_.empty()) return std::nullopt;   // closed
    TelemetryMsg msg = std::move(q_.front());
    q_.pop_front();
    return msg;
}

void Subscription::close() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        closed_ = true;
    }
    cv_.notify_all();
}

bool Subscription::closed() const {
    std::lock_guard<std::mutex> lock(mu_);
    return closed_;
}

uint64_t Subscription::dropped() const {
    std::lock_guard<std::mutex> lock(mu_);
    return dropped_;
}

// -------------------------------------------------------------------- EventBus

EventBus& EventBus::instance() {
    static EventBus bus;
    return bus;
}

std::shared_ptr<Subscription> EventBus::subscribe(size_t capacity) {
    auto sub = std::make_shared<Subscription>(capacity);
    std::lock_guard<std::mutex> lock(mu_);
    subs_.push_back(sub);
    return sub;
}

void EventBus::unsubscribe(const std::shared_ptr<Subscription>& sub) {
    sub->close();
    std::lock_guard<std::mutex> lock(mu_);
    subs_.erase(std::remove(subs_.begin(), subs_.end(), sub), subs_.end());
}

void EventBus::publish(const TelemetryMsg& msg) {
    // Copy the subscriber list so slow consumers never hold the bus lock.
    std::vector<std::shared_ptr<Subscription>> snapshot;
    {
        std::lock_guard<std::mutex> lock(mu_);
        snapshot = subs_;
    }
    for (auto& s : snapshot) s->push(msg);
}

void EventBus::close_all() {
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& s : subs_) s->close();
    subs_.clear();
}

// ------------------------------------------------------------------- Wrappers
// Alerts and status changes are also logged to stderr so they land in
// journald / the Windows event log even when no UI is connected.

void emit_event(const FsEvent& event) {
    EventBus::instance().publish(event);
}

void emit_alert(const Alert& alert) {
    std::cerr << "[ALERT] " << severity_str(alert.severity) << " score="
              << alert.threat_score << " pid=" << alert.pid << " "
              << alert.description << std::endl;
    EventBus::instance().publish(alert);
}

void emit_stats(const SystemStats& stats) {
    EventBus::instance().publish(stats);
}

void emit_status(const std::string& status) {
    if (status != "heartbeat") std::cerr << "[STATUS] " << status << std::endl;
    EventBus::instance().publish(StatusMsg{status});
}
