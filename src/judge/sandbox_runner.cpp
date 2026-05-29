#include "judge/sandbox_runner.h"

#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <signal.h>
#include <sstream>
#include <string>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>

namespace oj::judge {

std::string ReadFile(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs) return std::string();
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

void WriteFile(const std::string& path, const std::string& content) {
    std::ofstream ofs(path);
    ofs << content;
}

std::string ResolveSandboxPreloadPath() {
    const std::filesystem::path cwd = std::filesystem::current_path();
    const std::filesystem::path candidates[] = {
        cwd / "run" / "libjudge_sandbox_preload.so",
        cwd / "run" / "libsandbox_preload.so",
        cwd / "build" / "run" / "libjudge_sandbox_preload.so",
        cwd / "build" / "run" / "libsandbox_preload.so",
        cwd.parent_path() / "run" / "libjudge_sandbox_preload.so",
        cwd.parent_path() / "run" / "libsandbox_preload.so",
        cwd.parent_path() / "build" / "run" / "libjudge_sandbox_preload.so",
        cwd.parent_path() / "build" / "run" / "libsandbox_preload.so",
        std::filesystem::path("./run/libjudge_sandbox_preload.so"),
        std::filesystem::path("./run/libsandbox_preload.so"),
    };

    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return std::filesystem::absolute(candidate).string();
        }
    }
    return std::string();
}

int RunShellCommand(const std::string& cmd) {
    int ret = std::system(cmd.c_str());
    if (ret == -1) return -1;
    return WEXITSTATUS(ret);
}

RunResult RunProgramWithLimits(const std::string& binary,
                               const std::string& in_path,
                               const std::string& out_path,
                               const std::string& err_path,
                               int time_limit_ms,
                               int memory_limit_kb) {
    RunResult res;
    pid_t pid = fork();
    if (pid == -1) {
        res.status = "system_error";
        return res;
    }

    if (pid == 0) {
        struct rlimit rl;
        rl.rlim_cur = rl.rlim_max = (time_limit_ms + 999) / 1000;
        setrlimit(RLIMIT_CPU, &rl);

        rlim_t as_limit = static_cast<rlim_t>(memory_limit_kb) * 1024ULL;
        rl.rlim_cur = rl.rlim_max = as_limit;
        setrlimit(RLIMIT_AS, &rl);

        rl.rlim_cur = rl.rlim_max = 16;
        setrlimit(RLIMIT_NPROC, &rl);

        const std::string preload = ResolveSandboxPreloadPath();
        if (!preload.empty()) {
            setenv("LD_PRELOAD", preload.c_str(), 1);
        }

        int in_fd = open(in_path.c_str(), O_RDONLY);
        int out_fd = open(out_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
        int err_fd = open(err_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (in_fd >= 0) dup2(in_fd, STDIN_FILENO);
        if (out_fd >= 0) dup2(out_fd, STDOUT_FILENO);
        if (err_fd >= 0) dup2(err_fd, STDERR_FILENO);

        execl(binary.c_str(), binary.c_str(), (char*)NULL);
        _exit(127);
    }

    auto t0 = std::chrono::steady_clock::now();
    int status = 0;
    struct rusage usage;
    bool killed_for_timeout = false;
    while (true) {
        pid_t w = wait4(pid, &status, WNOHANG, &usage);
        auto t1 = std::chrono::steady_clock::now();
        int elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        if (w == 0) {
            if (elapsed_ms > time_limit_ms + 200) {
                kill(pid, SIGKILL);
                killed_for_timeout = true;
                wait4(pid, &status, 0, &usage);
                res.time_ms = elapsed_ms;
                break;
            }
            usleep(1000 * 5);
            continue;
        } else if (w == pid) {
            res.time_ms = elapsed_ms;
            break;
        } else {
            res.time_ms = elapsed_ms;
            break;
        }
    }

    res.stderr_txt = ReadFile(err_path);
    res.memory_kb = static_cast<int>(usage.ru_maxrss);

    if (killed_for_timeout) {
        res.status = "time_limit_exceeded";
        return res;
    }
    if (res.memory_kb > memory_limit_kb) {
        res.status = "memory_limit_exceeded";
        return res;
    }
    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        res.status = (sig == SIGKILL || sig == SIGXCPU) ? "time_limit_exceeded" : "runtime_error";
        return res;
    }
    if (WIFEXITED(status)) {
        res.exit_code = WEXITSTATUS(status);
        res.status = (res.exit_code == 0) ? "accepted" : "runtime_error";
    }
    return res;
}

} // namespace oj::judge
