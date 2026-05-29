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

#include "judge/sandbox_runner.h"

namespace oj::judge {

using json = nlohmann::json;

inline std::string NormalizeOutput(std::string text) {
	while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '\t')) {
		text.pop_back();
	}
	return text;
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
