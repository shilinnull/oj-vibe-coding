#include "db/dao/user_dao.h"

#include <cstring>
#include <stdexcept>
#include <vector>

#include <mysql/mysql.h>

namespace oj {
namespace {

// MySQL C API 的查询结果需要手动绑定输出缓冲区，这里封装成小工具降低重复代码量。
struct StringOut {
	std::vector<char> buf;
	unsigned long len{0};
	bool is_null{0};
	explicit StringOut(std::size_t cap) : buf(cap, 0) {}
	std::string str() const { return is_null ? std::string() : std::string(buf.data(), len); }
};

MYSQL_BIND BindStringIn(const std::string& s, unsigned long* len) {
	MYSQL_BIND b{};
	std::memset(&b, 0, sizeof(b));
	*len = static_cast<unsigned long>(s.size());
	b.buffer_type = MYSQL_TYPE_STRING;
	b.buffer = const_cast<char*>(s.data());
	b.buffer_length = *len;
	b.length = len;
	return b;
}

MYSQL_BIND BindInt64In(std::int64_t* v) {
	MYSQL_BIND b{};
	std::memset(&b, 0, sizeof(b));
	b.buffer_type = MYSQL_TYPE_LONGLONG;
	b.buffer = v;
	b.is_unsigned = 0;
	return b;
}

MYSQL_BIND BindStringOut(StringOut& out) {
	MYSQL_BIND b{};
	std::memset(&b, 0, sizeof(b));
	b.buffer_type = MYSQL_TYPE_STRING;
	b.buffer = out.buf.data();
	b.buffer_length = static_cast<unsigned long>(out.buf.size());
	b.length = &out.len;
	b.is_null = &out.is_null;
	return b;
}

MYSQL_BIND BindInt64Out(std::int64_t* v, bool* is_null) {
	MYSQL_BIND b{};
	std::memset(&b, 0, sizeof(b));
	b.buffer_type = MYSQL_TYPE_LONGLONG;
	b.buffer = v;
	b.is_null = is_null;
	b.is_unsigned = 0;
	return b;
}

MYSQL_BIND BindIntOut(int* v, bool* is_null) {
	MYSQL_BIND b{};
	std::memset(&b, 0, sizeof(b));
	b.buffer_type = MYSQL_TYPE_LONG;
	b.buffer = v;
	b.is_null = is_null;
	b.is_unsigned = 0;
	return b;
}

void Check(int rc, MYSQL* conn, const char* what) {
	if (rc != 0) {
		throw std::runtime_error(std::string(what) + ": " + mysql_error(conn));
	}
}

void CheckStmt(int rc, MYSQL_STMT* stmt, const char* what) {
	if (rc != 0) {
		throw std::runtime_error(std::string(what) + ": " + mysql_stmt_error(stmt));
	}
}

}  // namespace

UserDao::UserDao(MySqlPool& pool) : pool_(pool) {}

std::int64_t UserDao::CreateUser(const std::string& username,
																const std::string& password_hash,
																const std::string& email,
																const std::string& role,
																const std::string& status) {
	auto conn = pool_.Acquire();
	MYSQL* c = reinterpret_cast<MYSQL*>(conn.get());

	const char* sql =
			"INSERT INTO users(username,password,email,role,status) VALUES(?,?,?,?,?)";
	MYSQL_STMT* stmt = mysql_stmt_init(c);
	if (!stmt) throw std::runtime_error("mysql_stmt_init failed");
	try {
		// 用预编译语句写入，避免手拼 SQL，也能减少注入风险。
		CheckStmt(mysql_stmt_prepare(stmt, sql, std::strlen(sql)), stmt, "prepare");

		unsigned long l1 = 0, l2 = 0, l3 = 0, l4 = 0, l5 = 0;
		MYSQL_BIND binds[5] = {BindStringIn(username, &l1),
													 BindStringIn(password_hash, &l2),
													 BindStringIn(email, &l3),
													 BindStringIn(role, &l4),
													 BindStringIn(status, &l5)};
		CheckStmt(mysql_stmt_bind_param(stmt, binds), stmt, "bind_param");
		CheckStmt(mysql_stmt_execute(stmt), stmt, "execute");
		const std::uint64_t id = mysql_insert_id(c);
		mysql_stmt_close(stmt);
		return static_cast<std::int64_t>(id);
	} catch (...) {
		mysql_stmt_close(stmt);
		throw;
	}
}

std::optional<User> UserDao::GetByUsername(const std::string& username) {
	auto conn = pool_.Acquire();
	MYSQL* c = reinterpret_cast<MYSQL*>(conn.get());

	const char* sql =
			"SELECT id,username,password,email,role,status,created_at,updated_at "
			"FROM users WHERE username=? LIMIT 1";
	MYSQL_STMT* stmt = mysql_stmt_init(c);
	if (!stmt) throw std::runtime_error("mysql_stmt_init failed");

	try {
		// 查单个用户时，先把参数和返回列都绑定好，再执行和取结果。
		CheckStmt(mysql_stmt_prepare(stmt, sql, std::strlen(sql)), stmt, "prepare");
		unsigned long l1 = 0;
		MYSQL_BIND inb[1] = {BindStringIn(username, &l1)};
		CheckStmt(mysql_stmt_bind_param(stmt, inb), stmt, "bind_param");
		CheckStmt(mysql_stmt_execute(stmt), stmt, "execute");
		CheckStmt(mysql_stmt_store_result(stmt), stmt, "store_result");

		User u;
				bool null_id = 0;
		std::int64_t id = 0;
		StringOut out_username(128), out_password(512), out_email(256), out_role(32), out_status(32);
		StringOut out_created(32), out_updated(32);

		MYSQL_BIND outb[8] = {BindInt64Out(&id, &null_id),
													BindStringOut(out_username),
													BindStringOut(out_password),
													BindStringOut(out_email),
													BindStringOut(out_role),
													BindStringOut(out_status),
													BindStringOut(out_created),
													BindStringOut(out_updated)};
		CheckStmt(mysql_stmt_bind_result(stmt, outb), stmt, "bind_result");

		int fetch_rc = mysql_stmt_fetch(stmt);
		if (fetch_rc == MYSQL_NO_DATA) {
			mysql_stmt_close(stmt);
			return std::nullopt;
		}
		CheckStmt(fetch_rc, stmt, "fetch");

		u.id = id;
		u.username = out_username.str();
		u.password = out_password.str();
		u.email = out_email.str();
		u.role = out_role.str();
		u.status = out_status.str();
		u.created_at = out_created.str();
		u.updated_at = out_updated.str();

		mysql_stmt_close(stmt);
		return u;
	} catch (...) {
		mysql_stmt_close(stmt);
		throw;
	}
}

std::optional<User> UserDao::GetById(std::int64_t id) {
	auto conn = pool_.Acquire();
	MYSQL* c = reinterpret_cast<MYSQL*>(conn.get());

	const char* sql =
			"SELECT id,username,password,email,role,status,created_at,updated_at "
			"FROM users WHERE id=? LIMIT 1";
	MYSQL_STMT* stmt = mysql_stmt_init(c);
	if (!stmt) throw std::runtime_error("mysql_stmt_init failed");

	try {
		// 逻辑和 GetByUsername 一样，只是 WHERE 条件换成主键 id。
		CheckStmt(mysql_stmt_prepare(stmt, sql, std::strlen(sql)), stmt, "prepare");

		std::int64_t in_id = id;
		MYSQL_BIND inb[1] = {BindInt64In(&in_id)};
		CheckStmt(mysql_stmt_bind_param(stmt, inb), stmt, "bind_param");
		CheckStmt(mysql_stmt_execute(stmt), stmt, "execute");
		CheckStmt(mysql_stmt_store_result(stmt), stmt, "store_result");

		User u;
				bool null_id = 0;
		std::int64_t out_id = 0;
		StringOut out_username(128), out_password(512), out_email(256), out_role(32), out_status(32);
		StringOut out_created(32), out_updated(32);

		MYSQL_BIND outb[8] = {BindInt64Out(&out_id, &null_id),
													BindStringOut(out_username),
													BindStringOut(out_password),
													BindStringOut(out_email),
													BindStringOut(out_role),
													BindStringOut(out_status),
													BindStringOut(out_created),
													BindStringOut(out_updated)};
		CheckStmt(mysql_stmt_bind_result(stmt, outb), stmt, "bind_result");

		int fetch_rc = mysql_stmt_fetch(stmt);
		if (fetch_rc == MYSQL_NO_DATA) {
			mysql_stmt_close(stmt);
			return std::nullopt;
		}
		CheckStmt(fetch_rc, stmt, "fetch");

		u.id = out_id;
		u.username = out_username.str();
		u.password = out_password.str();
		u.email = out_email.str();
		u.role = out_role.str();
		u.status = out_status.str();
		u.created_at = out_created.str();
		u.updated_at = out_updated.str();

		mysql_stmt_close(stmt);
		return u;
	} catch (...) {
		mysql_stmt_close(stmt);
		throw;
	}
}

bool UserDao::UpdateStatus(std::int64_t id, const std::string& status) {
	auto conn = pool_.Acquire();
	MYSQL* c = reinterpret_cast<MYSQL*>(conn.get());
	const char* sql = "UPDATE users SET status=? WHERE id=?";
	MYSQL_STMT* stmt = mysql_stmt_init(c);
	if (!stmt) throw std::runtime_error("mysql_stmt_init failed");
	try {
		// 更新状态只改一列，返回值表示这次是否真的影响到了记录。
		CheckStmt(mysql_stmt_prepare(stmt, sql, std::strlen(sql)), stmt, "prepare");

		unsigned long l1 = 0;
		std::int64_t in_id = id;
		MYSQL_BIND b[2] = {BindStringIn(status, &l1), BindInt64In(&in_id)};
		CheckStmt(mysql_stmt_bind_param(stmt, b), stmt, "bind_param");
		CheckStmt(mysql_stmt_execute(stmt), stmt, "execute");

		const my_ulonglong affected = mysql_stmt_affected_rows(stmt);
		mysql_stmt_close(stmt);
		return affected > 0;
	} catch (...) {
		mysql_stmt_close(stmt);
		throw;
	}
}

}  // namespace oj
