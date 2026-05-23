#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace oj {

struct Problem {
	std::int64_t id{0};
	std::string title;
	std::string description;
	std::string difficulty{"medium"};
	int time_limit_ms{1000};
	int memory_limit_kb{262144};
	std::string status{"draft"};
	std::int64_t created_by{0};
	std::string created_at;
	std::string updated_at;
};

void to_json(nlohmann::json& j, const Problem& p);
void from_json(const nlohmann::json& j, Problem& p);

}  // namespace oj
