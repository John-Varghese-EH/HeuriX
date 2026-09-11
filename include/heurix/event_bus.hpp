#pragma once
#include "types.hpp"
#include <string>
#include <optional>

void emit_event(const FsEvent& event);
void emit_alert(const Alert& alert);
void emit_stats(const SystemStats& stats);
void emit_status(const std::string& status);
std::optional<std::string> read_command();
EngineConfig parse_config_update(const std::string& json);
