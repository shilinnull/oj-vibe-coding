// judge_manager.h
// 判题调度器：支持本地 judger_cli 判题，也支持运行时注册远端 judge_worker。

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <nlohmann/json.hpp>
#include <queue>
#include <thread>
#include <vector>

#include "judge/judge_worker_balancer.h"
#include "utils/config.h"

namespace oj {
class MySqlPool;
}

using json = nlohmann::json;

struct JudgeJob {
	long submission_id;
	json payload;
};

class JudgeManager {
public:
	explicit JudgeManager(oj::MySqlPool& pool,
						  const oj::JudgeConfig& judge_cfg,
						  const std::vector<oj::JudgeWorkerConfig>& workers = {});
	~JudgeManager();

	bool submit(const JudgeJob& job);
	void RegisterWorker(const oj::JudgeWorkerConfig& worker_cfg);
	void shutdown();
	void ProbeWorkersIfNeeded();

private:
	void worker_thread();
	bool ExecuteJob(const JudgeJob& job, std::string* result_json);
	bool ExecuteRemotely(const JudgeJob& job, std::string* result_json);
	bool SelectWorker(int* id, oj::JudgeWorkerConfig* worker_cfg);
	void ReleaseWorkerLoad(int id);
	void MarkOffline(int id);
	void MarkOnline(int id);
	bool HealthCheck(const oj::JudgeWorkerConfig& worker_cfg);
	bool SendHttpJson(const oj::JudgeWorkerConfig& worker_cfg,
					 const std::string& method,
					 const std::string& path,
					 const std::string& body,
					 std::string* response_body,
					 int* response_status);

	oj::MySqlPool* pool_;
	int max_concurrency_;
	int retry_count_;
	int request_timeout_ms_;
	int health_check_interval_ms_;
	oj::JudgeWorkerBalancer worker_balancer_;
	std::chrono::steady_clock::time_point last_health_probe_;
	std::atomic<bool> remote_mode_{false};
	std::vector<std::thread> worker_threads_;
	std::queue<JudgeJob> queue_;
	std::mutex mu_;
	std::condition_variable cv_;
	std::atomic<bool> running_{true};
};
