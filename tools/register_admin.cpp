#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

#include <mysql/mysql.h>

#include "utils/config.h"
#include "utils/crypto.h"

namespace {

constexpr const char* kDefaultConfigPath = "../config/config.yaml";
constexpr const char* kDefaultUsername = "admin";
constexpr const char* kDefaultPassword = "admin123";

struct MysqlConnection {
	MYSQL* conn{nullptr};

	explicit MysqlConnection(const oj::MysqlConfig& cfg) {
		conn = mysql_init(nullptr);
		if (conn == nullptr) {
			throw std::runtime_error("mysql_init failed");
		}

		unsigned int timeout_sec = static_cast<unsigned int>(std::max(1, cfg.pool.connect_timeout_ms / 1000));
		mysql_options(conn, MYSQL_OPT_CONNECT_TIMEOUT, &timeout_sec);

		if (mysql_real_connect(conn,
								 cfg.host.c_str(),
								 cfg.user.c_str(),
								 cfg.password.empty() ? nullptr : cfg.password.c_str(),
								 cfg.database.c_str(),
								 static_cast<unsigned int>(cfg.port),
								 nullptr,
								 0) == nullptr) {
			std::string err = mysql_error(conn);
			mysql_close(conn);
			conn = nullptr;
			throw std::runtime_error("mysql_real_connect failed: " + err);
		}

		mysql_set_character_set(conn, "utf8mb4");
	}

	MysqlConnection(const MysqlConnection&) = delete;
	MysqlConnection& operator=(const MysqlConnection&) = delete;

	~MysqlConnection() {
		if (conn != nullptr) {
			mysql_close(conn);
		}
	}
};

void ExecSql(MYSQL* conn, const std::string& sql) {
	if (mysql_query(conn, sql.c_str()) != 0) {
		throw std::runtime_error(std::string("mysql_query failed: ") + mysql_error(conn));
	}
	if (MYSQL_RES* result = mysql_store_result(conn)) {
		mysql_free_result(result);
	}
}

void BeginTransaction(MYSQL* conn) { ExecSql(conn, "START TRANSACTION"); }

void CommitTransaction(MYSQL* conn) { ExecSql(conn, "COMMIT"); }

void RollbackTransaction(MYSQL* conn) {
	try {
		ExecSql(conn, "ROLLBACK");
	} catch (...) {
	}
}

std::int64_t FindUserIdByUsername(MYSQL* conn, const std::string& username) {
	MYSQL_STMT* stmt = mysql_stmt_init(conn);
	if (stmt == nullptr) {
		throw std::runtime_error("mysql_stmt_init failed");
	}

	try {
		const char* sql = "SELECT id FROM users WHERE username=? LIMIT 1";
		if (mysql_stmt_prepare(stmt, sql, static_cast<unsigned long>(std::strlen(sql))) != 0) {
			throw std::runtime_error(std::string("mysql_stmt_prepare failed: ") + mysql_stmt_error(stmt));
		}

		unsigned long name_len = static_cast<unsigned long>(username.size());
		MYSQL_BIND input{};
		std::memset(&input, 0, sizeof(input));
		input.buffer_type = MYSQL_TYPE_STRING;
		input.buffer = const_cast<char*>(username.data());
		input.buffer_length = name_len;
		input.length = &name_len;

		if (mysql_stmt_bind_param(stmt, &input) != 0) {
			throw std::runtime_error(std::string("mysql_stmt_bind_param failed: ") + mysql_stmt_error(stmt));
		}
		if (mysql_stmt_execute(stmt) != 0) {
			throw std::runtime_error(std::string("mysql_stmt_execute failed: ") + mysql_stmt_error(stmt));
		}
		if (mysql_stmt_store_result(stmt) != 0) {
			throw std::runtime_error(std::string("mysql_stmt_store_result failed: ") + mysql_stmt_error(stmt));
		}

		std::int64_t id = 0;
		bool is_null = false;
		MYSQL_BIND output{};
		std::memset(&output, 0, sizeof(output));
		output.buffer_type = MYSQL_TYPE_LONGLONG;
		output.buffer = &id;
		output.is_null = &is_null;
		output.is_unsigned = 0;

		if (mysql_stmt_bind_result(stmt, &output) != 0) {
			throw std::runtime_error(std::string("mysql_stmt_bind_result failed: ") + mysql_stmt_error(stmt));
		}

		const int fetch_rc = mysql_stmt_fetch(stmt);
		if (fetch_rc == MYSQL_NO_DATA) {
			mysql_stmt_close(stmt);
			return -1;
		}
		if (fetch_rc != 0) {
			throw std::runtime_error(std::string("mysql_stmt_fetch failed: ") + mysql_stmt_error(stmt));
		}

		mysql_stmt_close(stmt);
		return id;
	} catch (...) {
		mysql_stmt_close(stmt);
		throw;
	}
}

void DeleteUserById(MYSQL* conn, std::int64_t id) {
	MYSQL_STMT* stmt = mysql_stmt_init(conn);
	if (stmt == nullptr) {
		throw std::runtime_error("mysql_stmt_init failed");
	}

	try {
		const char* sql = "DELETE FROM users WHERE id=?";
		if (mysql_stmt_prepare(stmt, sql, static_cast<unsigned long>(std::strlen(sql))) != 0) {
			throw std::runtime_error(std::string("mysql_stmt_prepare failed: ") + mysql_stmt_error(stmt));
		}

		MYSQL_BIND input{};
		std::memset(&input, 0, sizeof(input));
		input.buffer_type = MYSQL_TYPE_LONGLONG;
		input.buffer = &id;
		input.is_unsigned = 0;

		if (mysql_stmt_bind_param(stmt, &input) != 0) {
			throw std::runtime_error(std::string("mysql_stmt_bind_param failed: ") + mysql_stmt_error(stmt));
		}
		if (mysql_stmt_execute(stmt) != 0) {
			throw std::runtime_error(std::string("mysql_stmt_execute failed: ") + mysql_stmt_error(stmt));
		}

		mysql_stmt_close(stmt);
	} catch (...) {
		mysql_stmt_close(stmt);
		throw;
	}
}

std::int64_t InsertAdminUser(MYSQL* conn,
							   const std::string& username,
							   const std::string& password_hash) {
	MYSQL_STMT* stmt = mysql_stmt_init(conn);
	if (stmt == nullptr) {
		throw std::runtime_error("mysql_stmt_init failed");
	}

	try {
		const char* sql = "INSERT INTO users(username,password,email,role,status) VALUES(?,?,?,?,?)";
		if (mysql_stmt_prepare(stmt, sql, static_cast<unsigned long>(std::strlen(sql))) != 0) {
			throw std::runtime_error(std::string("mysql_stmt_prepare failed: ") + mysql_stmt_error(stmt));
		}

		std::string email;
		std::string role = "admin";
		std::string status = "active";

		unsigned long username_len = static_cast<unsigned long>(username.size());
		unsigned long password_len = static_cast<unsigned long>(password_hash.size());
		unsigned long email_len = static_cast<unsigned long>(email.size());
		unsigned long role_len = static_cast<unsigned long>(role.size());
		unsigned long status_len = static_cast<unsigned long>(status.size());

		MYSQL_BIND binds[5];
		std::memset(binds, 0, sizeof(binds));

		binds[0].buffer_type = MYSQL_TYPE_STRING;
		binds[0].buffer = const_cast<char*>(username.data());
		binds[0].buffer_length = username_len;
		binds[0].length = &username_len;

		binds[1].buffer_type = MYSQL_TYPE_STRING;
		binds[1].buffer = const_cast<char*>(password_hash.data());
		binds[1].buffer_length = password_len;
		binds[1].length = &password_len;

		binds[2].buffer_type = MYSQL_TYPE_STRING;
		binds[2].buffer = email.empty() ? const_cast<char*>("") : const_cast<char*>(email.data());
		binds[2].buffer_length = email_len;
		binds[2].length = &email_len;

		binds[3].buffer_type = MYSQL_TYPE_STRING;
		binds[3].buffer = const_cast<char*>(role.data());
		binds[3].buffer_length = role_len;
		binds[3].length = &role_len;

		binds[4].buffer_type = MYSQL_TYPE_STRING;
		binds[4].buffer = const_cast<char*>(status.data());
		binds[4].buffer_length = status_len;
		binds[4].length = &status_len;

		if (mysql_stmt_bind_param(stmt, binds) != 0) {
			throw std::runtime_error(std::string("mysql_stmt_bind_param failed: ") + mysql_stmt_error(stmt));
		}
		if (mysql_stmt_execute(stmt) != 0) {
			throw std::runtime_error(std::string("mysql_stmt_execute failed: ") + mysql_stmt_error(stmt));
		}

		const std::int64_t new_id = static_cast<std::int64_t>(mysql_insert_id(conn));
		mysql_stmt_close(stmt);
		return new_id;
	} catch (...) {
		mysql_stmt_close(stmt);
		throw;
	}
}

}  // namespace

