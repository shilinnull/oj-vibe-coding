#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <mysql/mysql.h>
#include <nlohmann/json.hpp>

#include "db/dao/language_dao.h"
#include "db/dao/problem_dao.h"
#include "db/dao/submission_dao.h"
#include "db/dao/test_case_dao.h"
#include "db/dao/user_dao.h"

namespace {

constexpr const char* kMysqlHost = "127.0.0.1";
constexpr int kMysqlPort = 3306;
constexpr const char* kMysqlUser = "shilin";
constexpr const char* kMysqlPassword = "123456";
constexpr const char* kMysqlDatabase = "oj";

std::string MakeUniqueSuffix() {
	auto now = std::chrono::steady_clock::now().time_since_epoch().count();
	return std::to_string(now);
}

class RawMysqlConnection {
 public:
	explicit RawMysqlConnection(const char* database = nullptr) {
		conn_ = mysql_init(nullptr);
		if (conn_ == nullptr) {
			throw std::runtime_error("mysql_init failed");
		}

		unsigned int timeout_sec = 3;
		mysql_options(conn_, MYSQL_OPT_CONNECT_TIMEOUT, &timeout_sec);

		if (mysql_real_connect(conn_,
										kMysqlHost,
										kMysqlUser,
										kMysqlPassword,
										database,
										kMysqlPort,
										nullptr,
										0) == nullptr) {
			std::string err = mysql_error(conn_);
			mysql_close(conn_);
			conn_ = nullptr;
			throw std::runtime_error("mysql_real_connect failed: " + err);
		}

		mysql_set_character_set(conn_, "utf8mb4");
	}

	RawMysqlConnection(const RawMysqlConnection&) = delete;
	RawMysqlConnection& operator=(const RawMysqlConnection&) = delete;

	~RawMysqlConnection() {
		if (conn_ != nullptr) {
			mysql_close(conn_);
		}
	}

	MYSQL* get() const { return conn_; }

