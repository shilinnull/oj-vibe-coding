#include "db/dao/problem_dao.h"

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

MYSQL_BIND BindIntOut(int* v, bool* is_null) {
	MYSQL_BIND b{};
	std::memset(&b, 0, sizeof(b));
	b.buffer_type = MYSQL_TYPE_LONG;
	b.buffer = v;
	b.is_null = is_null;
	b.is_unsigned = 0;
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

void CheckStmt(int rc, MYSQL_STMT* stmt, const char* what) {
	if (rc != 0) {
		throw std::runtime_error(std::string(what) + ": " + mysql_stmt_error(stmt));
	}
}

}  // namespace

ProblemDao::ProblemDao(MySqlPool& pool) : pool_(pool) {}

std::int64_t ProblemDao::Create(const Problem& p) {
	auto conn = pool_.Acquire();
	MYSQL* c = reinterpret_cast<MYSQL*>(conn.get());
	const char* sql =
			"INSERT INTO problems(title,description,difficulty,time_limit_ms,memory_limit_kb,status,created_by) "
			"VALUES(?,?,?,?,?,?,?)";
	MYSQL_STMT* stmt = mysql_stmt_init(c);
	if (!stmt) throw std::runtime_error("mysql_stmt_init failed");
	try {
		CheckStmt(mysql_stmt_prepare(stmt, sql, std::strlen(sql)), stmt, "prepare");
		unsigned long l1 = 0, l2 = 0, l3 = 0, l4 = 0;
		int tl = p.time_limit_ms;
		int ml = p.memory_limit_kb;
		std::int64_t created_by = p.created_by;
		MYSQL_BIND b[7] = {BindStringIn(p.title, &l1),
											 BindStringIn(p.description, &l2),
											 BindStringIn(p.difficulty, &l3),
											 BindIntIn(&tl),
											 BindIntIn(&ml),
											 BindStringIn(p.status, &l4),
											 BindInt64In(&created_by)};
		CheckStmt(mysql_stmt_bind_param(stmt, b), stmt, "bind_param");
		CheckStmt(mysql_stmt_execute(stmt), stmt, "execute");
		const std::uint64_t id = mysql_insert_id(c);
		mysql_stmt_close(stmt);
		return static_cast<std::int64_t>(id);
	} catch (...) {
		mysql_stmt_close(stmt);
		throw;
	}
}

