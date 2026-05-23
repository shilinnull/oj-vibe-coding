#include "db/dao/language_dao.h"

#include <cstring>
#include <stdexcept>
#include <vector>

#include <mysql/mysql.h>

namespace oj {
namespace {

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

MYSQL_BIND BindIntIn(int* v) {
	MYSQL_BIND b{};
	std::memset(&b, 0, sizeof(b));
	b.buffer_type = MYSQL_TYPE_LONG;
	b.buffer = v;
	b.is_unsigned = 0;
	return b;
}

MYSQL_BIND BindBoolIn(bool* v) {
	// MySQL tinyint(1)
	MYSQL_BIND b{};
	std::memset(&b, 0, sizeof(b));
	b.buffer_type = MYSQL_TYPE_TINY;
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

MYSQL_BIND BindIntOut(int* v, bool* is_null) {
	MYSQL_BIND b{};
	std::memset(&b, 0, sizeof(b));
	b.buffer_type = MYSQL_TYPE_LONG;
	b.buffer = v;
	b.is_null = is_null;
	b.is_unsigned = 0;
	return b;
}

MYSQL_BIND BindTinyOut(unsigned char* v, bool* is_null) {
	MYSQL_BIND b{};
	std::memset(&b, 0, sizeof(b));
	b.buffer_type = MYSQL_TYPE_TINY;
	b.buffer = v;
	b.is_null = is_null;
	b.is_unsigned = 0;
	return b;
}

void CheckStmt(int rc, MYSQL_STMT* stmt, const char* what) {
	if (rc != 0) {
		throw std::runtime_error(std::string(what) + ": " + mysql_stmt_error(stmt));
	}
}

}  // namespace

LanguageDao::LanguageDao(MySqlPool& pool) : pool_(pool) {}

std::vector<Language> LanguageDao::ListAll(bool only_enabled) {
	auto conn = pool_.Acquire();
	MYSQL* c = reinterpret_cast<MYSQL*>(conn.get());
	const char* sql_enabled =
			"SELECT id,name,extension,compile_cmd,run_cmd,enabled,created_at FROM languages "
			"WHERE enabled=1 ORDER BY id";
	const char* sql_all =
			"SELECT id,name,extension,compile_cmd,run_cmd,enabled,created_at FROM languages ORDER BY id";
	const char* sql = only_enabled ? sql_enabled : sql_all;

	MYSQL_STMT* stmt = mysql_stmt_init(c);
	if (!stmt) throw std::runtime_error("mysql_stmt_init failed");
	try {
		CheckStmt(mysql_stmt_prepare(stmt, sql, std::strlen(sql)), stmt, "prepare");
		CheckStmt(mysql_stmt_execute(stmt), stmt, "execute");
		CheckStmt(mysql_stmt_store_result(stmt), stmt, "store_result");

		int id = 0;
		bool null_id = 0;
		StringOut out_name(128), out_ext(16), out_compile(2048), out_run(1024), out_created(32);
		unsigned char enabled_u8 = 0;
		bool null_enabled = 0;

		MYSQL_BIND outb[7] = {BindIntOut(&id, &null_id),
													BindStringOut(out_name),
													BindStringOut(out_ext),
													BindStringOut(out_compile),
													BindStringOut(out_run),
													BindTinyOut(&enabled_u8, &null_enabled),
													BindStringOut(out_created)};
		CheckStmt(mysql_stmt_bind_result(stmt, outb), stmt, "bind_result");

		std::vector<Language> res;
		while (true) {
			int rc = mysql_stmt_fetch(stmt);
			if (rc == MYSQL_NO_DATA) break;
			CheckStmt(rc, stmt, "fetch");
			Language l;
			l.id = id;
			l.name = out_name.str();
			l.extension = out_ext.str();
			l.compile_cmd = out_compile.str();
			l.run_cmd = out_run.str();
			l.enabled = (enabled_u8 != 0);
			l.created_at = out_created.str();
			res.push_back(std::move(l));
		}

		mysql_stmt_close(stmt);
		return res;
	} catch (...) {
		mysql_stmt_close(stmt);
		throw;
	}
}

std::optional<Language> LanguageDao::GetById(int id) {
	auto conn = pool_.Acquire();
	MYSQL* c = reinterpret_cast<MYSQL*>(conn.get());
	const char* sql =
			"SELECT id,name,extension,compile_cmd,run_cmd,enabled,created_at FROM languages WHERE id=? LIMIT 1";
	MYSQL_STMT* stmt = mysql_stmt_init(c);
	if (!stmt) throw std::runtime_error("mysql_stmt_init failed");
	try {
		CheckStmt(mysql_stmt_prepare(stmt, sql, std::strlen(sql)), stmt, "prepare");
		int in_id = id;
		MYSQL_BIND inb[1] = {BindIntIn(&in_id)};
		CheckStmt(mysql_stmt_bind_param(stmt, inb), stmt, "bind_param");
		CheckStmt(mysql_stmt_execute(stmt), stmt, "execute");
		CheckStmt(mysql_stmt_store_result(stmt), stmt, "store_result");

		int out_id = 0;
		bool null_id = 0;
		StringOut out_name(128), out_ext(16), out_compile(2048), out_run(1024), out_created(32);
		unsigned char enabled_u8 = 0;
		bool null_enabled = 0;
		MYSQL_BIND outb[7] = {BindIntOut(&out_id, &null_id),
													BindStringOut(out_name),
													BindStringOut(out_ext),
													BindStringOut(out_compile),
													BindStringOut(out_run),
													BindTinyOut(&enabled_u8, &null_enabled),
													BindStringOut(out_created)};
		CheckStmt(mysql_stmt_bind_result(stmt, outb), stmt, "bind_result");

		int rc = mysql_stmt_fetch(stmt);
		if (rc == MYSQL_NO_DATA) {
			mysql_stmt_close(stmt);
			return std::nullopt;
		}
		CheckStmt(rc, stmt, "fetch");

		Language l;
		l.id = out_id;
		l.name = out_name.str();
		l.extension = out_ext.str();
		l.compile_cmd = out_compile.str();
		l.run_cmd = out_run.str();
		l.enabled = (enabled_u8 != 0);
		l.created_at = out_created.str();
		mysql_stmt_close(stmt);
		return l;
	} catch (...) {
		mysql_stmt_close(stmt);
		throw;
	}
}

int LanguageDao::Create(const Language& lang) {
	auto conn = pool_.Acquire();
	MYSQL* c = reinterpret_cast<MYSQL*>(conn.get());
	const char* sql =
			"INSERT INTO languages(name,extension,compile_cmd,run_cmd,enabled) VALUES(?,?,?,?,?)";
	MYSQL_STMT* stmt = mysql_stmt_init(c);
	if (!stmt) throw std::runtime_error("mysql_stmt_init failed");
	try {
		CheckStmt(mysql_stmt_prepare(stmt, sql, std::strlen(sql)), stmt, "prepare");
		unsigned long l1 = 0, l2 = 0, l3 = 0, l4 = 0;
		bool enabled = lang.enabled;
		MYSQL_BIND b[5] = {BindStringIn(lang.name, &l1),
											 BindStringIn(lang.extension, &l2),
											 BindStringIn(lang.compile_cmd, &l3),
											 BindStringIn(lang.run_cmd, &l4),
											 BindBoolIn(&enabled)};
		CheckStmt(mysql_stmt_bind_param(stmt, b), stmt, "bind_param");
		CheckStmt(mysql_stmt_execute(stmt), stmt, "execute");
		const std::uint64_t id = mysql_insert_id(c);
		mysql_stmt_close(stmt);
		return static_cast<int>(id);
	} catch (...) {
		mysql_stmt_close(stmt);
		throw;
	}
}

bool LanguageDao::Update(int id, const Language& lang) {
	auto conn = pool_.Acquire();
	MYSQL* c = reinterpret_cast<MYSQL*>(conn.get());
	const char* sql =
			"UPDATE languages SET name=?,extension=?,compile_cmd=?,run_cmd=?,enabled=? WHERE id=?";
	MYSQL_STMT* stmt = mysql_stmt_init(c);
	if (!stmt) throw std::runtime_error("mysql_stmt_init failed");
	try {
		CheckStmt(mysql_stmt_prepare(stmt, sql, std::strlen(sql)), stmt, "prepare");
		unsigned long l1 = 0, l2 = 0, l3 = 0, l4 = 0;
		bool enabled = lang.enabled;
		int in_id = id;
		MYSQL_BIND b[6] = {BindStringIn(lang.name, &l1),
											 BindStringIn(lang.extension, &l2),
											 BindStringIn(lang.compile_cmd, &l3),
											 BindStringIn(lang.run_cmd, &l4),
											 BindBoolIn(&enabled),
											 BindIntIn(&in_id)};
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

bool LanguageDao::SetEnabled(int id, bool enabled) {
	auto conn = pool_.Acquire();
	MYSQL* c = reinterpret_cast<MYSQL*>(conn.get());
	const char* sql = "UPDATE languages SET enabled=? WHERE id=?";
	MYSQL_STMT* stmt = mysql_stmt_init(c);
	if (!stmt) throw std::runtime_error("mysql_stmt_init failed");
	try {
		CheckStmt(mysql_stmt_prepare(stmt, sql, std::strlen(sql)), stmt, "prepare");
		int in_id = id;
		MYSQL_BIND b[2] = {BindBoolIn(&enabled), BindIntIn(&in_id)};
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
