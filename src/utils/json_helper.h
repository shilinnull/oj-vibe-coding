#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace oj {

std::optional<nlohmann::json> TryParseJson(const std::string& text, std::string* error);
std::string DumpJson(const nlohmann::json& j, int indent = 2);

}  // namespace oj