bool ProblemDao::Update(std::int64_t id, const Problem& p) {
	auto conn = pool_.Acquire();
	MYSQL* c = reinterpret_cast<MYSQL*>(conn.get());
	const char* sql =
			"UPDATE problems SET title=?,description=?,difficulty=?,time_limit_ms=?,memory_limit_kb=?,status=? WHERE id=?";
	MYSQL_STMT* stmt = mysql_stmt_init(c);
	if (!stmt) throw std::runtime_error("mysql_stmt_init failed");
	try {
		CheckStmt(mysql_stmt_prepare(stmt, sql, std::strlen(sql)), stmt, "prepare");
		unsigned long l1 = 0, l2 = 0, l3 = 0, l4 = 0;
		int tl = p.time_limit_ms;
		int ml = p.memory_limit_kb;
		std::int64_t in_id = id;
		MYSQL_BIND b[7] = {BindStringIn(p.title, &l1),
											 BindStringIn(p.description, &l2),
											 BindStringIn(p.difficulty, &l3),
											 BindIntIn(&tl),
											 BindIntIn(&ml),
											 BindStringIn(p.status, &l4),
											 BindInt64In(&in_id)};
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

bool ProblemDao::Delete(std::int64_t id) {
	auto conn = pool_.Acquire();
	MYSQL* c = reinterpret_cast<MYSQL*>(conn.get());
	const char* sql = "DELETE FROM problems WHERE id=?";
	MYSQL_STMT* stmt = mysql_stmt_init(c);
	if (!stmt) throw std::runtime_error("mysql_stmt_init failed");
	try {
		CheckStmt(mysql_stmt_prepare(stmt, sql, std::strlen(sql)), stmt, "prepare");
		std::int64_t in_id = id;
		MYSQL_BIND b[1] = {BindInt64In(&in_id)};
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

std::optional<Problem> ProblemDao::GetById(std::int64_t id) {
	auto conn = pool_.Acquire();
	MYSQL* c = reinterpret_cast<MYSQL*>(conn.get());
	const char* sql =
			"SELECT id,title,description,difficulty,time_limit_ms,memory_limit_kb,status,created_by,created_at,updated_at "
			"FROM problems WHERE id=? LIMIT 1";
	MYSQL_STMT* stmt = mysql_stmt_init(c);
	if (!stmt) throw std::runtime_error("mysql_stmt_init failed");
	try {
		CheckStmt(mysql_stmt_prepare(stmt, sql, std::strlen(sql)), stmt, "prepare");
		std::int64_t in_id = id;
		MYSQL_BIND inb[1] = {BindInt64In(&in_id)};
		CheckStmt(mysql_stmt_bind_param(stmt, inb), stmt, "bind_param");
		CheckStmt(mysql_stmt_execute(stmt), stmt, "execute");
		CheckStmt(mysql_stmt_store_result(stmt), stmt, "store_result");

		Problem p;
		std::int64_t out_id = 0;
		bool null_id = 0;
		StringOut out_title(512);
		StringOut out_desc(65536);
		StringOut out_diff(32);
		int tl = 0;
		int ml = 0;
		bool null_tl = 0, null_ml = 0;
		StringOut out_status(32);
		std::int64_t created_by = 0;
		bool null_created_by = 0;
		StringOut out_created(32), out_updated(32);

		MYSQL_BIND outb[10] = {
				BindInt64Out(&out_id, &null_id),
				BindStringOut(out_title),
				BindStringOut(out_desc),
				BindStringOut(out_diff),
				BindIntOut(&tl, &null_tl),
				BindIntOut(&ml, &null_ml),
				BindStringOut(out_status),
				BindInt64Out(&created_by, &null_created_by),
				BindStringOut(out_created),
				BindStringOut(out_updated),
		};
		CheckStmt(mysql_stmt_bind_result(stmt, outb), stmt, "bind_result");

		int rc = mysql_stmt_fetch(stmt);
		if (rc == MYSQL_NO_DATA) {
			mysql_stmt_close(stmt);
			return std::nullopt;
		}
		CheckStmt(rc, stmt, "fetch");

		p.id = out_id;
		p.title = out_title.str();
		p.description = out_desc.str();
		p.difficulty = out_diff.str();
		p.time_limit_ms = tl;
		p.memory_limit_kb = ml;
		p.status = out_status.str();
		p.created_by = created_by;
		p.created_at = out_created.str();
		p.updated_at = out_updated.str();

		mysql_stmt_close(stmt);
		return p;
	} catch (...) {
		mysql_stmt_close(stmt);
		throw;
	}
}

std::vector<Problem> ProblemDao::List(int limit, int offset, const std::string& status_filter) {
	auto conn = pool_.Acquire();
	MYSQL* c = reinterpret_cast<MYSQL*>(conn.get());

	// NOTE: 列表不返回 description（TEXT 大字段），详情再查。
	const char* sql_all =
			"SELECT id,title,difficulty,time_limit_ms,memory_limit_kb,status,created_by,created_at,updated_at "
			"FROM problems ORDER BY id DESC LIMIT ? OFFSET ?";
	const char* sql_status =
			"SELECT id,title,difficulty,time_limit_ms,memory_limit_kb,status,created_by,created_at,updated_at "
			"FROM problems WHERE status=? ORDER BY id DESC LIMIT ? OFFSET ?";

	bool has_filter = !status_filter.empty();
	const char* sql = has_filter ? sql_status : sql_all;
	MYSQL_STMT* stmt = mysql_stmt_init(c);
	if (!stmt) throw std::runtime_error("mysql_stmt_init failed");
	try {
		CheckStmt(mysql_stmt_prepare(stmt, sql, std::strlen(sql)), stmt, "prepare");
		int in_limit = limit;
		int in_offset = offset;

		unsigned long l1 = 0;
		if (has_filter) {
			MYSQL_BIND inb[3] = {BindStringIn(status_filter, &l1), BindIntIn(&in_limit),
													 BindIntIn(&in_offset)};
			CheckStmt(mysql_stmt_bind_param(stmt, inb), stmt, "bind_param");
		} else {
			MYSQL_BIND inb[2] = {BindIntIn(&in_limit), BindIntIn(&in_offset)};
			CheckStmt(mysql_stmt_bind_param(stmt, inb), stmt, "bind_param");
		}

		CheckStmt(mysql_stmt_execute(stmt), stmt, "execute");
		CheckStmt(mysql_stmt_store_result(stmt), stmt, "store_result");

		std::int64_t id64 = 0;
		bool null_id = 0;
		StringOut out_title(512);
		StringOut out_diff(32);
		int tl = 0;
		int ml = 0;
		bool null_tl = 0, null_ml = 0;
		StringOut out_status(32);
		std::int64_t created_by = 0;
		bool null_created_by = 0;
		StringOut out_created(32), out_updated(32);

		MYSQL_BIND outb[9] = {BindInt64Out(&id64, &null_id),
													BindStringOut(out_title),
													BindStringOut(out_diff),
													BindIntOut(&tl, &null_tl),
													BindIntOut(&ml, &null_ml),
													BindStringOut(out_status),
													BindInt64Out(&created_by, &null_created_by),
													BindStringOut(out_created),
													BindStringOut(out_updated)};
		CheckStmt(mysql_stmt_bind_result(stmt, outb), stmt, "bind_result");

		std::vector<Problem> res;
		while (true) {
			int rc = mysql_stmt_fetch(stmt);
			if (rc == MYSQL_NO_DATA) break;
			CheckStmt(rc, stmt, "fetch");
			Problem p;
			p.id = id64;
			p.title = out_title.str();
			p.difficulty = out_diff.str();
			p.time_limit_ms = tl;
			p.memory_limit_kb = ml;
			p.status = out_status.str();
			p.created_by = created_by;
			p.created_at = out_created.str();
			p.updated_at = out_updated.str();
			res.push_back(std::move(p));
		}
		mysql_stmt_close(stmt);
		return res;
	} catch (...) {
		mysql_stmt_close(stmt);
		throw;
	}
}

}  // namespace oj
