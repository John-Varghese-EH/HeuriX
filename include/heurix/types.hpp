#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <string_view>

enum class Severity { info, low, medium, high, critical };
enum class EventType { modify, create, del, rename, unknown };
enum class MitigationAction { none, suspended, terminated, quarantined, isolated, terminated_tree };

constexpr std::string_view severity_str(Severity s) {
    switch (s) {
        case Severity::info:     return "info";
        case Severity::low:      return "low";
        case Severity::medium:   return "medium";
        case Severity::high:     return "high";
        case Severity::critical: return "critical";
    }
    return "unknown";
}

constexpr std::string_view event_type_str(EventType t) {
    switch (t) {
        case EventType::modify:  return "modify";
        case EventType::create:  return "create";
        case EventType::del:     return "delete";
        case EventType::rename:  return "rename";
        case EventType::unknown: return "unknown";
    }
    return "unknown";
}

constexpr std::string_view action_str(MitigationAction a) {
    switch (a) {
        case MitigationAction::none:            return "none";
        case MitigationAction::suspended:       return "suspended";
        case MitigationAction::terminated:      return "terminated";
        case MitigationAction::quarantined:     return "quarantined";
        case MitigationAction::isolated:        return "isolated";
        case MitigationAction::terminated_tree: return "terminated_tree";
    }
    return "none";
}

inline std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;
        }
    }
    return out;
}

struct FsEvent {
    std::string path;
    EventType type;
    uint64_t timestamp_ms;

    std::string to_json() const {
        return "{\"path\":\"" + json_escape(path) +
               "\",\"event_type\":\"" + std::string(event_type_str(type)) +
               "\",\"timestamp_ms\":" + std::to_string(timestamp_ms) + "}";
    }
};

struct Alert {
    Severity severity = Severity::info;
    std::string description;
    double entropy = 0.0;
    int pid = 0;
    std::string process_name;
    MitigationAction action = MitigationAction::none;
    std::string quarantine_path;
    std::vector<int> killed_pids;
    double threat_score = 0.0;
    uint64_t timestamp_ms = 0;

    std::string to_json() const {
        std::string j =
            "{\"severity\":\"" + std::string(severity_str(severity)) +
            "\",\"description\":\"" + json_escape(description) +
            "\",\"entropy\":" + std::to_string(entropy) +
            ",\"pid\":" + std::to_string(pid) +
            ",\"process_name\":\"" + json_escape(process_name) +
            "\",\"action\":\"" + std::string(action_str(action)) + "\"";
        if (!quarantine_path.empty()) {
            j += ",\"quarantine_path\":\"" + json_escape(quarantine_path) + "\"";
        }
        if (!killed_pids.empty()) {
            j += ",\"killed_pids\":[";
            for (size_t i = 0; i < killed_pids.size(); ++i) {
                if (i) j += ",";
                j += std::to_string(killed_pids[i]);
            }
            j += "]";
        }
        if (threat_score > 0.0) {
            j += ",\"threat_score\":" + std::to_string(threat_score);
        }
        j += ",\"timestamp_ms\":" + std::to_string(timestamp_ms) + "}";
        return j;
    }
};

struct SystemStats {
    double cpu_percent;
    double mem_percent;
    uint64_t mem_used_mb;
    uint64_t mem_total_mb;
    double io_read_mb;
    double io_write_mb;

    std::string to_json() const {
        return "{\"cpu_percent\":" + std::to_string(cpu_percent) +
               ",\"mem_percent\":" + std::to_string(mem_percent) +
               ",\"mem_used_mb\":" + std::to_string(mem_used_mb) +
               ",\"mem_total_mb\":" + std::to_string(mem_total_mb) +
               ",\"io_read_mb\":" + std::to_string(io_read_mb) +
               ",\"io_write_mb\":" + std::to_string(io_write_mb) + "}";
    }
};

struct EngineConfig {
    std::string watch_dir;
    double entropy_threshold = 7.5;
    int burst_count = 15;
    int burst_window_ms = 2000;
    bool auto_kill = false;
    bool auto_mitigate = true;
    std::string mitigation_action = "suspend"; // suspend|terminate|quarantine|isolate
    std::vector<std::string> safelist;
    std::vector<std::string> canary_files;
    int burst_rename_count = 10;
    int entropy_cascade_count = 5;

    // Machine Learning Heuristics
    bool enable_ml = false;
    std::string ml_model_path;
    double ml_threshold = 0.85;
    bool enable_feature_logging = false;
    std::string feature_log_path;
    std::string feature_log_label;
};
