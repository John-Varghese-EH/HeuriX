#include "heurix/api_server.hpp"
#include "heurix/event_bus.hpp"
#include "heurix/httplib.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <optional>
#include <variant>

namespace heurix {

ApiServer::ApiServer(std::string address, EngineHooks hooks)
    : server_address_(std::move(address)), hooks_(std::move(hooks)) {
    srv_ = std::make_unique<httplib::Server>();
}

ApiServer::~ApiServer() {
    Stop();
}

// ---------------------------------------------------------------------------
// Minimal, correct JSON string escaping for serialization.
// ---------------------------------------------------------------------------
static std::string jesc(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    // Escape other control characters.
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Minimal but correct JSON parser for the /config POST body.
//
// Supports flat objects: {"key": value, ...} where value is a JSON string,
// number, or boolean. Does NOT support nested objects or arrays (not needed).
// Correctly handles Unicode escapes and backslash sequences inside strings.
// ---------------------------------------------------------------------------
namespace json_parse {

// Skip whitespace.
static void skip_ws(const std::string& s, size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
        ++i;
}

// Parse a JSON string starting at '"'. Returns the decoded value.
static std::optional<std::string> parse_string(const std::string& s, size_t& i) {
    if (i >= s.size() || s[i] != '"') return std::nullopt;
    ++i; // consume opening '"'
    std::string result;
    while (i < s.size() && s[i] != '"') {
        if (s[i] == '\\') {
            ++i;
            if (i >= s.size()) return std::nullopt;
            switch (s[i]) {
                case '"':  result += '"';  break;
                case '\\': result += '\\'; break;
                case '/':  result += '/';  break;
                case 'n':  result += '\n'; break;
                case 'r':  result += '\r'; break;
                case 't':  result += '\t'; break;
                case 'b':  result += '\b'; break;
                case 'f':  result += '\f'; break;
                case 'u': {
                    // \uXXXX — minimal handling: accept BMP codepoints, encode as UTF-8.
                    if (i + 4 >= s.size()) return std::nullopt;
                    std::string hex = s.substr(i + 1, 4);
                    i += 4;
                    unsigned cp = 0;
                    for (char c : hex) {
                        cp <<= 4;
                        if (c >= '0' && c <= '9') cp |= c - '0';
                        else if (c >= 'a' && c <= 'f') cp |= 10 + c - 'a';
                        else if (c >= 'A' && c <= 'F') cp |= 10 + c - 'A';
                        else return std::nullopt;
                    }
                    if (cp < 0x80) {
                        result += static_cast<char>(cp);
                    } else if (cp < 0x800) {
                        result += static_cast<char>(0xC0 | (cp >> 6));
                        result += static_cast<char>(0x80 | (cp & 0x3F));
                    } else {
                        result += static_cast<char>(0xE0 | (cp >> 12));
                        result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        result += static_cast<char>(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default: result += s[i]; break;
            }
        } else {
            result += s[i];
        }
        ++i;
    }
    if (i >= s.size() || s[i] != '"') return std::nullopt;
    ++i; // consume closing '"'
    return result;
}

using JsonValue = std::variant<std::string, double, bool, std::monostate>;

// Parse a JSON value (string, number, boolean, or null).
static JsonValue parse_value(const std::string& s, size_t& i) {
    skip_ws(s, i);
    if (i >= s.size()) return std::monostate{};
    if (s[i] == '"') {
        auto v = parse_string(s, i);
        return v ? JsonValue{*v} : JsonValue{std::monostate{}};
    }
    if (s.substr(i, 4) == "true")  { i += 4; return true; }
    if (s.substr(i, 5) == "false") { i += 5; return false; }
    if (s.substr(i, 4) == "null")  { i += 4; return std::monostate{}; }
    // Number
    size_t start = i;
    if (s[i] == '-') ++i;
    while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) ||
           s[i] == '.' || s[i] == 'e' || s[i] == 'E' || s[i] == '+' || s[i] == '-')) {
        ++i;
    }
    if (i > start) {
        try { return std::stod(s.substr(start, i - start)); } catch (...) {}
    }
    return std::monostate{};
}

// Parse a flat JSON object into a key→value map.
// Returns false if the input is not a valid JSON object.
static bool parse_object(const std::string& body,
                         std::unordered_map<std::string, JsonValue>& out) {
    size_t i = 0;
    skip_ws(body, i);
    if (i >= body.size() || body[i] != '{') return false;
    ++i;
    skip_ws(body, i);
    if (i < body.size() && body[i] == '}') return true; // empty object

    while (i < body.size()) {
        skip_ws(body, i);
        if (body[i] != '"') return false;
        auto key = parse_string(body, i);
        if (!key) return false;
        skip_ws(body, i);
        if (i >= body.size() || body[i] != ':') return false;
        ++i;
        skip_ws(body, i);
        out[*key] = parse_value(body, i);
        skip_ws(body, i);
        if (i >= body.size()) break;
        if (body[i] == '}') break;
        if (body[i] != ',') return false;
        ++i;
    }
    return true;
}

// Convenience getters.
static std::optional<std::string> get_str(const std::unordered_map<std::string, JsonValue>& m,
                                           const std::string& k) {
    auto it = m.find(k);
    if (it == m.end()) return std::nullopt;
    if (auto* v = std::get_if<std::string>(&it->second)) return *v;
    return std::nullopt;
}
static std::optional<double> get_num(const std::unordered_map<std::string, JsonValue>& m,
                                      const std::string& k) {
    auto it = m.find(k);
    if (it == m.end()) return std::nullopt;
    if (auto* v = std::get_if<double>(&it->second)) return *v;
    return std::nullopt;
}
static std::optional<bool> get_bool(const std::unordered_map<std::string, JsonValue>& m,
                                     const std::string& k) {
    auto it = m.find(k);
    if (it == m.end()) return std::nullopt;
    if (auto* v = std::get_if<bool>(&it->second)) return *v;
    return std::nullopt;
}

} // namespace json_parse

bool ApiServer::Start() {
    if (running_) return true;

    std::string ip = "127.0.0.1";
    int port = 50051;

    size_t colon = server_address_.rfind(':');
    if (colon != std::string::npos) {
        ip = server_address_.substr(0, colon);
        try {
            port = std::stoi(server_address_.substr(colon + 1));
        } catch (...) {
            port = 50051;
        }
    }

    // ---------- GET /config ----------
    srv_->Get("/config", [this](const httplib::Request&, httplib::Response& res) {
        auto cfg = hooks_.current_config();
        std::ostringstream ss;
        ss << "{"
           << "\"watch_dir\":\"" << jesc(cfg.watch_dir) << "\","
           << "\"entropy_threshold\":" << cfg.entropy_threshold << ","
           << "\"burst_count\":" << cfg.burst_count << ","
           << "\"burst_window_ms\":" << cfg.burst_window_ms << ","
           << "\"auto_kill\":" << (cfg.auto_kill ? "true" : "false") << ","
           << "\"auto_mitigate\":" << (cfg.auto_mitigate ? "true" : "false") << ","
           << "\"mitigation_action\":\"" << jesc(cfg.mitigation_action) << "\","
           << "\"enable_ml\":" << (cfg.enable_ml ? "true" : "false") << ","
           << "\"ml_model_path\":\"" << jesc(cfg.ml_model_path) << "\","
           << "\"ml_threshold\":" << cfg.ml_threshold
           << "}";
        res.set_content(ss.str(), "application/json");
    });

    // ---------- POST /config ----------
    srv_->Post("/config", [this](const httplib::Request& req, httplib::Response& res) {
        std::unordered_map<std::string, json_parse::JsonValue> fields;
        if (!json_parse::parse_object(req.body, fields)) {
            res.status = 400;
            res.set_content("{\"success\":false,\"message\":\"Invalid JSON body\"}", "application/json");
            return;
        }

        auto cfg = hooks_.current_config();

        if (auto v = json_parse::get_str(fields, "watch_dir"))        cfg.watch_dir = *v;
        if (auto v = json_parse::get_str(fields, "mitigation_action")) cfg.mitigation_action = *v;
        if (auto v = json_parse::get_num(fields, "entropy_threshold")) cfg.entropy_threshold = *v;
        if (auto v = json_parse::get_num(fields, "burst_count"))       cfg.burst_count = static_cast<int>(*v);
        if (auto v = json_parse::get_num(fields, "burst_window_ms"))   cfg.burst_window_ms = static_cast<int>(*v);
        if (auto v = json_parse::get_bool(fields, "auto_kill"))        cfg.auto_kill = *v;
        if (auto v = json_parse::get_bool(fields, "auto_mitigate"))    cfg.auto_mitigate = *v;
        if (auto v = json_parse::get_bool(fields, "enable_ml"))        cfg.enable_ml = *v;
        if (auto v = json_parse::get_num(fields, "ml_threshold"))      cfg.ml_threshold = *v;

        // Basic input validation.
        if (cfg.entropy_threshold < 1.0 || cfg.entropy_threshold > 8.0) {
            res.status = 400;
            res.set_content("{\"success\":false,\"message\":\"entropy_threshold must be in [1.0, 8.0]\"}", "application/json");
            return;
        }
        if (cfg.burst_count < 1 || cfg.burst_count > 10000) {
            res.status = 400;
            res.set_content("{\"success\":false,\"message\":\"burst_count out of range\"}", "application/json");
            return;
        }
        if (cfg.burst_window_ms < 100 || cfg.burst_window_ms > 60000) {
            res.status = 400;
            res.set_content("{\"success\":false,\"message\":\"burst_window_ms out of range [100, 60000]\"}", "application/json");
            return;
        }

        std::string err = hooks_.apply_config(cfg);
        if (!err.empty()) {
            res.status = 400;
            res.set_content("{\"success\":false,\"message\":\"" + jesc(err) + "\"}", "application/json");
        } else {
            res.set_content("{\"success\":true,\"message\":\"\"}", "application/json");
        }
    });

    // ---------- GET /telemetry (NDJSON chunked stream) ----------
    srv_->Get("/telemetry", [this](const httplib::Request&, httplib::Response& res) {
        auto sub = EventBus::instance().subscribe();

        res.set_chunked_content_provider(
            "application/x-ndjson",
            [sub, this](size_t /*offset*/, httplib::DataSink& sink) {
                if (!running_) {
                    EventBus::instance().unsubscribe(sub);
                    return false;
                }
                auto msg = sub->pop(std::chrono::milliseconds(500));
                if (!msg) {
                    // Keep-alive ping so the client doesn't time out.
                    const char ping[] = "{\"type\":\"status\",\"data\":{\"status\":\"heartbeat\"}}\n";
                    sink.write(ping, sizeof(ping) - 1);
                    return true;
                }

                std::ostringstream ss;
                std::visit([&](auto&& m) {
                    using T = std::decay_t<decltype(m)>;
                    if constexpr (std::is_same_v<T, FsEvent>) {
                        ss << "{\"type\":\"event\",\"data\":{\"path\":\"" << jesc(m.path)
                           << "\",\"event_type\":\"" << event_type_str(m.type)
                           << "\",\"timestamp_ms\":" << m.timestamp_ms << "}}\n";
                    } else if constexpr (std::is_same_v<T, Alert>) {
                        ss << "{\"type\":\"alert\",\"data\":{\"severity\":\"" << severity_str(m.severity)
                           << "\",\"description\":\"" << jesc(m.description)
                           << "\",\"entropy\":" << m.entropy
                           << ",\"threat_score\":" << m.threat_score
                           << ",\"pid\":" << m.pid
                           << ",\"process_name\":\"" << jesc(m.process_name)
                           << "\",\"action\":\"" << action_str(m.action)
                           << "\",\"timestamp_ms\":" << m.timestamp_ms;
                        if (!m.quarantine_path.empty())
                            ss << ",\"quarantine_path\":\"" << jesc(m.quarantine_path) << "\"";
                        if (!m.killed_pids.empty()) {
                            ss << ",\"killed_pids\":[";
                            for (size_t i = 0; i < m.killed_pids.size(); ++i) {
                                if (i) ss << ",";
                                ss << m.killed_pids[i];
                            }
                            ss << "]";
                        }
                        ss << "}}\n";
                    } else if constexpr (std::is_same_v<T, SystemStats>) {
                        ss << "{\"type\":\"stats\",\"data\":{\"cpu_percent\":" << m.cpu_percent
                           << ",\"mem_percent\":" << m.mem_percent
                           << ",\"mem_used_mb\":" << m.mem_used_mb
                           << ",\"mem_total_mb\":" << m.mem_total_mb
                           << ",\"io_read_mb\":" << m.io_read_mb
                           << ",\"io_write_mb\":" << m.io_write_mb << "}}\n";
                    } else {
                        ss << "{\"type\":\"status\",\"data\":{\"status\":\"" << jesc(m.status) << "\"}}\n";
                    }
                }, *msg);

                std::string payload = ss.str();
                return sink.write(payload.c_str(), payload.size());
            },
            [sub](bool /*success*/) {
                EventBus::instance().unsubscribe(sub);
            }
        );
    });

    // ---------- GET /health ----------
    srv_->Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"status\":\"ok\",\"version\":\"0.1.0\"}", "application/json");
    });

    // ---------- GET /stats (snapshot, no streaming) ----------
    srv_->Get("/stats", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"message\":\"Use /telemetry for live stats stream\"}", "application/json");
    });

    running_ = true;
    std::cerr << "[Server] HeuriX daemon listening on " << ip << ":" << port << std::endl;

    server_thread_ = std::thread([this, ip, port]() {
        srv_->listen(ip.c_str(), port);
    });
    return true;
}

void ApiServer::Stop() {
    if (!running_) return;
    running_ = false;
    if (srv_) srv_->stop();
    if (server_thread_.joinable()) server_thread_.join();
}

} // namespace heurix
