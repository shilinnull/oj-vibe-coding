// judge_manager.cpp
// 简化的 JudgeManager 实现：维护固定数量的工作线程（max_concurrency），从队列取出任务，调用外部可执行 judger_cli（通过 stdin 传 JSON），并读取其 stdout 结果。
// 该实现为基础版本，错误处理和日志可按需增强。

#include "judge_manager.h"
#include "db/dao/submission_dao.h"
#include "utils/json_helper.h"
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <iostream>
#include <sstream>
#include <cstring>
#include <filesystem>
#include <vector>

JudgeManager::JudgeManager(oj::MySqlPool& pool, int max_concurrency)
	: pool_(&pool), max_concurrency_(max_concurrency), running_(true) {
	for (int i = 0; i < max_concurrency_; ++i) {
		workers_.emplace_back(&JudgeManager::worker_thread, this);
	}
}

JudgeManager::~JudgeManager() {
	shutdown();
}

bool JudgeManager::submit(const JudgeJob& job) {
	if (!running_) return false;
	{
		std::lock_guard<std::mutex> lk(mu_);
		queue_.push(job);
	}
	cv_.notify_one();
	return true;
}

void JudgeManager::shutdown() {
	bool expected = true;
	if (!running_.compare_exchange_strong(expected, false)) return;
	cv_.notify_all();
	for (auto& t : workers_) {
		if (t.joinable()) t.join();
	}
}

static std::string read_fd_all(int fd) {
	std::ostringstream ss;
	char buf[4096];
	ssize_t n;
	while ((n = read(fd, buf, sizeof(buf))) > 0) {
		ss.write(buf, n);
	}
	return ss.str();
}

static std::string resolve_judger_cli_path() {
	for (const char* candidate : {
				"./run/judger_cli",
				"../run/judger_cli",
				"../../run/judger_cli",
				"run/judger_cli",
		}) {
		std::error_code ec;
		if (std::filesystem::exists(candidate, ec)) {
			return std::filesystem::path(candidate).lexically_normal().string();
		}
	}
	return "./run/judger_cli";
}

void JudgeManager::worker_thread() {
	while (true) {
		JudgeJob job;
		{
			std::unique_lock<std::mutex> lk(mu_);
			cv_.wait(lk, [this]{ return !queue_.empty() || !running_; });
			if (!running_ && queue_.empty()) return;
			job = queue_.front();
			queue_.pop();
		}

		// 为每个任务 fork 出进程，执行 judger_cli，可通过 PATH 或可执行相对路径找到
		int inpipe[2];
		int outpipe[2];
		if (pipe(inpipe) != 0 || pipe(outpipe) != 0) {
			std::cerr << "JudgeManager: 创建管道失败\n";
			continue;
		}

		pid_t pid = fork();
		if (pid < 0) {
			std::cerr << "JudgeManager: fork 失败\n";
			close(inpipe[0]); close(inpipe[1]); close(outpipe[0]); close(outpipe[1]);
			continue;
		}

		if (pid == 0) {
			// 子进程：把 stdin/stdout 重定向到管道，然后 exec judger_cli
			dup2(inpipe[0], STDIN_FILENO);
			dup2(outpipe[1], STDOUT_FILENO);
			// 关闭无用 fd
			close(inpipe[0]); close(inpipe[1]); close(outpipe[0]); close(outpipe[1]);

			// exec
			const std::string exe = resolve_judger_cli_path();
			execlp(exe.c_str(), exe.c_str(), (char*)NULL);
			// 若 exec 失败，退出
			std::cerr << "JudgeManager: exec judger_cli 失败: " << strerror(errno) << "\n";
			_exit(127);
		}

		// 父进程：关闭不需要的端，写入 JSON 到子进程 stdin，读取 stdout
		close(inpipe[0]); close(outpipe[1]);

		// 将 job.payload 序列化并写入子进程 stdin
		std::string req = job.payload.dump();
		ssize_t w = write(inpipe[1], req.data(), req.size());
		(void)w;
		close(inpipe[1]);

		// 读取子进程输出
		std::string out = read_fd_all(outpipe[0]);
		close(outpipe[0]);

		int status = 0;
		waitpid(pid, &status, 0);

		std::string final_status = "system_error";
		std::string stored_result;
		int time_ms = 0;
		int memory_kb = 0;
		if (auto parsed = oj::TryParseJson(out, nullptr); parsed.has_value()) {
			stored_result = out;
			const auto& result = *parsed;
			if (result.contains("status") && result["status"].is_string()) {
				final_status = result["status"].get<std::string>();
			}
			if (result.contains("summary") && result["summary"].is_object()) {
				time_ms = result["summary"].value("total_time_ms", 0);
				memory_kb = result["summary"].value("peak_memory_kb", 0);
			}
		} else if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
			final_status = "system_error";
			stored_result = json{{"status", "SYSTEM_ERROR"}, {"error", out.empty() ? "judge failed" : out}}.dump();
		} else {
			stored_result = json{{"status", "SYSTEM_ERROR"}, {"error", out.empty() ? "empty judge output" : out}}.dump();
		}
		if (stored_result.empty()) {
			stored_result = json{{"status", "SYSTEM_ERROR"}, {"error", out.empty() ? "empty judge output" : out}}.dump();
		}

		try {
			oj::SubmissionDao dao(*pool_);
			dao.UpdateResult(job.submission_id, final_status, stored_result, time_ms, memory_kb);
		} catch (const std::exception& e) {
			std::cerr << "JudgeManager: 回写 submission_id=" << job.submission_id
					  << " 失败: " << e.what() << "\n";
		}

		std::cerr << "JudgeManager: submission_id=" << job.submission_id << " finished, result=" << out << "\n";
	}
}

