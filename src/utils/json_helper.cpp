#include "utils/json_helper.h"

namespace oj {

std::optional<nlohmann::json> TryParseJson(const std::string& text, std::string* error) {
	try {
		return nlohmann::json::parse(text);
	} catch (const std::exception& e) {
		if (error) {
			*error = e.what();
		}
		return std::nullopt;
	}
}

std::string DumpJson(const nlohmann::json& j, int indent) {
	return j.dump(indent);
}

}  // namespace oj
