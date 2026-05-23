#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace oj {

struct User {
	std::int64_t id{0};
	std::string username;
	std::string password;  // hash
	std::string email;
	std::string role{"student"};
	std::string status{"active"};
	std::string created_at;
	std::string updated_at;
};

void to_json(nlohmann::json& j, const User& u);
void from_json(const nlohmann::json& j, User& u);

}  // namespace oj
