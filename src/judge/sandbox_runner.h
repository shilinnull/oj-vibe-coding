#pragma once

#include <string>

namespace oj::judge {

struct RunResult {
    int exit_code{-1};
    std::string status{"runtime_error"};
    int time_ms{0};
    int memory_kb{0};
    std::string stderr_txt;
};

std::string ReadFile(const std::string& path);
void WriteFile(const std::string& path, const std::string& content);
std::string ResolveSandboxPreloadPath();
int RunShellCommand(const std::string& cmd);
RunResult RunProgramWithLimits(const std::string& binary,
                               const std::string& in_path,
                               const std::string& out_path,
                               const std::string& err_path,
                               int time_limit_ms,
                               int memory_limit_kb);

} // namespace oj::judge
