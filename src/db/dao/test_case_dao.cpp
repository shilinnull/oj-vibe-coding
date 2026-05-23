#include "db/dao/test_case_dao.h"

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

MYSQL_BIND BindTinyIn(unsigned char* v) {
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

MYSQL_BIND BindInt64Out(std::int64_t* v, bool* is_null) {
	MYSQL_BIND b{};
	std::memset(&b, 0, sizeof(b));
	b.buffer_type = MYSQL_TYPE_LONGLONG;
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

TestCaseDao::TestCaseDao(MySqlPool& pool) : pool_(pool) {}

std::int64_t TestCaseDao::Add(const TestCase& tc) {
	auto conn = pool_.Acquire();
	MYSQL* c = reinterpret_cast<MYSQL*>(conn.get());
	const char* sql =
			"INSERT INTO test_cases(problem_id,is_sample,input,output,sort_order) VALUES(?,?,?,?,?)";
	MYSQL_STMT* stmt = mysql_stmt_init(c);
	if (!stmt) throw std::runtime_error("mysql_stmt_init failed");
	try {
		CheckStmt(mysql_stmt_prepare(stmt, sql, std::strlen(sql)), stmt, "prepare");
		std::int64_t pid = tc.problem_id;
		unsigned char is_sample = tc.is_sample ? 1 : 0;
		unsigned long l_in = 0, l_out = 0;
		int sort_order = tc.sort_order;
		MYSQL_BIND b[5] = {BindInt64In(&pid),
											 BindTinyIn(&is_sample),
											 BindStringIn(tc.input, &l_in),
											 BindStringIn(tc.output, &l_out),
											 BindIntIn(&sort_order)};
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

std::vector<TestCase> TestCaseDao::ListByProblem(std::int64_t problem_id, bool only_sample) {
	auto conn = pool_.Acquire();
	MYSQL* c = reinterpret_cast<MYSQL*>(conn.get());
	const char* sql_sample =
			"SELECT id,problem_id,is_sample,input,output,sort_order,created_at FROM test_cases "
			"WHERE problem_id=? AND is_sample=1 ORDER BY sort_order,id";
	const char* sql_all =
			"SELECT id,problem_id,is_sample,input,output,sort_order,created_at FROM test_cases "
			"WHERE problem_id=? ORDER BY sort_order,id";
	const char* sql = only_sample ? sql_sample : sql_all;

	MYSQL_STMT* stmt = mysql_stmt_init(c);
	if (!stmt) throw std::runtime_error("mysql_stmt_init failed");
	try {
		CheckStmt(mysql_stmt_prepare(stmt, sql, std::strlen(sql)), stmt, "prepare");
		std::int64_t pid = problem_id;
		MYSQL_BIND inb[1] = {BindInt64In(&pid)};
		CheckStmt(mysql_stmt_bind_param(stmt, inb), stmt, "bind_param");
		CheckStmt(mysql_stmt_execute(stmt), stmt, "execute");
		CheckStmt(mysql_stmt_store_result(stmt), stmt, "store_result");

		std::int64_t id = 0;
		bool null_id = 0;
		std::int64_t out_pid = 0;
		bool null_pid = 0;
		unsigned char is_sample = 0;
		bool null_is_sample = 0;
		StringOut in_text(65536);
		StringOut out_text(65536);
		int sort_order = 0;
		bool null_sort = 0;
		StringOut created(32);

		MYSQL_BIND outb[7] = {BindInt64Out(&id, &null_id),
													BindInt64Out(&out_pid, &null_pid),
													BindTinyOut(&is_sample, &null_is_sample),
													BindStringOut(in_text),
													BindStringOut(out_text),
													BindIntOut(&sort_order, &null_sort),
													BindStringOut(created)};
		CheckStmt(mysql_stmt_bind_result(stmt, outb), stmt, "bind_result");

		std::vector<TestCase> res;
		while (true) {
			int rc = mysql_stmt_fetch(stmt);
			if (rc == MYSQL_NO_DATA) break;
			CheckStmt(rc, stmt, "fetch");
			TestCase tc;
			tc.id = id;
			tc.problem_id = out_pid;
			tc.is_sample = (is_sample != 0);
			tc.input = in_text.str();
			tc.output = out_text.str();
			tc.sort_order = sort_order;
			tc.created_at = created.str();
			res.push_back(std::move(tc));
		}

		mysql_stmt_close(stmt);
		return res;
	} catch (...) {
		mysql_stmt_close(stmt);
		throw;
	}
}

}  // namespace oj