 private:
	MYSQL* conn_{nullptr};
};

void ExecSql(MYSQL* conn, const std::string& sql) {
	if (mysql_query(conn, sql.c_str()) != 0) {
		throw std::runtime_error(std::string("mysql_query failed: ") + mysql_error(conn));
	}
	if (MYSQL_RES* result = mysql_store_result(conn)) {
		mysql_free_result(result);
	}
}

void BootstrapSchema() {
	RawMysqlConnection server(nullptr);
	ExecSql(server.get(), "CREATE DATABASE IF NOT EXISTS shilin DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci");
	if (mysql_select_db(server.get(), kMysqlDatabase) != 0) {
		throw std::runtime_error(std::string("mysql_select_db failed: ") + mysql_error(server.get()));
	}

	ExecSql(server.get(), R"SQL(
CREATE TABLE IF NOT EXISTS users (
	id          BIGINT AUTO_INCREMENT PRIMARY KEY,
	username    VARCHAR(64)  NOT NULL UNIQUE,
	password    VARCHAR(256) NOT NULL,
	email       VARCHAR(128) NOT NULL DEFAULT '',
	role        ENUM('student', 'admin') DEFAULT 'student',
	status      ENUM('active', 'banned') DEFAULT 'active',
	created_at  DATETIME DEFAULT CURRENT_TIMESTAMP,
	updated_at  DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB
)SQL");

	ExecSql(server.get(), R"SQL(
CREATE TABLE IF NOT EXISTS languages (
	id          INT AUTO_INCREMENT PRIMARY KEY,
	name        VARCHAR(32)  NOT NULL,
	extension   VARCHAR(8)   NOT NULL,
	compile_cmd VARCHAR(512) NOT NULL,
	run_cmd     VARCHAR(512) NOT NULL,
	enabled     TINYINT(1) DEFAULT 1,
	created_at  DATETIME DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB
)SQL");

	ExecSql(server.get(), R"SQL(
CREATE TABLE IF NOT EXISTS problems (
	id              BIGINT AUTO_INCREMENT PRIMARY KEY,
	title           VARCHAR(256) NOT NULL,
	description     TEXT         NOT NULL,
	difficulty      ENUM('easy', 'medium', 'hard') DEFAULT 'medium',
	time_limit_ms   INT          NOT NULL DEFAULT 1000,
	memory_limit_kb INT          NOT NULL DEFAULT 262144,
	status          ENUM('draft', 'published', 'archived') DEFAULT 'draft',
	created_by      BIGINT,
	created_at      DATETIME DEFAULT CURRENT_TIMESTAMP,
	updated_at      DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
	FOREIGN KEY (created_by) REFERENCES users(id)
) ENGINE=InnoDB
)SQL");

	ExecSql(server.get(), R"SQL(
CREATE TABLE IF NOT EXISTS test_cases (
	id          BIGINT AUTO_INCREMENT PRIMARY KEY,
	problem_id  BIGINT NOT NULL,
	is_sample   TINYINT(1) DEFAULT 0,
	input       TEXT   NOT NULL,
	output      TEXT   NOT NULL,
	sort_order  INT DEFAULT 0,
	created_at  DATETIME DEFAULT CURRENT_TIMESTAMP,
	FOREIGN KEY (problem_id) REFERENCES problems(id) ON DELETE CASCADE
) ENGINE=InnoDB
)SQL");

	ExecSql(server.get(), R"SQL(
CREATE TABLE IF NOT EXISTS submissions (
	id          BIGINT AUTO_INCREMENT PRIMARY KEY,
	user_id     BIGINT NOT NULL,
	problem_id  BIGINT NOT NULL,
	language_id INT    NOT NULL,
	source_code TEXT   NOT NULL,
	mode        ENUM('run', 'submit') DEFAULT 'submit',
	status      ENUM('pending', 'running', 'accepted', 'wrong_answer',
						 'time_limit_exceeded', 'memory_limit_exceeded',
						 'runtime_error', 'compile_error', 'system_error')
						 DEFAULT 'pending',
	result_json JSON,
	time_ms     INT DEFAULT 0,
	memory_kb   INT DEFAULT 0,
	created_at  DATETIME DEFAULT CURRENT_TIMESTAMP,
	FOREIGN KEY (user_id)     REFERENCES users(id),
	FOREIGN KEY (problem_id)  REFERENCES problems(id),
	FOREIGN KEY (language_id) REFERENCES languages(id)
) ENGINE=InnoDB
)SQL");

	ExecSql(server.get(), "INSERT IGNORE INTO languages(id, name, extension, compile_cmd, run_cmd, enabled) VALUES (1, 'C++17', 'cpp', 'g++ -O2 -std=c++17 {source} -o {output}', '{binary}', 1)");
	ExecSql(server.get(), "INSERT IGNORE INTO languages(id, name, extension, compile_cmd, run_cmd, enabled) VALUES (2, 'C11', 'c', 'gcc -O2 -std=c11 {source} -o {output}', '{binary}', 1)");
}

oj::MysqlConfig MakeMysqlConfig() {
	oj::MysqlConfig cfg;
	cfg.host = kMysqlHost;
	cfg.port = kMysqlPort;
	cfg.user = kMysqlUser;
	cfg.password = kMysqlPassword;
	cfg.database = kMysqlDatabase;
	cfg.pool.max_connections = 4;
	cfg.pool.connect_timeout_ms = 3000;
	return cfg;
}

class DbCleanup {
 public:
	explicit DbCleanup(oj::MySqlPool& pool) : pool_(pool) {}

	~DbCleanup() {
		for (auto it = actions_.rbegin(); it != actions_.rend(); ++it) {
			try {
				(*it)();
			} catch (...) {
			}
		}
	}

	void AddDeleteById(std::string table, std::int64_t id) {
		actions_.push_back([this, table = std::move(table), id]() {
			Execute("DELETE FROM " + table + " WHERE id=" + std::to_string(id));
		});
	}

 private:
	void Execute(const std::string& sql) {
		auto conn = pool_.Acquire();
		ExecSql(conn.get(), sql);
	}

	oj::MySqlPool& pool_;
	std::vector<std::function<void()>> actions_;
};

class DbIntegrationTest : public ::testing::Test {
 protected:
	static void SetUpTestSuite() {
		BootstrapSchema();
		pool_ = std::make_unique<oj::MySqlPool>(MakeMysqlConfig());
	}

	static void TearDownTestSuite() {
		pool_.reset();
	}

	static oj::MySqlPool& Pool() {
		return *pool_;
	}

	static std::unique_ptr<oj::MySqlPool> pool_;
};

std::unique_ptr<oj::MySqlPool> DbIntegrationTest::pool_;

bool ContainsId(const std::vector<std::int64_t>& ids, std::int64_t id) {
	return std::find(ids.begin(), ids.end(), id) != ids.end();
}

}  // namespace

TEST_F(DbIntegrationTest, MySqlPoolAcquiresConnection) {
	auto conn = Pool().Acquire();
	ASSERT_TRUE(conn);
	ASSERT_NE(conn.get(), nullptr);
	EXPECT_NE(std::string(mysql_get_server_info(conn.get())), std::string());
}

TEST_F(DbIntegrationTest, UserDaoRoundTrip) {
	DbCleanup cleanup(Pool());
	oj::UserDao user_dao(Pool());

	const std::string suffix = MakeUniqueSuffix();
	const std::string username = "db_user_" + suffix;
	const std::string password_hash = "hash_" + suffix;
	const std::string email = username + "@example.com";

	const std::int64_t user_id = user_dao.CreateUser(username, password_hash, email);
	cleanup.AddDeleteById("users", user_id);

	auto by_username = user_dao.GetByUsername(username);
	ASSERT_TRUE(by_username.has_value());
	EXPECT_EQ(by_username->id, user_id);
	EXPECT_EQ(by_username->username, username);
	EXPECT_EQ(by_username->password, password_hash);
	EXPECT_EQ(by_username->email, email);
	EXPECT_EQ(by_username->role, "student");
	EXPECT_EQ(by_username->status, "active");

	auto by_id = user_dao.GetById(user_id);
	ASSERT_TRUE(by_id.has_value());
	EXPECT_EQ(by_id->username, username);

	EXPECT_TRUE(user_dao.UpdateStatus(user_id, "banned"));
	auto updated = user_dao.GetById(user_id);
	ASSERT_TRUE(updated.has_value());
	EXPECT_EQ(updated->status, "banned");
}

TEST_F(DbIntegrationTest, LanguageDaoRoundTrip) {
	DbCleanup cleanup(Pool());
	oj::LanguageDao language_dao(Pool());

	oj::Language language;
	language.name = "C++20";
	language.extension = "cpp";
	language.compile_cmd = "g++ -O2 -std=c++20 {source} -o {output}";
	language.run_cmd = "{binary}";
	language.enabled = true;

	const int language_id = language_dao.Create(language);
	cleanup.AddDeleteById("languages", language_id);

	auto by_id = language_dao.GetById(language_id);
	ASSERT_TRUE(by_id.has_value());
	EXPECT_EQ(by_id->name, language.name);
	EXPECT_EQ(by_id->extension, language.extension);
	EXPECT_TRUE(by_id->enabled);

	auto enabled_languages = language_dao.ListAll(true);
	std::vector<std::int64_t> ids;
	for (const auto& item : enabled_languages) {
		ids.push_back(item.id);
	}
	EXPECT_TRUE(ContainsId(ids, language_id));

	language.enabled = false;
	EXPECT_TRUE(language_dao.Update(language_id, language));
	auto after_update = language_dao.GetById(language_id);
	ASSERT_TRUE(after_update.has_value());
	EXPECT_FALSE(after_update->enabled);
	EXPECT_TRUE(language_dao.SetEnabled(language_id, true));
	auto reenabled = language_dao.GetById(language_id);
	ASSERT_TRUE(reenabled.has_value());
	EXPECT_TRUE(reenabled->enabled);
}

TEST_F(DbIntegrationTest, ProblemAndTestCaseDaoRoundTrip) {
	DbCleanup cleanup(Pool());
	oj::UserDao user_dao(Pool());
	oj::ProblemDao problem_dao(Pool());
	oj::TestCaseDao test_case_dao(Pool());

	const std::string suffix = MakeUniqueSuffix();
	const std::int64_t user_id = user_dao.CreateUser("db_problem_user_" + suffix, "hash_" + suffix, "problem@example.com");
	cleanup.AddDeleteById("users", user_id);

	oj::Problem problem;
	problem.title = "DAO problem " + suffix;
	problem.description = "Simple add problem";
	problem.difficulty = "easy";
	problem.time_limit_ms = 800;
	problem.memory_limit_kb = 65536;
	problem.status = "published";
	problem.created_by = user_id;

	const std::int64_t problem_id = problem_dao.Create(problem);
	cleanup.AddDeleteById("problems", problem_id);

	oj::TestCase sample;
	sample.problem_id = problem_id;
	sample.is_sample = true;
	sample.input = "1 2";
	sample.output = "3";
	sample.sort_order = 1;
	const std::int64_t sample_id = test_case_dao.Add(sample);
	cleanup.AddDeleteById("test_cases", sample_id);

	oj::TestCase hidden;
	hidden.problem_id = problem_id;
	hidden.is_sample = false;
	hidden.input = "2 3";
	hidden.output = "5";
	hidden.sort_order = 2;
	const std::int64_t hidden_id = test_case_dao.Add(hidden);
	cleanup.AddDeleteById("test_cases", hidden_id);

	auto fetched = problem_dao.GetById(problem_id);
	ASSERT_TRUE(fetched.has_value());
	EXPECT_EQ(fetched->title, problem.title);
	EXPECT_EQ(fetched->status, problem.status);

	auto list = problem_dao.List(1000, 0, "published");
	std::vector<std::int64_t> ids;
	for (const auto& item : list) {
		ids.push_back(item.id);
	}
	EXPECT_TRUE(ContainsId(ids, problem_id));

	problem.title = "DAO problem updated " + suffix;
	problem.time_limit_ms = 1200;
	EXPECT_TRUE(problem_dao.Update(problem_id, problem));
	auto updated = problem_dao.GetById(problem_id);
	ASSERT_TRUE(updated.has_value());
	EXPECT_EQ(updated->title, problem.title);
	EXPECT_EQ(updated->time_limit_ms, 1200);

	auto samples = test_case_dao.ListByProblem(problem_id, true);
	ASSERT_EQ(samples.size(), 1u);
	EXPECT_EQ(samples.front().input, sample.input);

	auto all_cases = test_case_dao.ListByProblem(problem_id, false);
	ASSERT_EQ(all_cases.size(), 2u);
	EXPECT_TRUE(hidden_id > 0);
}

TEST_F(DbIntegrationTest, SubmissionDaoRoundTrip) {
	DbCleanup cleanup(Pool());
	oj::UserDao user_dao(Pool());
	oj::ProblemDao problem_dao(Pool());
	oj::SubmissionDao submission_dao(Pool());

	const std::string suffix = MakeUniqueSuffix();
	const std::int64_t user_id = user_dao.CreateUser("db_submission_user_" + suffix, "hash_" + suffix, "submission@example.com");
	cleanup.AddDeleteById("users", user_id);

	oj::Problem problem;
	problem.title = "Submission problem " + suffix;
	problem.description = "Return sum";
	problem.difficulty = "easy";
	problem.time_limit_ms = 1000;
	problem.memory_limit_kb = 65536;
	problem.status = "published";
	problem.created_by = user_id;

	const std::int64_t problem_id = problem_dao.Create(problem);
	cleanup.AddDeleteById("problems", problem_id);

	oj::Submission submission;
	submission.user_id = user_id;
	submission.problem_id = problem_id;
	submission.language_id = 1;
	submission.source_code = "#include <iostream>\nint main(){ std::cout << 3; }";
	submission.mode = "submit";
	submission.status = "pending";
	submission.result_json = R"({"status":"PENDING"})";
	submission.time_ms = 0;
	submission.memory_kb = 0;

	const std::int64_t submission_id = submission_dao.Create(submission);
	cleanup.AddDeleteById("submissions", submission_id);

	auto fetched = submission_dao.GetById(submission_id);
	ASSERT_TRUE(fetched.has_value());
	EXPECT_EQ(fetched->user_id, user_id);
	EXPECT_EQ(fetched->problem_id, problem_id);
	EXPECT_EQ(fetched->language_id, 1);
	EXPECT_EQ(fetched->status, "pending");

	EXPECT_TRUE(submission_dao.UpdateResult(submission_id, "accepted", R"({"status":"ACCEPTED"})", 12, 4096));
	auto updated = submission_dao.GetById(submission_id);
	ASSERT_TRUE(updated.has_value());
	EXPECT_EQ(updated->status, "accepted");
	EXPECT_EQ(nlohmann::json::parse(updated->result_json), nlohmann::json::parse(R"({"status":"ACCEPTED"})"));
	EXPECT_EQ(updated->time_ms, 12);
	EXPECT_EQ(updated->memory_kb, 4096);

	auto by_user = submission_dao.ListByUser(user_id, 100, 0);
	std::vector<std::int64_t> ids;
	for (const auto& item : by_user) {
		ids.push_back(item.id);
	}
	EXPECT_TRUE(ContainsId(ids, submission_id));
}
