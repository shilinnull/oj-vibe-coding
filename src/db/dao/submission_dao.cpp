#include "db/dao/submission_dao.h"

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

SubmissionDao::SubmissionDao(MySqlPool& pool) : pool_(pool) {}

std::int64_t SubmissionDao::Create(const Submission& s) {
	auto conn = pool_.Acquire();
	MYSQL* c = reinterpret_cast<MYSQL*>(conn.get());
	const char* sql =
			"INSERT INTO submissions(user_id,problem_id,language_id,source_code,mode,status,result_json,time_ms,memory_kb) "
			"VALUES(?,?,?,?,?,?,?,?,?)";
	MYSQL_STMT* stmt = mysql_stmt_init(c);
	if (!stmt) throw std::runtime_error("mysql_stmt_init failed");
	try {
		CheckStmt(mysql_stmt_prepare(stmt, sql, std::strlen(sql)), stmt, "prepare");
		std::int64_t user_id = s.user_id;
		std::int64_t problem_id = s.problem_id;
		int language_id = s.language_id;
		unsigned long l_src = 0, l_mode = 0, l_status = 0, l_json = 0;
		int time_ms = s.time_ms;
		int mem_kb = s.memory_kb;
		MYSQL_BIND b[9] = {BindInt64In(&user_id),
											 BindInt64In(&problem_id),
											 BindIntIn(&language_id),
											 BindStringIn(s.source_code, &l_src),
											 BindStringIn(s.mode, &l_mode),
											 BindStringIn(s.status, &l_status),
											 BindStringIn(s.result_json, &l_json),
											 BindIntIn(&time_ms),
											 BindIntIn(&mem_kb)};
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

std::optional<Submission> SubmissionDao::GetById(std::int64_t id) {
	auto conn = pool_.Acquire();
	MYSQL* c = reinterpret_cast<MYSQL*>(conn.get());
	const char* sql =
			"SELECT id,user_id,problem_id,language_id,source_code,mode,status,result_json,time_ms,memory_kb,created_at "
			"FROM submissions WHERE id=? LIMIT 1";
	MYSQL_STMT* stmt = mysql_stmt_init(c);
	if (!stmt) throw std::runtime_error("mysql_stmt_init failed");

	try {
		CheckStmt(mysql_stmt_prepare(stmt, sql, std::strlen(sql)), stmt, "prepare");
		std::int64_t in_id = id;
		MYSQL_BIND inb[1] = {BindInt64In(&in_id)};
		CheckStmt(mysql_stmt_bind_param(stmt, inb), stmt, "bind_param");
		CheckStmt(mysql_stmt_execute(stmt), stmt, "execute");
		CheckStmt(mysql_stmt_store_result(stmt), stmt, "store_result");

		Submission s;
		std::int64_t out_id = 0;
		bool null_id = 0;
		std::int64_t user_id = 0;
		bool null_uid = 0;
		std::int64_t problem_id = 0;
		bool null_pid = 0;
		int language_id = 0;
		bool null_lid = 0;
		StringOut out_src(65536);
		StringOut out_mode(16);
		StringOut out_status(32);
		StringOut out_json(65536);
		int time_ms = 0;
		bool null_time = false;
		int mem_kb = 0;
		bool null_mem = false;
		StringOut out_created(32);

		MYSQL_BIND outb[11] = {BindInt64Out(&out_id, &null_id),
													 BindInt64Out(&user_id, &null_uid),
													 BindInt64Out(&problem_id, &null_pid),
													 BindIntOut(&language_id, &null_lid),
													 BindStringOut(out_src),
													 BindStringOut(out_mode),
													 BindStringOut(out_status),
													 BindStringOut(out_json),
													 BindIntOut(&time_ms, &null_time),
													 BindIntOut(&mem_kb, &null_mem),
													 BindStringOut(out_created)};
		CheckStmt(mysql_stmt_bind_result(stmt, outb), stmt, "bind_result");

		int rc = mysql_stmt_fetch(stmt);
		if (rc == MYSQL_NO_DATA) {
			mysql_stmt_close(stmt);
			return std::nullopt;
		}
		CheckStmt(rc, stmt, "fetch");

		s.id = out_id;
		s.user_id = user_id;
		s.problem_id = problem_id;
		s.language_id = language_id;
		s.source_code = out_src.str();
		s.mode = out_mode.str();
		s.status = out_status.str();
		s.result_json = out_json.str();
		s.time_ms = time_ms;
		s.memory_kb = mem_kb;
		s.created_at = out_created.str();

		mysql_stmt_close(stmt);
		return s;
	} catch (...) {
		mysql_stmt_close(stmt);
		throw;
	}
}

std::vector<Submission> SubmissionDao::ListByUser(std::int64_t user_id, int limit, int offset) {
	auto conn = pool_.Acquire();
	MYSQL* c = reinterpret_cast<MYSQL*>(conn.get());
	const char* sql =
			"SELECT id,user_id,problem_id,language_id,mode,status,time_ms,memory_kb,created_at "
			"FROM submissions WHERE user_id=? ORDER BY id DESC LIMIT ? OFFSET ?";
	MYSQL_STMT* stmt = mysql_stmt_init(c);
	if (!stmt) throw std::runtime_error("mysql_stmt_init failed");

	try {
		CheckStmt(mysql_stmt_prepare(stmt, sql, std::strlen(sql)), stmt, "prepare");
		std::int64_t in_uid = user_id;
		int in_limit = limit;
		int in_offset = offset;
		MYSQL_BIND inb[3] = {BindInt64In(&in_uid), BindIntIn(&in_limit), BindIntIn(&in_offset)};
		CheckStmt(mysql_stmt_bind_param(stmt, inb), stmt, "bind_param");
		CheckStmt(mysql_stmt_execute(stmt), stmt, "execute");
		CheckStmt(mysql_stmt_store_result(stmt), stmt, "store_result");

		std::int64_t out_id = 0;
		bool null_id = 0;
		std::int64_t out_uid = 0;
		bool null_uid = 0;
		std::int64_t out_pid = 0;
		bool null_pid = 0;
		int out_lid = 0;
		bool null_lid = 0;
		StringOut out_mode(16);
		StringOut out_status(32);
		int time_ms = 0;
		bool null_time = false;
		int mem_kb = 0;
		bool null_mem = false;
		StringOut out_created(32);

		MYSQL_BIND outb[9] = {BindInt64Out(&out_id, &null_id),
													BindInt64Out(&out_uid, &null_uid),
													BindInt64Out(&out_pid, &null_pid),
													BindIntOut(&out_lid, &null_lid),
													BindStringOut(out_mode),
													BindStringOut(out_status),
													BindIntOut(&time_ms, &null_time),
													BindIntOut(&mem_kb, &null_mem),
													BindStringOut(out_created)};
		CheckStmt(mysql_stmt_bind_result(stmt, outb), stmt, "bind_result");

		std::vector<Submission> res;
		while (true) {
			int rc = mysql_stmt_fetch(stmt);
			if (rc == MYSQL_NO_DATA) break;
			CheckStmt(rc, stmt, "fetch");
			Submission s;
			s.id = out_id;
			s.user_id = out_uid;
			s.problem_id = out_pid;
			s.language_id = out_lid;
			s.mode = out_mode.str();
			s.status = out_status.str();
			s.time_ms = time_ms;
			s.memory_kb = mem_kb;
			s.created_at = out_created.str();
			res.push_back(std::move(s));
		}

		mysql_stmt_close(stmt);
		return res;
	} catch (...) {
		mysql_stmt_close(stmt);
		throw;
	}
}

std::vector<Submission> SubmissionDao::ListAll(int limit, int offset) {
	auto conn = pool_.Acquire();
	MYSQL* c = reinterpret_cast<MYSQL*>(conn.get());
	const char* sql =
			"SELECT id,user_id,problem_id,language_id,mode,status,time_ms,memory_kb,created_at "
			"FROM submissions ORDER BY id DESC LIMIT ? OFFSET ?";
	MYSQL_STMT* stmt = mysql_stmt_init(c);
	if (!stmt) throw std::runtime_error("mysql_stmt_init failed");

	try {
		CheckStmt(mysql_stmt_prepare(stmt, sql, std::strlen(sql)), stmt, "prepare");
		int in_limit = limit;
		int in_offset = offset;
		MYSQL_BIND inb[2] = {BindIntIn(&in_limit), BindIntIn(&in_offset)};
		CheckStmt(mysql_stmt_bind_param(stmt, inb), stmt, "bind_param");
		CheckStmt(mysql_stmt_execute(stmt), stmt, "execute");
		CheckStmt(mysql_stmt_store_result(stmt), stmt, "store_result");

		std::int64_t out_id = 0;
		bool null_id = 0;
		std::int64_t out_uid = 0;
		bool null_uid = 0;
		std::int64_t out_pid = 0;
		bool null_pid = 0;
		int out_lid = 0;
		bool null_lid = 0;
		StringOut out_mode(16);
		StringOut out_status(32);
		int time_ms = 0;
		bool null_time = false;
		int mem_kb = 0;
		bool null_mem = false;
		StringOut out_created(32);

		MYSQL_BIND outb[9] = {BindInt64Out(&out_id, &null_id),
							BindInt64Out(&out_uid, &null_uid),
							BindInt64Out(&out_pid, &null_pid),
							BindIntOut(&out_lid, &null_lid),
							BindStringOut(out_mode),
							BindStringOut(out_status),
							BindIntOut(&time_ms, &null_time),
							BindIntOut(&mem_kb, &null_mem),
							BindStringOut(out_created)};
		CheckStmt(mysql_stmt_bind_result(stmt, outb), stmt, "bind_result");

		std::vector<Submission> res;
		while (true) {
			int rc = mysql_stmt_fetch(stmt);
			if (rc == MYSQL_NO_DATA) break;
			CheckStmt(rc, stmt, "fetch");
			Submission s;
			s.id = out_id;
			s.user_id = out_uid;
			s.problem_id = out_pid;
			s.language_id = out_lid;
			s.mode = out_mode.str();
			s.status = out_status.str();
			s.time_ms = time_ms;
			s.memory_kb = mem_kb;
			s.created_at = out_created.str();
			res.push_back(std::move(s));
		}

		mysql_stmt_close(stmt);
		return res;
	} catch (...) {
		mysql_stmt_close(stmt);
		throw;
	}
}

bool SubmissionDao::UpdateResult(std::int64_t id,
																const std::string& status,
																const std::string& result_json,
																int time_ms,
																int memory_kb) {
	auto conn = pool_.Acquire();
	MYSQL* c = reinterpret_cast<MYSQL*>(conn.get());
	const char* sql =
			"UPDATE submissions SET status=?,result_json=?,time_ms=?,memory_kb=? WHERE id=?";
	MYSQL_STMT* stmt = mysql_stmt_init(c);
	if (!stmt) throw std::runtime_error("mysql_stmt_init failed");
	try {
		CheckStmt(mysql_stmt_prepare(stmt, sql, std::strlen(sql)), stmt, "prepare");
		unsigned long l_status = 0, l_json = 0;
		std::int64_t in_id = id;
		int in_time = time_ms;
		int in_mem = memory_kb;
		MYSQL_BIND b[5] = {BindStringIn(status, &l_status),
											 BindStringIn(result_json, &l_json),
											 BindIntIn(&in_time),
											 BindIntIn(&in_mem),
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

}  // namespace oj
