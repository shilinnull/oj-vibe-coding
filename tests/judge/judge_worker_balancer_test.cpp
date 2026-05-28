#include <gtest/gtest.h>

#include "judge/judge_worker_balancer.h"

namespace {
	oj::JudgeWorkerConfig MakeWorker(const std::string& host, int port) {
		oj::JudgeWorkerConfig cfg;
		cfg.host = host;
		cfg.port = port;
		cfg.health_path = "/healthz";
		cfg.judge_path = "/judge";
		cfg.timeout_ms = 3000;
		return cfg;
	}

}  // namespace

TEST(JudgeWorkerBalancerTest, PicksLowestLoadWorkersInOrder) {
	oj::JudgeWorkerBalancer balancer;
	balancer.UpsertWorker(MakeWorker("127.0.0.1", 9001));
	balancer.UpsertWorker(MakeWorker("127.0.0.1", 9002));
	balancer.UpsertWorker(MakeWorker("127.0.0.1", 9003));

	int id = -1;
	oj::JudgeWorkerConfig selected;

	ASSERT_TRUE(balancer.SelectWorker(&id, &selected));
	EXPECT_EQ(id, 0);
	EXPECT_EQ(selected.port, 9001);

	ASSERT_TRUE(balancer.SelectWorker(&id, &selected));
	EXPECT_EQ(id, 1);
	EXPECT_EQ(selected.port, 9002);

	ASSERT_TRUE(balancer.SelectWorker(&id, &selected));
	EXPECT_EQ(id, 2);
	EXPECT_EQ(selected.port, 9003);

	auto snapshot = balancer.Snapshot();
	ASSERT_EQ(snapshot.size(), 3u);
	EXPECT_EQ(snapshot[0].load, 1u);
	EXPECT_EQ(snapshot[1].load, 1u);
	EXPECT_EQ(snapshot[2].load, 1u);
}

TEST(JudgeWorkerBalancerTest, SkipsOfflineWorkers) {
	oj::JudgeWorkerBalancer balancer;
	balancer.UpsertWorker(MakeWorker("127.0.0.1", 9001));
	balancer.UpsertWorker(MakeWorker("127.0.0.1", 9002));
	balancer.UpsertWorker(MakeWorker("127.0.0.1", 9003));

	balancer.MarkOffline(0);

	int id = -1;
	oj::JudgeWorkerConfig selected;
	ASSERT_TRUE(balancer.SelectWorker(&id, &selected));
	EXPECT_EQ(id, 1);
	EXPECT_EQ(selected.port, 9002);

	auto snapshot = balancer.Snapshot();
	ASSERT_EQ(snapshot.size(), 3u);
	EXPECT_FALSE(snapshot[0].online);
	EXPECT_TRUE(snapshot[1].online);
	EXPECT_TRUE(snapshot[2].online);
}

TEST(JudgeWorkerBalancerTest, ReleaseWorkerMakesItSelectableAgain) {
	oj::JudgeWorkerBalancer balancer;
	balancer.UpsertWorker(MakeWorker("127.0.0.1", 9001));
	balancer.UpsertWorker(MakeWorker("127.0.0.1", 9002));

	int id = -1;
	oj::JudgeWorkerConfig selected;
	ASSERT_TRUE(balancer.SelectWorker(&id, &selected));
	EXPECT_EQ(id, 0);
	EXPECT_EQ(selected.port, 9001);

	ASSERT_TRUE(balancer.SelectWorker(&id, &selected));
	EXPECT_EQ(id, 1);
	EXPECT_EQ(selected.port, 9002);

	balancer.ReleaseWorkerLoad(0);

	ASSERT_TRUE(balancer.SelectWorker(&id, &selected));
	EXPECT_EQ(id, 0);
	EXPECT_EQ(selected.port, 9001);
}