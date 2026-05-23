#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace oj {

struct Submission {
	std::int64_t id{0};
	std::int64_t user_id{0};
	std::int64_t problem_id{0};
	int language_id{0};
	std::string source_code;
	std::string mode{"submit"};
	std::string status{"pending"};
	std::string result_json;  // stored as JSON text
	int time_ms{0};
	int memory_kb{0};
	std::string created_at;
};

void to_json(nlohmann::json& j, const Submission& s);
void from_json(const nlohmann::json& j, Submission& s);

}  // namespace oj
