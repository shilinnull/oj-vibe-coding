#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace oj {

struct Language {
	int id{0};
	std::string name;
	std::string extension;
	std::string compile_cmd;
	std::string run_cmd;
	bool enabled{true};
	std::string created_at;
};

void to_json(nlohmann::json& j, const Language& l);
void from_json(const nlohmann::json& j, Language& l);

}  // namespace oj
