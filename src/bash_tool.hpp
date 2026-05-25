// Bash tool header - execute commands with human-in-the-loop permission

#pragma once

#include <string>

namespace bash_tool {

enum class PermissionMode {
    Ask,         // Always ask before executing
    Bypass       // Skip permission prompt
};

PermissionMode get_permission_mode();
void set_permission_mode(PermissionMode mode);

std::string get_tool_definition();
std::string execute(const std::string& command);
std::string format_response_json(const std::string& command,
                                  const std::string& output,
                                  bool success);

} // namespace bash_tool