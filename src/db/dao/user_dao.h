#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "db/mysql_pool.h"
#include "model/user.h"

namespace oj {

class UserDao {
 public:
	explicit UserDao(MySqlPool& pool);

	std::int64_t CreateUser(const std::string& username,
													const std::string& password_hash,
													const std::string& email,
													const std::string& role = "student",
													const std::string& status = "active");

	std::optional<User> GetByUsername(const std::string& username);
	std::optional<User> GetById(std::int64_t id);

	bool UpdateStatus(std::int64_t id, const std::string& status);

 private:
	MySqlPool& pool_;
};

}  // namespace oj
