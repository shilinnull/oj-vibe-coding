#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <mutex>
#include <vector>

#include "utils/config.h"

namespace oj {

class JudgeWorkerBalancer {
public:
	struct WorkerNode {
		JudgeWorkerConfig cfg;
		std::uint64_t load{0};
		bool online{true};
	};

	int UpsertWorker(const JudgeWorkerConfig& cfg, bool* refreshed = nullptr) {
		std::lock_guard<std::mutex> lk(mu_);
		for (std::size_t i = 0; i < nodes_.size(); ++i) {
			if (SameEndpoint(nodes_[i].cfg, cfg)) {
				nodes_[i].cfg = cfg;
				nodes_[i].load = 0;
				nodes_[i].online = true;
				if (refreshed) *refreshed = true;
				return static_cast<int>(i);
			}
		}
		nodes_.push_back(WorkerNode{cfg, 0, true});
		if (refreshed) *refreshed = false;
		return static_cast<int>(nodes_.size() - 1);
	}

	bool SelectWorker(int* id, JudgeWorkerConfig* cfg, std::uint64_t* load_before = nullptr) {
		std::lock_guard<std::mutex> lk(mu_);
		if (nodes_.empty()) return false;
		int best = -1;
		std::uint64_t best_load = std::numeric_limits<std::uint64_t>::max();
		std::size_t n = nodes_.size();
		for (std::size_t offset = 0; offset < n; ++offset) {
			std::size_t i = (next_worker_ + offset) % n;
			if (!nodes_[i].online) continue;
			if (nodes_[i].load < best_load) {
				best_load = nodes_[i].load;
				best = static_cast<int>(i);
			}
		}
		if (best == -1) return false;
		nodes_[best].load += 1;
		next_worker_ = (static_cast<std::size_t>(best) + 1) % n;
		if (id) *id = best;
		if (cfg) *cfg = nodes_[best].cfg;
		if (load_before) *load_before = best_load;
		return true;
	}

	void ReleaseWorkerLoad(int id) {
		std::lock_guard<std::mutex> lk(mu_);
		if (id < 0 || static_cast<std::size_t>(id) >= nodes_.size()) return;
		if (nodes_[id].load > 0) {
			nodes_[id].load -= 1;
		}
	}

	void MarkOffline(int id) {
		std::lock_guard<std::mutex> lk(mu_);
		if (id < 0 || static_cast<std::size_t>(id) >= nodes_.size()) return;
		nodes_[id].online = false;
		nodes_[id].load = 0;
	}

	void MarkOnline(int id) {
		std::lock_guard<std::mutex> lk(mu_);
		if (id < 0 || static_cast<std::size_t>(id) >= nodes_.size()) return;
		nodes_[id].online = true;
	}

	bool GetWorker(int id, WorkerNode* out) const {
		std::lock_guard<std::mutex> lk(mu_);
		if (id < 0 || static_cast<std::size_t>(id) >= nodes_.size() || out == nullptr) return false;
		*out = nodes_[id];
		return true;
	}

	std::vector<WorkerNode> Snapshot() const {
		std::lock_guard<std::mutex> lk(mu_);
		return nodes_;
	}

	std::size_t Size() const {
		std::lock_guard<std::mutex> lk(mu_);
		return nodes_.size();
	}

	std::size_t OnlineCount() const {
		std::lock_guard<std::mutex> lk(mu_);
		return static_cast<std::size_t>(std::count_if(nodes_.begin(), nodes_.end(), [](const WorkerNode& node) {
			return node.online;
		}));
	}

private:
	static bool SameEndpoint(const JudgeWorkerConfig& lhs, const JudgeWorkerConfig& rhs) {
		return lhs.host == rhs.host && lhs.port == rhs.port;
	}

	mutable std::mutex mu_;
	std::vector<WorkerNode> nodes_;
	std::size_t next_worker_{0};
};

}  // namespace oj