#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "model/language.h"
#include "model/problem.h"
#include "model/submission.h"
#include "model/test_case.h"
#include "model/user.h"

TEST(ModelJsonTest, UserRoundTrip) {
	oj::User user;
	user.id = 7;
	user.username = "alice";
	user.password = "hash";
	user.email = "alice@example.com";
	user.role = "admin";
	user.status = "active";
	user.created_at = "2026-05-23 10:00:00";
	user.updated_at = "2026-05-23 10:00:01";

	nlohmann::json json = user;
	auto restored = json.get<oj::User>();

	EXPECT_EQ(restored.id, user.id);
	EXPECT_EQ(restored.username, user.username);
	EXPECT_EQ(restored.password, user.password);
	EXPECT_EQ(restored.email, user.email);
	EXPECT_EQ(restored.role, user.role);
	EXPECT_EQ(restored.status, user.status);
	EXPECT_EQ(restored.created_at, user.created_at);
	EXPECT_EQ(restored.updated_at, user.updated_at);
}

TEST(ModelJsonTest, ProblemRoundTrip) {
	oj::Problem problem;
	problem.id = 11;
	problem.title = "Two Sum";
	problem.description = "Add numbers";
	problem.difficulty = "easy";
	problem.time_limit_ms = 500;
	problem.memory_limit_kb = 65536;
	problem.status = "published";
	problem.created_by = 42;
	problem.created_at = "2026-05-23 10:00:00";
	problem.updated_at = "2026-05-23 10:00:01";

	nlohmann::json json = problem;
	auto restored = json.get<oj::Problem>();

	EXPECT_EQ(restored.title, problem.title);
	EXPECT_EQ(restored.time_limit_ms, problem.time_limit_ms);
	EXPECT_EQ(restored.memory_limit_kb, problem.memory_limit_kb);
	EXPECT_EQ(restored.status, problem.status);
}

TEST(ModelJsonTest, SubmissionRoundTrip) {
	oj::Submission submission;
	submission.id = 19;
	submission.user_id = 7;
	submission.problem_id = 11;
	submission.language_id = 1;
	submission.source_code = "int main(){}";
	submission.mode = "submit";
	submission.status = "accepted";
	submission.result_json = R"({"status":"ACCEPTED"})";
	submission.time_ms = 12;
	submission.memory_kb = 4096;
	submission.created_at = "2026-05-23 10:00:02";

	nlohmann::json json = submission;
	auto restored = json.get<oj::Submission>();

	EXPECT_EQ(restored.user_id, submission.user_id);
	EXPECT_EQ(restored.problem_id, submission.problem_id);
	EXPECT_EQ(restored.language_id, submission.language_id);
	EXPECT_EQ(restored.result_json, submission.result_json);
}

TEST(ModelJsonTest, TestCaseRoundTrip) {
	oj::TestCase test_case;
	test_case.id = 3;
	test_case.problem_id = 11;
	test_case.is_sample = true;
	test_case.input = "1 2";
	test_case.output = "3";
	test_case.sort_order = 1;
	test_case.created_at = "2026-05-23 10:00:03";

	nlohmann::json json = test_case;
	auto restored = json.get<oj::TestCase>();

	EXPECT_TRUE(restored.is_sample);
	EXPECT_EQ(restored.input, test_case.input);
	EXPECT_EQ(restored.output, test_case.output);
}

TEST(ModelJsonTest, LanguageRoundTrip) {
	oj::Language language;
	language.id = 1;
	language.name = "C++17";
	language.extension = "cpp";
	language.compile_cmd = "g++ -std=c++17";
	language.run_cmd = "./a.out";
	language.enabled = true;
	language.created_at = "2026-05-23 10:00:04";

	nlohmann::json json = language;
	auto restored = json.get<oj::Language>();

	EXPECT_EQ(restored.name, language.name);
	EXPECT_EQ(restored.extension, language.extension);
	EXPECT_EQ(restored.compile_cmd, language.compile_cmd);
	EXPECT_EQ(restored.run_cmd, language.run_cmd);
	EXPECT_TRUE(restored.enabled);
}