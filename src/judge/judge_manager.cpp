// judge_manager.cpp
// 简化的 JudgeManager 实现：维护固定数量的工作线程（max_concurrency），从队列取出任务，调用外部可执行 judger_cli（通过 stdin 传 JSON），并读取其 stdout 结果。
// 该实现为基础版本，错误处理和日志可按需增强。

#include "judge_manager.h"
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <iostream>
#include <sstream>
#include <vector>

JudgeManager::JudgeManager(int max_concurrency)
	: max_concurrency_(max_concurrency), running_(true) {
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
			const char* exe = "./run/judger_cli"; // 运行目录可调整为构建产物位置
			execlp(exe, exe, (char*)NULL);
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

		// 简单打印结果，实际应写回数据库或通过回调上报
		std::cerr << "JudgeManager: submission_id=" << job.submission_id << " finished, result=" << out << "\n";
	}
}

