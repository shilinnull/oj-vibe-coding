#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace oj {

struct TestCase {
	std::int64_t id{0};
	std::int64_t problem_id{0};
	bool is_sample{false};
	std::string input;
	std::string output;
	int sort_order{0};
	std::string created_at;
};

void to_json(nlohmann::json& j, const TestCase& tc);
void from_json(const nlohmann::json& j, TestCase& tc);

}  // namespace oj
