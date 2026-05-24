// judger_cli.cpp
// 负责：读取 JSON 输入（stdin），编译源码，针对每个测试用例运行并进行严格输出比对，最终把结果以 JSON 输出到 stdout。
// 注意：该实现为基础版本，不包含 nsjail/cgroups/seccomp 等沙箱机制，仅用于本地验证判题流程逻辑。

#include <nlohmann/json.hpp>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <chrono>

using json = nlohmann::json;

static std::string read_stdin_all() {
	std::ostringstream ss;
	ss << std::cin.rdbuf();
	return ss.str();
}

static int run_shell_command(const std::string& cmd) {
	// 简单调用系统命令并返回退出码
	int ret = std::system(cmd.c_str());
	if (ret == -1) return -1;
	return WEXITSTATUS(ret);
}

static std::string read_file(const std::string& path) {
	std::ifstream ifs(path);
	if (!ifs) return std::string();
	std::ostringstream ss;
	ss << ifs.rdbuf();
	return ss.str();
}

static void write_file(const std::string& path, const std::string& content) {
	std::ofstream ofs(path);
	ofs << content;
}

int main() {
	// 读取 stdin 的 JSON 参数
	std::string input = read_stdin_all();
	if (input.empty()) {
		std::cerr << "judger_cli: 没有从 stdin 读取到输入 JSON\n";
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
	int memory_limit_kb = req.value("memory_limit_kb", 262144);
	std::string work_dir = req.value("work_dir", std::string("./run/judge"));
	std::string compile_cmd = req.value("compile_cmd", std::string("g++ -O2 -std=c++17 {source} -o {output}"));
	std::string run_cmd = req.value("run_cmd", std::string("{binary}"));

	// 准备 work_dir
	mkdir(work_dir.c_str(), 0755);

	std::string source_path = work_dir + "/user_code.cpp";
	std::string binary_path = work_dir + "/user_bin";
	std::string compile_err_path = work_dir + "/compile_err.txt";

	write_file(source_path, source_code);

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
	// 将 stderr 重定向到文件
	compile_command += " 2> \"" + compile_err_path + "\"";

	int compile_ret = run_shell_command(compile_command);
	json out;

	if (compile_ret != 0) {
		// 读取编译错误并返回 CE
		std::string ce = read_file(compile_err_path);
		out["status"] = "compile_error";
		out["compile_error"] = ce;
		out["results"] = json::array();
		std::cout << out.dump() << std::endl;
		return 0;
	}

	// 编译成功，遍历测试用例
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
	int peak_memory_kb = 0; // 占位，未测量
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

		write_file(in_path, input_data);

		// 构造运行命令
		std::string run_command = run_cmd;
		run_command = replace_all(run_command, "{binary}", binary_path);
		// 将 stdin 重定向，stdout 重定向到文件，stderr 重定向到 err 文件
		run_command += " < \"" + in_path + "\" > \"" + out_path + "\" 2> \"" + run_err + "\"";

		// 计时
		auto t0 = std::chrono::steady_clock::now();
		int run_ret = run_shell_command(run_command);
		auto t1 = std::chrono::steady_clock::now();
		int elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
		total_time_ms += elapsed_ms;

		std::string actual = read_file(out_path);
		std::string stderr_txt = read_file(run_err);

		json r;
		r["test_case_id"] = tc_id;
		r["time_ms"] = elapsed_ms;
		r["memory_kb"] = 0; // 未测量
		r["actual_output"] = actual;
		r["expected_output"] = expected;

		if (run_ret != 0) {
			r["status"] = "runtime_error";
			r["stderr"] = stderr_txt;
			out["status"] = "runtime_error";
		} else if (actual == expected) {
			r["status"] = "accepted";
			passed++;
		} else {
			r["status"] = "wrong_answer";
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
