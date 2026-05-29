// judger_cli.cpp
// 负责：读取 JSON 输入（stdin），编译源码，针对每个测试用例运行并进行严格输出比对，最终把结果以 JSON 输出到 stdout。
// 注意：运行阶段通过 preload 沙箱 + rlimit 做进程、文件和资源约束，用于本地验收与安全测试。

#include <nlohmann/json.hpp>
#include <filesystem>
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <chrono>
#include <signal.h>
#include "utils/logger.h"
#include "judge/sandbox_runner.h"

using json = nlohmann::json;

static std::string read_stdin_all() {
    std::ostringstream ss;
    ss << std::cin.rdbuf();
    return ss.str();
}

static std::string normalize_output(std::string text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '\t')) {
        text.pop_back();
    }
    return text;
}

int main() {
    {
        oj::LoggingConfig log_cfg;
        log_cfg.to_stdout = true;
        oj::Logger::Instance().Init(log_cfg);
    }

    // 读取 stdin 的 JSON 参数
    std::string input = read_stdin_all();
    if (input.empty()) {
        OJ_LOG_ERROR("judger_cli: 没有从 stdin 读取到输入 JSON");
        return 1;
    }

    json req;
    try {
        req = json::parse(input);
    } catch (std::exception& e) {
        json out = {{"status", "system_error"}, {"error", std::string("JSON parse error: ") + e.what()}};
        std::cout << out.dump() << std::endl;
        return 1;
    }

    std::string language = req.value("language", "cpp");
    std::string source_code = req.value("source_code", std::string());
    int time_limit_ms = req.value("time_limit_ms", 1000);
    int max_time_limit_ms = 10000; // default 10s
    const char* env_max = std::getenv("JUDGER_MAX_TIME_MS");
    if (env_max) {
        try {
            int v = std::stoi(std::string(env_max));
            if (v > 0) max_time_limit_ms = v;
        } catch (...) {}
    }
    if (time_limit_ms > max_time_limit_ms) time_limit_ms = max_time_limit_ms;
    int memory_limit_kb = req.value("memory_limit_kb", 262144);
    std::string work_dir = req.value("work_dir", std::string("./run/judge"));
    std::string compile_cmd = req.value("compile_cmd", std::string("g++ -O2 -std=c++17 {source} -o {output}"));
    std::string run_cmd = req.value("run_cmd", std::string("{binary}"));

    // 准备 work_dir
    mkdir(work_dir.c_str(), 0755);

    std::string source_path = work_dir + "/user_code.cpp";
    std::string binary_path = work_dir + "/user_bin";
    std::string compile_err_path = work_dir + "/compile_err.txt";

    oj::judge::WriteFile(source_path, source_code);

    auto replace_all = [](std::string s, const std::string& a, const std::string& b){
        size_t pos = 0;
        while ((pos = s.find(a, pos)) != std::string::npos) {
            s.replace(pos, a.length(), b);
            pos += b.length();
        }
        return s;
    };

    std::string compile_command = compile_cmd;
    compile_command = replace_all(compile_command, "{source}", source_path);
    compile_command = replace_all(compile_command, "{output}", binary_path);
    compile_command += " 2> \"" + compile_err_path + "\"";

    int compile_ret = oj::judge::RunShellCommand(compile_command);
    json out;

    if (compile_ret != 0) {
        std::string ce = oj::judge::ReadFile(compile_err_path);
        out["status"] = "compile_error";
        out["compile_error"] = ce;
        out["results"] = json::array();
        std::cout << out.dump() << std::endl;
        return 0;
    }

    out["status"] = "accepted";
    out["compile_error"] = nullptr;
    out["results"] = json::array();

    if (!req.contains("test_cases") || !req["test_cases"].is_array()) {
        out["status"] = "system_error";
        out["error"] = "no test_cases provided";
        std::cout << out.dump() << std::endl;
        return 0;
    }

    int total_time_ms = 0;
    int peak_memory_kb = 0;
    int passed = 0;
    int total = 0;

    for (const auto& tc : req["test_cases"]) {
        total++;
        int tc_id = tc.value("id", 0);
        std::string input_data = tc.value("input", std::string());
        std::string expected = tc.value("expected_output", std::string());

        std::string in_path = work_dir + "/tc_in_" + std::to_string(tc_id) + ".txt";
        std::string out_path = work_dir + "/tc_out_" + std::to_string(tc_id) + ".txt";
        std::string run_err = work_dir + "/run_err_" + std::to_string(tc_id) + ".txt";

        oj::judge::WriteFile(in_path, input_data);

        oj::judge::RunResult rr = oj::judge::RunProgramWithLimits(binary_path, in_path, out_path, run_err, time_limit_ms, memory_limit_kb);
        total_time_ms += rr.time_ms;
        if (rr.memory_kb > peak_memory_kb) peak_memory_kb = rr.memory_kb;

        const size_t MAX_ACTUAL_OUTPUT = 64 * 1024; // 64 KB per test case
        std::string actual_full = oj::judge::ReadFile(out_path);
        bool actual_truncated = false;
        std::string actual = actual_full;
        if (actual_full.size() > MAX_ACTUAL_OUTPUT) {
            actual = actual_full.substr(0, MAX_ACTUAL_OUTPUT);
            actual_truncated = true;
        }

        json r;
        r["test_case_id"] = tc_id;
        r["time_ms"] = rr.time_ms;
        r["memory_kb"] = rr.memory_kb;
        r["actual_output"] = actual;
        if (actual_truncated) r["actual_truncated"] = true;
        r["expected_output"] = expected;

        if (rr.status == "accepted") {
            if (normalize_output(actual) == normalize_output(expected)) {
                r["status"] = "accepted";
                passed++;
            } else {
                r["status"] = "wrong_answer";
                out["status"] = "wrong_answer";
            }
        } else if (rr.status == "time_limit_exceeded") {
            r["status"] = "time_limit_exceeded";
            out["status"] = "time_limit_exceeded";
            r["stderr"] = rr.stderr_txt;
        } else if (rr.status == "memory_limit_exceeded") {
            r["status"] = "memory_limit_exceeded";
            out["status"] = "memory_limit_exceeded";
            r["stderr"] = rr.stderr_txt;
        } else if (rr.status == "runtime_error") {
            r["status"] = "runtime_error";
            out["status"] = "runtime_error";
            r["stderr"] = rr.stderr_txt;
        } else {
            r["status"] = rr.status;
            out["status"] = rr.status;
            r["stderr"] = rr.stderr_txt;
        }

        out["results"].push_back(r);
    }

    out["summary"] = {
        {"total", total},
        {"passed", passed},
        {"total_time_ms", total_time_ms},
        {"peak_memory_kb", peak_memory_kb}
    };

    std::cout << out.dump() << std::endl;
    return 0;
}
// judger_cli.cpp
// 负责：读取 JSON 输入（stdin），编译源码，针对每个测试用例运行并进行严格输出比对，最终把结果以 JSON 输出到 stdout。
// 注意：运行阶段通过 preload 沙箱 + rlimit 做进程、文件和资源约束，用于本地验收与安全测试。

