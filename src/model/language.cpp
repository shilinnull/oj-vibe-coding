#include "model/language.h"

namespace oj {

void to_json(nlohmann::json& j, const Language& l) {
	j = nlohmann::json{{"id", l.id},
										 {"name", l.name},
										 {"extension", l.extension},
										 {"compile_cmd", l.compile_cmd},
										 {"run_cmd", l.run_cmd},
										 {"enabled", l.enabled},
										 {"created_at", l.created_at}};
}

void from_json(const nlohmann::json& j, Language& l) {
	l.id = j.value("id", 0);
	l.name = j.value("name", "");
	l.extension = j.value("extension", "");
	l.compile_cmd = j.value("compile_cmd", "");
	l.run_cmd = j.value("run_cmd", "");
	l.enabled = j.value("enabled", true);
	l.created_at = j.value("created_at", "");
}

}  // namespace oj
