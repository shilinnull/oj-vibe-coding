// judge_manager.h
// 提供 JudgeManager：任务队列 + 并发控制（默认并发上限 4）。
// 简化实现：接受 JSON 任务（包含 submission_id 与参数），通过 fork+exec 调用 judger_cli，可扩展为使用线程/进程池。

#pragma once

#include <nlohmann/json.hpp>
#include <functional>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <vector>

namespace oj {
class MySqlPool;
}

using json = nlohmann::json;

struct JudgeJob {
	long submission_id;
	json payload; // 传递给 judger_cli 的 JSON
};

class JudgeManager {
public:
	explicit JudgeManager(oj::MySqlPool& pool, int max_concurrency = 4);
	~JudgeManager();

	// 提交任务到队列，返回任务是否成功入队
	bool submit(const JudgeJob& job);

	// 停止管理器并等待工作线程结束
	void shutdown();

private:
	void worker_thread();

	oj::MySqlPool* pool_;
	int max_concurrency_;
	std::vector<std::thread> workers_;
	std::queue<JudgeJob> queue_;
	std::mutex mu_;
	std::condition_variable cv_;
	std::atomic<bool> running_;
};

