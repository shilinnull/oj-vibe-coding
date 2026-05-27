#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <mysql/mysql.h>
#include <sys/socket.h>
#include <unistd.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "db/dao/problem_dao.h"
#include "db/dao/submission_dao.h"
#include "db/dao/test_case_dao.h"
#include "db/dao/user_dao.h"
#include "server.h"

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

int FindFreePort() {
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		return 0;
	}

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = 0;

	if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
		close(fd);
		return 0;
	}

	socklen_t len = sizeof(addr);
	if (getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
		close(fd);
		return 0;
	}

	const int port = ntohs(addr.sin_port);
	close(fd);
	return port;
}

bool WaitForServer(int port) {
	httplib::Client client("127.0.0.1", port);
	client.set_connection_timeout(1, 0);
	client.set_read_timeout(1, 0);

	for (int i = 0; i < 50; ++i) {
		auto res = client.Get("/healthz");
		if (res) {
			return true;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}

	return false;
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
	ExecSql(server.get(), "CREATE DATABASE IF NOT EXISTS oj DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci");
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

class ServerGuard {
 public:
	ServerGuard(httplib::Server& server, int port)
		: server_(server), thread_([&server, port]() { server.listen("127.0.0.1", port); }) {}

	~ServerGuard() {
		server_.stop();
		if (thread_.joinable()) {
			thread_.join();
		}
	}

	ServerGuard(const ServerGuard&) = delete;
	ServerGuard& operator=(const ServerGuard&) = delete;

 private:
	httplib::Server& server_;
	std::thread thread_;
};

class ApiIntegrationTest : public ::testing::Test {
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

std::unique_ptr<oj::MySqlPool> ApiIntegrationTest::pool_;

httplib::Client MakeClient(int port) {
	httplib::Client client("127.0.0.1", port);
	client.set_connection_timeout(1, 0);
	client.set_read_timeout(2, 0);
	return client;
}

std::string MakeJsonBody(const nlohmann::json& body) {
	return body.dump();
}

}  // namespace

TEST_F(ApiIntegrationTest, ProblemApiReturnsListAndDetail) {
	DbCleanup cleanup(Pool());
	oj::UserDao user_dao(Pool());
	oj::ProblemDao problem_dao(Pool());
	oj::TestCaseDao test_case_dao(Pool());

	const std::string suffix = MakeUniqueSuffix();
	const std::int64_t user_id = user_dao.CreateUser("api_problem_user_" + suffix, "hash_" + suffix, "problem@example.com");
	cleanup.AddDeleteById("users", user_id);

	oj::Problem problem;
	problem.title = "API problem " + suffix;
	problem.description = "Return sum";
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

	oj::AppConfig cfg;
	cfg.auth.jwt.secret = "test-secret";
	cfg.mysql = MakeMysqlConfig();
	oj::HttpServer server(cfg);
	httplib::Server raw_server;
	server.router().Mount(raw_server);

	const int port = FindFreePort();
	ASSERT_GT(port, 0);
	ServerGuard guard(raw_server, port);
	ASSERT_TRUE(WaitForServer(port));

	auto client = MakeClient(port);
	auto list_res = client.Get("/api/problems?limit=20&offset=0&status=published");
	ASSERT_TRUE(list_res);
	EXPECT_EQ(list_res->status, 200);
	auto list_json = nlohmann::json::parse(list_res->body);
	ASSERT_TRUE(list_json["items"].is_array());
	ASSERT_GE(list_json["items"].size(), 1u);
	EXPECT_EQ(list_json["items"][0]["id"].get<std::int64_t>(), problem_id);
	EXPECT_EQ(list_json["items"][0]["title"].get<std::string>(), problem.title);

	auto detail_res = client.Get(("/api/problems/" + std::to_string(problem_id)).c_str());
	ASSERT_TRUE(detail_res);
	EXPECT_EQ(detail_res->status, 200);
	auto detail_json = nlohmann::json::parse(detail_res->body);
	EXPECT_EQ(detail_json["id"].get<std::int64_t>(), problem_id);
	EXPECT_EQ(detail_json["title"].get<std::string>(), problem.title);
	ASSERT_TRUE(detail_json["samples"].is_array());
	ASSERT_EQ(detail_json["samples"].size(), 1u);
	EXPECT_EQ(detail_json["samples"][0]["input"].get<std::string>(), sample.input);
}

TEST_F(ApiIntegrationTest, SubmissionApiCreatesAndFetchesSubmission) {
	DbCleanup cleanup(Pool());
	oj::UserDao user_dao(Pool());
	oj::ProblemDao problem_dao(Pool());
	oj::TestCaseDao test_case_dao(Pool());

	const std::string suffix = MakeUniqueSuffix();
	const std::int64_t user_id = user_dao.CreateUser("api_submission_user_" + suffix, "hash_" + suffix, "submission@example.com");
	cleanup.AddDeleteById("users", user_id);

	oj::Problem problem;
	problem.title = "Submission API problem " + suffix;
	problem.description = "Return sum";
	problem.difficulty = "easy";
	problem.time_limit_ms = 1000;
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

	oj::AppConfig cfg;
	cfg.auth.jwt.secret = "test-secret";
	cfg.mysql = MakeMysqlConfig();
	oj::HttpServer server(cfg);
	httplib::Server raw_server;
	server.router().Mount(raw_server);

	const int port = FindFreePort();
	ASSERT_GT(port, 0);
	ServerGuard guard(raw_server, port);
	ASSERT_TRUE(WaitForServer(port));

	auto client = MakeClient(port);
	nlohmann::json create_body = {
			{"user_id", user_id},
			{"problem_id", problem_id},
			{"language_id", 1},
			{"mode", "run"},
			{"source_code", "#include <iostream>\nint main(){ std::cout << 3; }"},
	};
	auto create_res = client.Post("/api/submissions", MakeJsonBody(create_body), "application/json");
	ASSERT_TRUE(create_res);
	EXPECT_EQ(create_res->status, 201);
	auto create_json = nlohmann::json::parse(create_res->body);
	ASSERT_TRUE(create_json.contains("id"));
	const std::int64_t submission_id = create_json["id"].get<std::int64_t>();
	EXPECT_EQ(create_json["user_id"].get<std::int64_t>(), user_id);
	EXPECT_EQ(create_json["problem_id"].get<std::int64_t>(), problem_id);
	EXPECT_EQ(create_json["language_id"].get<int>(), 1);
	EXPECT_EQ(create_json["mode"].get<std::string>(), "run");
	EXPECT_EQ(create_json["sample_case_count"].get<std::size_t>(), 1u);
	EXPECT_EQ(create_json["result"]["status"].get<std::string>(), "PENDING");
	EXPECT_EQ(create_json["result"]["judge_scope"].get<std::string>(), "sample");

	auto get_res = client.Get(("/api/submissions/" + std::to_string(submission_id)).c_str());
	ASSERT_TRUE(get_res);
	EXPECT_EQ(get_res->status, 200);
	auto get_json = nlohmann::json::parse(get_res->body);
	EXPECT_EQ(get_json["id"].get<std::int64_t>(), submission_id);
	EXPECT_EQ(get_json["status"].get<std::string>(), "pending");
	EXPECT_EQ(get_json["mode"].get<std::string>(), "run");
	EXPECT_EQ(get_json["result"]["judge_scope"].get<std::string>(), "sample");

	auto history_res = client.Get(("/api/submissions?user_id=" + std::to_string(user_id)).c_str());
	ASSERT_TRUE(history_res);
	EXPECT_EQ(history_res->status, 200);
	auto history_json = nlohmann::json::parse(history_res->body);
	ASSERT_TRUE(history_json["items"].is_array());
	ASSERT_GE(history_json["items"].size(), 1u);
	EXPECT_EQ(history_json["items"][0]["id"].get<std::int64_t>(), submission_id);
}

TEST_F(ApiIntegrationTest, SubmissionApiRunFallsBackToAllCasesWhenNoSampleCasesExist) {
	DbCleanup cleanup(Pool());
	oj::UserDao user_dao(Pool());
	oj::ProblemDao problem_dao(Pool());
	oj::TestCaseDao test_case_dao(Pool());

	const std::string suffix = MakeUniqueSuffix();
	const std::int64_t user_id = user_dao.CreateUser("api_run_user_" + suffix, "hash_" + suffix, "run@example.com");
	cleanup.AddDeleteById("users", user_id);

	oj::Problem problem;
	problem.title = "Run fallback problem " + suffix;
	problem.description = "Return sum";
	problem.difficulty = "easy";
	problem.time_limit_ms = 1000;
	problem.memory_limit_kb = 65536;
	problem.status = "published";
	problem.created_by = user_id;

	const std::int64_t problem_id = problem_dao.Create(problem);
	cleanup.AddDeleteById("problems", problem_id);

	oj::TestCase hidden;
	hidden.problem_id = problem_id;
	hidden.is_sample = false;
	hidden.input = "1 2\n";
	hidden.output = "3\n";
	hidden.sort_order = 1;
	const std::int64_t hidden_id = test_case_dao.Add(hidden);
	cleanup.AddDeleteById("test_cases", hidden_id);

	oj::AppConfig cfg;
	cfg.auth.jwt.secret = "test-secret";
	cfg.mysql = MakeMysqlConfig();
	oj::HttpServer server(cfg);
	httplib::Server raw_server;
	server.router().Mount(raw_server);

	const int port = FindFreePort();
	ASSERT_GT(port, 0);
	ServerGuard guard(raw_server, port);
	ASSERT_TRUE(WaitForServer(port));

	auto client = MakeClient(port);
	nlohmann::json create_body = {
			{"user_id", user_id},
			{"problem_id", problem_id},
			{"language_id", 1},
			{"mode", "run"},
			{"source_code", "#include <iostream>\nint main(){ long long a,b; if(!(std::cin>>a>>b)) return 0; std::cout << a+b << '\\n'; }"},
	};
	auto create_res = client.Post("/api/submissions", MakeJsonBody(create_body), "application/json");
	ASSERT_TRUE(create_res);
	EXPECT_EQ(create_res->status, 201);
	auto create_json = nlohmann::json::parse(create_res->body);
	ASSERT_TRUE(create_json.contains("result"));
	EXPECT_EQ(create_json["sample_case_count"].get<std::size_t>(), 1u);
	EXPECT_EQ(create_json["result"]["judge_scope"].get<std::string>(), "all");
	EXPECT_EQ(create_json["result"]["status"].get<std::string>(), "PENDING");
}
