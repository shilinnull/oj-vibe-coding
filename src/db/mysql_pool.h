#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>
#include <string>

#include "utils/config.h"
#include <mysql/mysql.h>

namespace oj {

class MySqlPool;

class PooledMySqlConnection {
 public:
	PooledMySqlConnection() = default;
	PooledMySqlConnection(MySqlPool* pool, MYSQL* conn);
	PooledMySqlConnection(const PooledMySqlConnection&) = delete;
	PooledMySqlConnection& operator=(const PooledMySqlConnection&) = delete;
	PooledMySqlConnection(PooledMySqlConnection&& other) noexcept;
	PooledMySqlConnection& operator=(PooledMySqlConnection&& other) noexcept;
	~PooledMySqlConnection();

	MYSQL* get() const { return conn_; }
	explicit operator bool() const { return conn_ != nullptr; }

 private:
	void Release();

	MySqlPool* pool_{nullptr};
	MYSQL* conn_{nullptr};
};

class MySqlPool {
 public:
	explicit MySqlPool(const MysqlConfig& cfg);
	~MySqlPool();

	PooledMySqlConnection Acquire();
	void Release(MYSQL* conn);
	void Shutdown();

 private:
	MYSQL* CreateConnection();
	bool IsConnectionAlive(MYSQL* conn);

	MysqlConfig cfg_{};
	std::mutex mu_{};
	std::condition_variable cv_{};
	std::queue<MYSQL*> idle_{};
	std::size_t total_{0};
	bool shutdown_{false};
};

}  // namespace oj
