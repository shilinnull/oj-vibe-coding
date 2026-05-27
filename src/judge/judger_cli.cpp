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

using json = nlohmann::json;

static std::string read_stdin_all() {
	std::ostringstream ss;
	ss << std::cin.rdbuf();
	return ss.str();
}

// forward declare resolve function (defined later)
static std::string resolve_sandbox_preload_path();

static int run_shell_command(const std::string& cmd) {
	// 简单调用系统命令并返回退出码
	int ret = std::system(cmd.c_str());
	if (ret == -1) return -1;
	return WEXITSTATUS(ret);
}

struct RunResult {
	int exit_code; // child exit code or -1
	std::string status; // accepted/runtime_error/timeout/memory_limit
	int time_ms;
	int memory_kb;
	std::string stderr_txt;
};

static RunResult run_program_with_limits(const std::string& binary,
										 const std::string& in_path,
										 const std::string& out_path,
										 const std::string& err_path,
										 int time_limit_ms,
										 int memory_limit_kb) {
	RunResult res;
	res.exit_code = -1;
	res.status = "runtime_error";
	res.time_ms = 0;
	res.memory_kb = 0;
	res.stderr_txt = std::string();

	pid_t pid = fork();
	if (pid == -1) {
		res.status = "system_error";
		return res;
	}

	if (pid == 0) {
		// child
		// set resource limits
		struct rlimit rl;
		// CPU time in seconds (ceil)
		rl.rlim_cur = rl.rlim_max = (time_limit_ms + 999) / 1000;
		setrlimit(RLIMIT_CPU, &rl);

		// Address space limit (bytes)
		rlim_t as_limit = (rlim_t)memory_limit_kb * 1024ULL;
		rl.rlim_cur = rl.rlim_max = as_limit;
		setrlimit(RLIMIT_AS, &rl);

		// Limit number of processes
		rl.rlim_cur = rl.rlim_max = 16;
		setrlimit(RLIMIT_NPROC, &rl);

		const std::string preload = resolve_sandbox_preload_path();
		if (!preload.empty()) {
			setenv("LD_PRELOAD", preload.c_str(), 1);
		}

		// Redirect stdin/stdout/stderr
		int in_fd = open(in_path.c_str(), O_RDONLY);
		int out_fd = open(out_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
		int err_fd = open(err_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
		if (in_fd >= 0) dup2(in_fd, STDIN_FILENO);
		if (out_fd >= 0) dup2(out_fd, STDOUT_FILENO);
		if (err_fd >= 0) dup2(err_fd, STDERR_FILENO);

		// drop privileges? (not implemented here)

		// exec binary
		execl(binary.c_str(), binary.c_str(), (char*)NULL);
		// if exec fails
		_exit(127);
	}

	// parent
	auto t0 = std::chrono::steady_clock::now();
	int status = 0;
	struct rusage usage;
	bool killed_for_timeout = false;
	while (true) {
		pid_t w = wait4(pid, &status, WNOHANG, &usage);
		auto t1 = std::chrono::steady_clock::now();
		int elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
		if (w == 0) {
			// still running
			if (elapsed_ms > time_limit_ms + 200) {
				// kill it
				kill(pid, SIGKILL);
				killed_for_timeout = true;
				// wait for it to die
				wait4(pid, &status, 0, &usage);
				res.time_ms = elapsed_ms;
				break;
			}
			// sleep a bit
			usleep(1000 * 5);
			continue;
		} else if (w == pid) {
			res.time_ms = elapsed_ms;
			break;
		} else {
			// unexpected
			res.time_ms = elapsed_ms;
			break;
		}
	}

	// read stderr
	res.stderr_txt = "";
	std::string se = [] (const std::string& p)->std::string {
		std::ifstream ifs(p);
		if (!ifs) return std::string();
		std::ostringstream ss;
		ss << ifs.rdbuf();
		return ss.str();
	}(err_path);
	res.stderr_txt = se;

	// ru_maxrss is in kilobytes on Linux
	res.memory_kb = static_cast<int>(usage.ru_maxrss);

	if (killed_for_timeout) {
		res.status = "time_limit_exceeded";
		res.exit_code = -1;
		return res;
	}

	if (res.memory_kb > memory_limit_kb) {
		res.status = "memory_limit_exceeded";
		res.exit_code = -1;
		return res;
	}

	if (WIFSIGNALED(status)) {
		int sig = WTERMSIG(status);
		if (sig == SIGKILL || sig == SIGXCPU) {
			res.status = "time_limit_exceeded";
		} else {
			res.status = "runtime_error";
		}
		res.exit_code = -1;
		return res;
	}

	if (WIFEXITED(status)) {
		int ec = WEXITSTATUS(status);
		res.exit_code = ec;
		if (ec != 0) {
			res.status = "runtime_error";
		} else {
			res.status = "accepted";
		}
	}

	return res;
}

static std::string read_file(const std::string& path) {
	std::ifstream ifs(path);
	if (!ifs) return std::string();
	std::ostringstream ss;
	ss << ifs.rdbuf();
	return ss.str();
}

static std::string normalize_output(std::string text) {
	while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '\t')) {
		text.pop_back();
	}
	return text;
}

static void write_file(const std::string& path, const std::string& content) {
	std::ofstream ofs(path);
	ofs << content;
}

static std::string resolve_sandbox_preload_path() {
	const std::filesystem::path cwd = std::filesystem::current_path();
	const std::filesystem::path candidates[] = {
		cwd / "run" / "libjudge_sandbox_preload.so",
		cwd / "run" / "libsandbox_preload.so",
		cwd / "build" / "run" / "libjudge_sandbox_preload.so",
		cwd / "build" / "run" / "libsandbox_preload.so",
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
	// Clamp to a maximum allowed time to avoid runaway long-running submissions.
	// Can be overridden by environment variable JUDGER_MAX_TIME_MS (in milliseconds).
	int max_time_limit_ms = 10000; // default 10s
	const char* env_max = std::getenv("JUDGER_MAX_TIME_MS");
	if (env_max) {
		try {
			int v = std::stoi(std::string(env_max));
			if (v > 0) max_time_limit_ms = v;
		} catch (...) {
			// ignore parse errors
		}
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

		// 使用受限运行器执行二进制
		RunResult rr = run_program_with_limits(binary_path, in_path, out_path, run_err, time_limit_ms, memory_limit_kb);
		total_time_ms += rr.time_ms;

		// limit actual output size to avoid producing excessively large JSON
		const size_t MAX_ACTUAL_OUTPUT = 64 * 1024; // 64 KB per test case
		std::string actual_full = read_file(out_path);
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
