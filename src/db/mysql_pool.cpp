#include "db/mysql_pool.h"

#include <stdexcept>

#include <mysql/mysql.h>

namespace oj {

PooledMySqlConnection::PooledMySqlConnection(MySqlPool* pool, MYSQL* conn)
		: pool_(pool), conn_(conn) {}

PooledMySqlConnection::PooledMySqlConnection(PooledMySqlConnection&& other) noexcept {
	pool_ = other.pool_;
	conn_ = other.conn_;
	other.pool_ = nullptr;
	other.conn_ = nullptr;
}

PooledMySqlConnection& PooledMySqlConnection::operator=(PooledMySqlConnection&& other) noexcept {
	if (this == &other) return *this;
	Release();
	pool_ = other.pool_;
	conn_ = other.conn_;
	other.pool_ = nullptr;
	other.conn_ = nullptr;
	return *this;
}

PooledMySqlConnection::~PooledMySqlConnection() { Release(); }

void PooledMySqlConnection::Release() {
	if (pool_ != nullptr && conn_ != nullptr) {
		pool_->Release(conn_);
	}
	pool_ = nullptr;
	conn_ = nullptr;
}

MySqlPool::MySqlPool(const MysqlConfig& cfg) : cfg_(cfg) {
	if (cfg_.pool.max_connections == 0) {
		cfg_.pool.max_connections = 1;
	}
}

MySqlPool::~MySqlPool() { Shutdown(); }

void MySqlPool::Shutdown() {
	std::lock_guard<std::mutex> lock(mu_);
	if (shutdown_) return;
	shutdown_ = true;
	while (!idle_.empty()) {
		MYSQL* c = idle_.front();
		idle_.pop();
		mysql_close(c);
	}
	total_ = 0;
	cv_.notify_all();
}

bool MySqlPool::IsConnectionAlive(MYSQL* conn) {
	if (conn == nullptr) return false;
	return mysql_ping(conn) == 0;
}

MYSQL* MySqlPool::CreateConnection() {
	MYSQL* conn = mysql_init(nullptr);
	if (conn == nullptr) {
		throw std::runtime_error("mysql_init failed");
	}

	unsigned int timeout_sec = static_cast<unsigned int>(std::max(1, cfg_.pool.connect_timeout_ms / 1000));
	mysql_options(conn, MYSQL_OPT_CONNECT_TIMEOUT, &timeout_sec);

	if (mysql_real_connect(conn,
		cfg_.host.c_str(),
		cfg_.user.c_str(),
		cfg_.password.empty() ? nullptr : cfg_.password.c_str(),
		cfg_.database.c_str(),
		static_cast<unsigned int>(cfg_.port),
		nullptr,
		0) == nullptr) {
		std::string err = mysql_error(conn);
		mysql_close(conn);
		throw std::runtime_error("mysql_real_connect failed: " + err);
	}

	mysql_set_character_set(conn, "utf8mb4");
	return conn;
}

PooledMySqlConnection MySqlPool::Acquire() {
	std::unique_lock<std::mutex> lock(mu_);
	if (shutdown_) {
		throw std::runtime_error("mysql pool is shut down");
	}

	while (idle_.empty() && total_ >= cfg_.pool.max_connections && !shutdown_) {
		cv_.wait(lock);
	}
	if (shutdown_) {
		throw std::runtime_error("mysql pool is shut down");
	}

	if (!idle_.empty()) {
		MYSQL* c = idle_.front();
		idle_.pop();
		lock.unlock();
		if (!IsConnectionAlive(c)) {
			mysql_close(c);
			lock.lock();
			--total_;
			lock.unlock();
			// retry
			return Acquire();
		}
		return PooledMySqlConnection(this, c);
	}

	// Create new connection
	++total_;
	lock.unlock();
	try {
		MYSQL* c = CreateConnection();
		return PooledMySqlConnection(this, c);
	} catch (...) {
		lock.lock();
		--total_;
		cv_.notify_one();
		throw;
	}
}

void MySqlPool::Release(MYSQL* conn) {
	std::lock_guard<std::mutex> lock(mu_);
	if (shutdown_) {
		if (conn != nullptr) {
			mysql_close(conn);
		}
		return;
	}

	if (conn == nullptr) {
		return;
	}

	if (!IsConnectionAlive(conn)) {
		mysql_close(conn);
		if (total_ > 0) {
			--total_;
		}
		cv_.notify_one();
		return;
	}

	idle_.push(conn);
	cv_.notify_one();
}

}  // namespace oj
