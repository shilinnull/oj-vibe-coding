#pragma once

#include <chrono>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include <signal.h>
#include <sstream>
#include <string>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "utils/logger.h"

namespace oj::judge {

using json = nlohmann::json;

struct RunResult {
	int exit_code{-1};
	std::string status{"runtime_error"};
	int time_ms{0};
	int memory_kb{0};
	std::string stderr_txt;
};

inline std::string ReadFile(const std::string& path) {
	std::ifstream ifs(path);
	if (!ifs) return std::string();
	std::ostringstream ss;
	ss << ifs.rdbuf();
	return ss.str();
}

inline std::string NormalizeOutput(std::string text) {
	while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '\t')) {
		text.pop_back();
	}
	return text;
}

inline void WriteFile(const std::string& path, const std::string& content) {
	std::ofstream ofs(path);
	ofs << content;
}

inline std::string ResolveSandboxPreloadPath() {
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

inline int RunShellCommand(const std::string& cmd) {
	int ret = std::system(cmd.c_str());
	if (ret == -1) return -1;
	return WEXITSTATUS(ret);
}

inline RunResult RunProgramWithLimits(const std::string& binary,
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

inline void ExecuteJudgeRequest(const std::string& in_json, std::string* out_json) {
	json req;
	try {
		req = json::parse(in_json);
	} catch (const std::exception& e) {
		json out = {{"status", "system_error"}, {"error", std::string("JSON parse error: ") + e.what()}};
		*out_json = out.dump();
		return;
	}

	std::string source_code = req.value("source_code", std::string());
	int time_limit_ms = req.value("time_limit_ms", 1000);
	int max_time_limit_ms = 10000;
	const char* env_max = std::getenv("JUDGER_MAX_TIME_MS");
	if (env_max) {
		try {
			int v = std::stoi(std::string(env_max));
			if (v > 0) max_time_limit_ms = v;
		} catch (...) {
		}
	}
	if (time_limit_ms > max_time_limit_ms) time_limit_ms = max_time_limit_ms;
	int memory_limit_kb = req.value("memory_limit_kb", 262144);
	std::string work_dir = req.value("work_dir", std::string("./run/judge"));
	std::string compile_cmd = req.value("compile_cmd", std::string("g++ -O2 -std=c++17 {source} -o {output}"));
	std::string run_cmd = req.value("run_cmd", std::string("{binary}"));

	(void)run_cmd;
	mkdir(work_dir.c_str(), 0755);
	std::string source_path = work_dir + "/user_code.cpp";
	std::string binary_path = work_dir + "/user_bin";
	std::string compile_err_path = work_dir + "/compile_err.txt";
	WriteFile(source_path, source_code);

	auto replace_all = [](std::string s, const std::string& a, const std::string& b) {
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

	int compile_ret = RunShellCommand(compile_command);
	json out;
	if (compile_ret != 0) {
		out["status"] = "compile_error";
		out["compile_error"] = ReadFile(compile_err_path);
		out["results"] = json::array();
		*out_json = out.dump();
		return;
	}

	out["status"] = "accepted";
	out["compile_error"] = nullptr;
	out["results"] = json::array();

	if (!req.contains("test_cases") || !req["test_cases"].is_array()) {
		out["status"] = "system_error";
		out["error"] = "no test_cases provided";
		*out_json = out.dump();
		return;
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

		WriteFile(in_path, input_data);
		RunResult rr = RunProgramWithLimits(binary_path, in_path, out_path, run_err, time_limit_ms, memory_limit_kb);
		total_time_ms += rr.time_ms;
		if (rr.memory_kb > peak_memory_kb) peak_memory_kb = rr.memory_kb;

		const size_t MAX_ACTUAL_OUTPUT = 64 * 1024;
		std::string actual_full = ReadFile(out_path);
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
			if (NormalizeOutput(actual) == NormalizeOutput(expected)) {
				r["status"] = "accepted";
				passed++;
			} else {
				r["status"] = "wrong_answer";
				out["status"] = "wrong_answer";
			}
		} else {
			r["status"] = rr.status;
			out["status"] = rr.status;
			r["stderr"] = rr.stderr_txt;
		}

		out["results"].push_back(r);
	}

	out["summary"] = {{"total", total}, {"passed", passed}, {"total_time_ms", total_time_ms}, {"peak_memory_kb", peak_memory_kb}};
	*out_json = out.dump();
}

}  // namespace oj::judge