int main(int argc, char* argv[]) {
	try {
		std::string config_path = kDefaultConfigPath;
		std::string username = kDefaultUsername;
		std::string password = kDefaultPassword;

		for (int i = 1; i < argc; ++i) {
			const std::string arg = argv[i];
			if (arg == "--config" && i + 1 < argc) {
				config_path = argv[++i];
			} else if (arg == "--username" && i + 1 < argc) {
				username = argv[++i];
			} else if (arg == "--password" && i + 1 < argc) {
				password = argv[++i];
			} else {
				std::cerr << "Usage: " << argv[0]
				          << " [--config path] [--username name] [--password value]\n";
				return 2;
			}
		}

		oj::AppConfig cfg = oj::Config::LoadFromFile(config_path);
		MysqlConnection conn(cfg.mysql);

		const std::string password_hash = oj::GeneratePasswordHash(password);
		BeginTransaction(conn.conn);

		const std::int64_t existing_id = FindUserIdByUsername(conn.conn, username);
		if (existing_id >= 0) {
			DeleteUserById(conn.conn, existing_id);
			std::cout << "Deleted existing admin account: " << username << " (id=" << existing_id << ")\n";
		} else {
			std::cout << "No existing admin account found, inserting a new one.\n";
		}

		const std::int64_t new_id = InsertAdminUser(conn.conn, username, password_hash);
		CommitTransaction(conn.conn);

		std::cout << "Admin account ready: username=" << username << ", id=" << new_id << '\n';
		return 0;
	} catch (const std::exception& ex) {
		std::cerr << "register_admin failed: " << ex.what() << '\n';
		return 1;
	}
}