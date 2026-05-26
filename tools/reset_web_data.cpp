#include <iostream>
#include <sstream>
#include <string>
#include <vector>

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
        std::cerr << "SQL error: " << mysql_error(conn) << "\n";
        std::cerr << "While executing: " << sql << "\n";
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

int main() {
    try {
        auto cfg = Config::LoadFromFile(ResolveConfigPath());
        cfg.mysql.user = kResetDbUser;
        cfg.mysql.password = kResetDbPassword;
        cfg.mysql.host = kResetDbHost;
        cfg.mysql.database = kResetDbName;
        MySqlPool pool(cfg.mysql);

        auto pconn = pool.Acquire();
        MYSQL* conn = pconn.get();
        if (!conn) {
            std::cerr << "failed to acquire mysql connection" << std::endl;
            return 1;
        }

        std::cout << "Disabling foreign key checks...\n";
        ExecOrDie(conn, "SET FOREIGN_KEY_CHECKS=0");

        // Truncate tables used by frontend tests
        std::vector<std::string> tables = {"submissions", "test_cases", "problems", "languages", "users"};
        for (const auto& t : tables) {
            std::string q = "TRUNCATE TABLE `" + t + "`";
            std::cout << "Truncating " << t << "\n";
            ExecOrDie(conn, q);
        }

        // Recreate basic users: admin and ui_user_20260526
        std::cout << "Inserting users...\n";
        std::string admin_pass = GeneratePasswordHash("adminpass");
        std::string user_pass = GeneratePasswordHash("pass1234");

        std::string admin_sql = "INSERT INTO users (username, password, email, role, status) VALUES ('admin', '" + Escape(conn, admin_pass) + "', 'admin@example.com', 'admin', 'active')";
        ExecOrDie(conn, admin_sql);
        std::string ui_sql = "INSERT INTO users (username, password, email, role, status) VALUES ('ui_user_20260526', '" + Escape(conn, user_pass) + "', 'ui@example.com', 'student', 'active')";
        ExecOrDie(conn, ui_sql);

        // Insert languages
        std::cout << "Inserting languages...\n";
        ExecOrDie(conn, "INSERT INTO languages (name, extension, compile_cmd, run_cmd, enabled) VALUES ('C++17', 'cpp', 'g++ -O2 -std=c++17 {source} -o {output}', '{binary}', 1)");
        ExecOrDie(conn, "INSERT INTO languages (name, extension, compile_cmd, run_cmd, enabled) VALUES ('C11', 'c', 'gcc -O2 -std=c11 {source} -o {output}', '{binary}', 1)");

        // Find a user id to use as created_by (admin)
        ExecOrDie(conn, "SELECT id FROM users WHERE username = 'admin' LIMIT 1");
        MYSQL_RES* res = mysql_store_result(conn);
        long admin_id = 0;
        if (res) {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row && row[0]) admin_id = std::stol(row[0]);
            mysql_free_result(res);
        }

        if (!admin_id) {
            std::cerr << "cannot find admin id" << std::endl;
            return 1;
        }

        // Insert problems with stable ids
        std::cout << "Inserting problems...\n";
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
        std::cout << "Inserting test cases...\n";
        ExecOrDie(conn, "INSERT INTO test_cases (problem_id, is_sample, input, output, sort_order) VALUES (900001, 1, '1 2\n', '3\n', 1)");
        ExecOrDie(conn, "INSERT INTO test_cases (problem_id, is_sample, input, output, sort_order) VALUES (900001, 1, '-5 10\n', '5\n', 2)");
        ExecOrDie(conn, "INSERT INTO test_cases (problem_id, is_sample, input, output, sort_order) VALUES (900003, 1, '123 456\n', '56088\n', 1)");

        // Prepare some sample submissions (only a few statuses)
        std::cout << "Inserting sample submissions...\n";

        // find ui user id
        ExecOrDie(conn, "SELECT id FROM users WHERE username = 'ui_user_20260526' LIMIT 1");
        res = mysql_store_result(conn);
        long ui_id = 0;
        if (res) {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row && row[0]) ui_id = std::stol(row[0]);
            mysql_free_result(res);
        }

        // find language id for C++17
        ExecOrDie(conn, "SELECT id FROM languages WHERE extension = 'cpp' LIMIT 1");
        res = mysql_store_result(conn);
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

        std::cout << "Database reset and seed complete." << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << std::endl;
        return 1;
    }
}