#include <nlohmann/json.hpp>
#include <filesystem>
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <chrono>
#include <signal.h>
#include "utils/logger.h"
#include "judge/sandbox_runner.h"

using json = nlohmann::json;

static std::string read_stdin_all() {
    std::ostringstream ss;
    ss << std::cin.rdbuf();
    return ss.str();
}

static std::string normalize_output(std::string text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '\t')) {
        text.pop_back();
    }
    return text;
}

int main() {
    {
        oj::LoggingConfig log_cfg;
        log_cfg.to_stdout = true;
        oj::Logger::Instance().Init(log_cfg);
    }

    // 读取 stdin 的 JSON 参数
    std::string input = read_stdin_all();
    if (input.empty()) {
        OJ_LOG_ERROR("judger_cli: 没有从 stdin 读取到输入 JSON");
        return 1;
    }

    json req;
    try {
        req = json::parse(input);
    } catch (std::exception& e) {
        json out = {{"status", "system_error"}, {"error", std::string("JSON parse error: ") + e.what()}};
        std::cout << out.dump() << std::endl;
        return 1;
    }

    std::string language = req.value("language", "cpp");
    std::string source_code = req.value("source_code", std::string());
    int time_limit_ms = req.value("time_limit_ms", 1000);
    int max_time_limit_ms = 10000; // default 10s
    const char* env_max = std::getenv("JUDGER_MAX_TIME_MS");
    if (env_max) {
        try {
            int v = std::stoi(std::string(env_max));
            if (v > 0) max_time_limit_ms = v;
        } catch (...) {}
    }
    if (time_limit_ms > max_time_limit_ms) time_limit_ms = max_time_limit_ms;
    int memory_limit_kb = req.value("memory_limit_kb", 262144);
    std::string work_dir = req.value("work_dir", std::string("./run/judge"));
    std::string compile_cmd = req.value("compile_cmd", std::string("g++ -O2 -std=c++17 {source} -o {output}"));
    std::string run_cmd = req.value("run_cmd", std::string("{binary}"));

    // 准备 work_dir
    mkdir(work_dir.c_str(), 0755);

    std::string source_path = work_dir + "/user_code.cpp";
    std::string binary_path = work_dir + "/user_bin";
    std::string compile_err_path = work_dir + "/compile_err.txt";

    oj::judge::WriteFile(source_path, source_code);

    // 替换命令中的占位符
    auto replace_all = [](std::string s, const std::string& a, const std::string& b){
        size_t pos = 0;
        while ((pos = s.find(a, pos)) != std::string::npos) {
            s.replace(pos, a.length(), b);
            pos += b.length();
        }
        return s;
    };

    std::string compile_command = compile_cmd;
    compile_command = replace_all(compile_command, "{source}", source_path);
    compile_command = replace_all(compile_command, "{output}", binary_path);
    compile_command += " 2> \"" + compile_err_path + "\"";

    int compile_ret = oj::judge::RunShellCommand(compile_command);
    json out;

    if (compile_ret != 0) {
        std::string ce = oj::judge::ReadFile(compile_err_path);
        out["status"] = "compile_error";
        out["compile_error"] = ce;
        out["results"] = json::array();
        std::cout << out.dump() << std::endl;
        return 0;
    }

    out["status"] = "accepted";
    out["compile_error"] = nullptr;
    out["results"] = json::array();

    if (!req.contains("test_cases") || !req["test_cases"].is_array()) {
        out["status"] = "system_error";
        out["error"] = "no test_cases provided";
        std::cout << out.dump() << std::endl;
        return 0;
    }

    int total_time_ms = 0;
    int peak_memory_kb = 0;
    int passed = 0;
    int total = 0;

    for (const auto& tc : req["test_cases"]) {
        total++;
        int tc_id = tc.value("id", 0);
        std::string input_data = tc.value("input", std::string());
        std::string expected = tc.value("expected_output", std::string());

        std::string in_path = work_dir + "/tc_in_" + std::to_string(tc_id) + ".txt";
        std::string out_path = work_dir + "/tc_out_" + std::to_string(tc_id) + ".txt";
        std::string run_err = work_dir + "/run_err_" + std::to_string(tc_id) + ".txt";

        oj::judge::WriteFile(in_path, input_data);

        oj::judge::RunResult rr = oj::judge::RunProgramWithLimits(binary_path, in_path, out_path, run_err, time_limit_ms, memory_limit_kb);
        total_time_ms += rr.time_ms;
        if (rr.memory_kb > peak_memory_kb) peak_memory_kb = rr.memory_kb;

        const size_t MAX_ACTUAL_OUTPUT = 64 * 1024; // 64 KB per test case
        std::string actual_full = oj::judge::ReadFile(out_path);
        bool actual_truncated = false;
        std::string actual = actual_full;
        if (actual_full.size() > MAX_ACTUAL_OUTPUT) {
            actual = actual_full.substr(0, MAX_ACTUAL_OUTPUT);
            actual_truncated = true;
        }

        json r;
        r["test_case_id"] = tc_id;
        r["time_ms"] = rr.time_ms;
        r["memory_kb"] = rr.memory_kb;
        r["actual_output"] = actual;
        if (actual_truncated) r["actual_truncated"] = true;
        r["expected_output"] = expected;

        if (rr.status == "accepted") {
            if (normalize_output(actual) == normalize_output(expected)) {
                r["status"] = "accepted";
                passed++;
            } else {
                r["status"] = "wrong_answer";
                out["status"] = "wrong_answer";
            }
        } else if (rr.status == "time_limit_exceeded") {
            r["status"] = "time_limit_exceeded";
            out["status"] = "time_limit_exceeded";
            r["stderr"] = rr.stderr_txt;
        } else if (rr.status == "memory_limit_exceeded") {
            r["status"] = "memory_limit_exceeded";
            out["status"] = "memory_limit_exceeded";
            r["stderr"] = rr.stderr_txt;
        } else if (rr.status == "runtime_error") {
            r["status"] = "runtime_error";
            out["status"] = "runtime_error";
            r["stderr"] = rr.stderr_txt;
        } else {
            r["status"] = rr.status;
            out["status"] = rr.status;
            r["stderr"] = rr.stderr_txt;
        }

        out["results"].push_back(r);
    }

    out["summary"] = {
        {"total", total},
        {"passed", passed},
        {"total_time_ms", total_time_ms},
        {"peak_memory_kb", peak_memory_kb}
    };

    std::cout << out.dump() << std::endl;
    return 0;
}
