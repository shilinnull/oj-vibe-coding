#include <gtest/gtest.h>

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

#include "db/dao/language_dao.h"
#include "db/dao/problem_dao.h"
#include "db/dao/test_case_dao.h"
#include "db/dao/user_dao.h"
#include "middleware/auth.h"
#include "utils/config.h"
#include "server.h"

namespace {

using json = nlohmann::json;

constexpr const char* kMysqlHost = "127.0.0.1";
constexpr int kMysqlPort = 3306;
constexpr const char* kMysqlUser = "shilin";
constexpr const char* kMysqlPassword = "123456";
constexpr const char* kMysqlDatabase = "oj";

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

int FindFreePort() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;
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
        if (res) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

class ServerGuard {
 public:
    ServerGuard(httplib::Server& server, int port)
        : server_(server), thread_([&server, port]() { server.listen("127.0.0.1", port); }) {}
    ~ServerGuard() {
        server_.stop();
        if (thread_.joinable()) thread_.join();
    }
 private:
    httplib::Server& server_;
    std::thread thread_;
};

class AdminApiTest : public ::testing::Test {
 protected:
    static void SetUpTestSuite() {
        // reuse bootstrap from other tests by including same setup
        extern void BootstrapSchema();
        BootstrapSchema();
        pool_ = std::make_unique<oj::MySqlPool>(MakeMysqlConfig());
    }

    static void TearDownTestSuite() { pool_.reset(); }

    static oj::MySqlPool& Pool() { return *pool_; }
    static std::unique_ptr<oj::MySqlPool> pool_;
};

std::unique_ptr<oj::MySqlPool> AdminApiTest::pool_;

httplib::Client MakeClient(int port) {
    httplib::Client client("127.0.0.1", port);
    client.set_connection_timeout(1, 0);
    client.set_read_timeout(2, 0);
    return client;
}

std::string MakeJsonBody(const json& body) { return body.dump(); }

TEST_F(AdminApiTest, AdminLanguageAndProblemLifecycle) {
    DbCleanup cleanup(Pool());
    oj::UserDao user_dao(Pool());
    oj::LanguageDao lang_dao(Pool());
    oj::ProblemDao problem_dao(Pool());
    oj::TestCaseDao tc_dao(Pool());

    const std::string suf = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::int64_t admin_id = user_dao.CreateUser("admin_user_" + suf, "hash", "a@b.c", "admin");
    cleanup.AddDeleteById("users", admin_id);

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

    // create language as admin
    auto jwt = oj::GenerateJwt(cfg.auth.jwt.secret, admin_id, "admin_user", "admin", 3600);
    json lang_body = {
        {"name", "TestLang"},
        {"extension", "tl"},
        {"compile_cmd", "compile"},
        {"run_cmd", "run"}
    };
    httplib::Headers hdrs = {{"Authorization", std::string("Bearer ") + jwt}};
    auto res = client.Post("/api/admin/languages", hdrs, MakeJsonBody(lang_body), "application/json");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 201);
    auto lang_json = json::parse(res->body);
    ASSERT_TRUE(lang_json.contains("id"));
    int lang_id = lang_json["id"].get<int>();
    cleanup.AddDeleteById("languages", lang_id);

    // create problem
    json prob_body = {
        {"title", "API admin problem " + suf},
        {"description", "desc"},
        {"difficulty", "easy"},
        {"time_limit_ms", 1000},
        {"memory_limit_kb", 65536},
        {"status", "published"}
    };
    res = client.Post("/api/admin/problems", hdrs, MakeJsonBody(prob_body), "application/json");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 201);
    auto prob_json = json::parse(res->body);
    std::int64_t pid = prob_json["id"].get<std::int64_t>();
    cleanup.AddDeleteById("problems", pid);

    // add a testcase
    json tc_body = {
        {"is_sample", true},
        {"input", "1 2"},
        {"output", "3"},
        {"sort_order", 1}
    };
    std::string tc_path = "/api/admin/problems/" + std::to_string(pid) + "/testcases";
    res = client.Post(tc_path.c_str(), hdrs, MakeJsonBody(tc_body), "application/json");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 201);
    auto tc_json = json::parse(res->body);
    ASSERT_TRUE(tc_json.contains("id"));
    cleanup.AddDeleteById("test_cases", tc_json["id"].get<std::int64_t>());

    // change user status (ban)
    json status_body = {{"status", "banned"}};
    std::string status_path = "/api/admin/users/" + std::to_string(admin_id) + "/status";
    res = client.Put(status_path.c_str(), hdrs, MakeJsonBody(status_body), "application/json");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);

    oj::UserDao udao(Pool());
    auto maybe_user = udao.GetById(admin_id);
    ASSERT_TRUE(maybe_user.has_value());
    EXPECT_EQ(maybe_user->status, "banned");
}

}  // namespace
