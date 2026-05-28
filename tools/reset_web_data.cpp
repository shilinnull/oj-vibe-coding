#include <sstream>
#include <string>
#include <vector>
#include <chrono>
#include "utils/logger.h"

#include <mysql/mysql.h>
#include <fstream>
#include <nlohmann/json.hpp>

#include "utils/config.h"
#include "utils/crypto.h"
#include "db/mysql_pool.h"

using namespace oj;

static constexpr const char* kResetDbUser = "shilin";
static constexpr const char* kResetDbPassword = "123456";
static constexpr const char* kResetDbHost = "localhost";
static constexpr const char* kResetDbName = "oj";
static constexpr const char* kAdminUsername = "admin";
static constexpr const char* kAdminPassword = "admin123";

static std::string ResolveConfigPath() {
    const char* candidates[] = {"./config/config.yaml", "../config/config.yaml", "../../config/config.yaml"};
    for (const char* c : candidates) {
        std::ifstream ifs(c);
        if (ifs.good()) {
            return std::string(c);
        }
    }
    return std::string("./config/config.yaml");
}

static void ExecOrDie(MYSQL* conn, const std::string& sql) {
    if (mysql_real_query(conn, sql.c_str(), (unsigned long)sql.size())) {
        OJ_LOG_ERROR(std::string("SQL error: ") + mysql_error(conn));
        OJ_LOG_ERROR("While executing: " + sql);
        std::exit(1);
    }
}

static std::string Escape(MYSQL* conn, const std::string& s) {
    std::string out;
    out.resize(s.size() * 2 + 1);
    unsigned long len = mysql_real_escape_string(conn, &out[0], s.c_str(), (unsigned long)s.size());
    out.resize(len);
    return out;
}

static std::string CurrentTimestampSuffix() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto sec = duration_cast<seconds>(now.time_since_epoch()).count();
    return std::to_string(sec);
}

static long QueryUserIdByUsername(MYSQL* conn, const std::string& username) {
    ExecOrDie(conn, "SELECT id FROM users WHERE username = '" + Escape(conn, username) + "' LIMIT 1");
    MYSQL_RES* res = mysql_store_result(conn);
    long user_id = 0;
    if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row && row[0]) user_id = std::stol(row[0]);
        mysql_free_result(res);
    }
    return user_id;
}

static long EnsureAdminAccount(MYSQL* conn) {
    const std::string admin_hash = GeneratePasswordHash(kAdminPassword);
    const long existing_id = QueryUserIdByUsername(conn, kAdminUsername);
    if (existing_id > 0) {
        std::string update_sql =
            "UPDATE users SET username='" + Escape(conn, kAdminUsername) +
            "', password='" + Escape(conn, admin_hash) +
            "', email='', role='admin', status='active' WHERE id=" + std::to_string(existing_id);
        ExecOrDie(conn, update_sql);
        return existing_id;
    }

    std::string insert_sql =
        "INSERT INTO users (username, password, email, role, status) VALUES ('" + Escape(conn, kAdminUsername) +
        "', '" + Escape(conn, admin_hash) +
        "', '', 'admin', 'active')";
    ExecOrDie(conn, insert_sql);
    return (long)mysql_insert_id(conn);
}

int main() {
    try {
        auto cfg = Config::LoadFromFile(ResolveConfigPath());
        Logger::Instance().Init(cfg.logging);
        cfg.mysql.user = kResetDbUser;
        cfg.mysql.password = kResetDbPassword;
        cfg.mysql.host = kResetDbHost;
        cfg.mysql.database = kResetDbName;
        MySqlPool pool(cfg.mysql);

        auto pconn = pool.Acquire();
        MYSQL* conn = pconn.get();
        if (!conn) {
            OJ_LOG_ERROR("failed to acquire mysql connection");
            return 1;
        }

        OJ_LOG_INFO("Disabling foreign key checks...");
        ExecOrDie(conn, "SET FOREIGN_KEY_CHECKS=0");

        // Truncate tables used by frontend tests
        std::vector<std::string> tables = {"submissions", "test_cases", "problems", "languages", "users"};
        for (const auto& t : tables) {
            std::string q = "TRUNCATE TABLE `" + t + "`";
            OJ_LOG_INFO("Truncating " + t);
            ExecOrDie(conn, q);
        }

        // Recreate basic users: admin and several student users with timestamp suffix.
        OJ_LOG_INFO("Inserting users...");
        std::string user_pass = GeneratePasswordHash("pass1234");
        std::string ts_suffix = CurrentTimestampSuffix();
        std::vector<std::string> seeded_users;
        const int kStudentUserCount = 6;

        long admin_id = EnsureAdminAccount(conn);
        OJ_LOG_INFO(std::string("Admin account ready: username=") + kAdminUsername + ", id=" + std::to_string(admin_id));
        for (int i = 1; i <= kStudentUserCount; ++i) {
            std::string username = "ui_user_" + ts_suffix + "_" + std::to_string(i);
            std::string email = "ui_" + ts_suffix + "_" + std::to_string(i) + "@example.com";
            std::string ui_sql = "INSERT INTO users (username, password, email, role, status) VALUES ('" + Escape(conn, username) + "', '" + Escape(conn, user_pass) + "', '" + Escape(conn, email) + "', 'student', 'active')";
            ExecOrDie(conn, ui_sql);
            seeded_users.push_back(username);
        }

        // Insert languages
        OJ_LOG_INFO("Inserting languages...");
        ExecOrDie(conn, "INSERT INTO languages (name, extension, compile_cmd, run_cmd, enabled) VALUES ('C++17', 'cpp', 'g++ -O2 -std=c++17 {source} -o {output}', '{binary}', 1)");
        ExecOrDie(conn, "INSERT INTO languages (name, extension, compile_cmd, run_cmd, enabled) VALUES ('C11', 'c', 'gcc -O2 -std=c11 {source} -o {output}', '{binary}', 1)");

        // Find a user id to use as created_by (admin)
        if (!admin_id) {
            OJ_LOG_ERROR("cannot find admin id");
            return 1;
        }

        // Insert problems with stable ids
        OJ_LOG_INFO("Inserting problems...");
        std::vector<std::tuple<long, std::string, std::string, std::string, int, int, std::string>> problems = {
            {900001, "A + B Problem", "给定两个整数，输出它们的和。\n输入: 一行两个整数。", "easy", 1000, 262144, "published"},
            {900002, "Array Rotate", "旋转数组题目描述。", "medium", 1500, 262144, "published"},
            {900003, "Big Integer Multiply", "大整数乘法题目描述。", "hard", 2500, 524288, "published"}
        };

        for (auto& p : problems) {
            long id; std::string title, desc, diff; int tl, ml; std::string status;
            std::tie(id, title, desc, diff, tl, ml, status) = p;
            std::string q = "INSERT INTO problems (id, title, description, difficulty, time_limit_ms, memory_limit_kb, status, created_by) VALUES ('" + std::to_string(id) + "', '" + Escape(conn, title) + "', '" + Escape(conn, desc) + "', '" + diff + "', " + std::to_string(tl) + ", " + std::to_string(ml) + ", '" + status + "', " + std::to_string(admin_id) + ")";
            ExecOrDie(conn, q);
        }

        // Insert sample test cases
        OJ_LOG_INFO("Inserting test cases...");
        ExecOrDie(conn, "INSERT INTO test_cases (problem_id, is_sample, input, output, sort_order) VALUES (900001, 1, '1 2\n', '3\n', 1)");
        ExecOrDie(conn, "INSERT INTO test_cases (problem_id, is_sample, input, output, sort_order) VALUES (900001, 1, '-5 10\n', '5\n', 2)");
        ExecOrDie(conn, "INSERT INTO test_cases (problem_id, is_sample, input, output, sort_order) VALUES (900003, 1, '123 456\n', '56088\n', 1)");

        // Prepare some sample submissions (only a few statuses)
        OJ_LOG_INFO("Inserting sample submissions...");

        // find one seeded ui user id
        if (seeded_users.empty()) {
            OJ_LOG_ERROR("no seeded users found");
            return 1;
        }
        long ui_id = QueryUserIdByUsername(conn, seeded_users.front());

        // find language id for C++17
        ExecOrDie(conn, "SELECT id FROM languages WHERE extension = 'cpp' LIMIT 1");
        MYSQL_RES* res = mysql_store_result(conn);
        long cpp_lang = 0;
        if (res) {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row && row[0]) cpp_lang = std::stol(row[0]);
            mysql_free_result(res);
        }

        auto makeResultJson = [&](const nlohmann::json& j) {
            return Escape(conn, j.dump());
        };

        nlohmann::json ac_result = {
            {"status", "ACCEPTED"},
            {"compile_error", nullptr},
            {"results", {{{"test_case_id", 1}, {"status", "ACCEPTED"}, {"time_ms", 10}, {"memory_kb", 1024}, {"actual_output", "3\n"}, {"expected_output", "3\n"}}}},
            {"summary", {{"total",1}, {"passed",1}, {"total_time_ms",10}, {"peak_memory_kb",1024}}}
        };

        nlohmann::json wa_result = {
            {"status", "WRONG_ANSWER"},
            {"compile_error", nullptr},
            {"results", {{{"test_case_id", 1}, {"status", "WRONG_ANSWER"}, {"time_ms", 5}, {"memory_kb", 512}, {"actual_output", "4\n"}, {"expected_output", "3\n"}}}},
            {"summary", {{"total",1}, {"passed",0}, {"total_time_ms",5}, {"peak_memory_kb",512}}}
        };

        // Insert an Accepted submission
        std::string insert_sub_ac = "INSERT INTO submissions (user_id, problem_id, language_id, source_code, mode, status, result_json, time_ms, memory_kb) VALUES (" + std::to_string(ui_id) + ", 900001, " + std::to_string(cpp_lang) + ", 'int main() { return 0; }', 'submit', 'accepted', '" + makeResultJson(ac_result) + "', 10, 1024)";
        ExecOrDie(conn, insert_sub_ac);

        // Insert a Wrong Answer submission
        std::string insert_sub_wa = "INSERT INTO submissions (user_id, problem_id, language_id, source_code, mode, status, result_json, time_ms, memory_kb) VALUES (" + std::to_string(ui_id) + ", 900001, " + std::to_string(cpp_lang) + ", 'int main() { return 0; }', 'submit', 'wrong_answer', '" + makeResultJson(wa_result) + "', 5, 512)";
        ExecOrDie(conn, insert_sub_wa);

        // Re-enable foreign key checks
        ExecOrDie(conn, "SET FOREIGN_KEY_CHECKS=1");

        OJ_LOG_INFO("Database reset and seed complete.");
        return 0;
    } catch (const std::exception& e) {
        OJ_LOG_ERROR(std::string("fatal: ") + e.what());
        return 1;
    }
}
