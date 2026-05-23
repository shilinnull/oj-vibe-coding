#include "model/problem.h"

namespace oj {

void to_json(nlohmann::json& j, const Problem& p) {
	j = nlohmann::json{{"id", p.id},
										 {"title", p.title},
										 {"description", p.description},
										 {"difficulty", p.difficulty},
										 {"time_limit_ms", p.time_limit_ms},
										 {"memory_limit_kb", p.memory_limit_kb},
										 {"status", p.status},
										 {"created_by", p.created_by},
										 {"created_at", p.created_at},
										 {"updated_at", p.updated_at}};
}

void from_json(const nlohmann::json& j, Problem& p) {
	p.id = j.value("id", 0LL);
	p.title = j.value("title", "");
	p.description = j.value("description", "");
	p.difficulty = j.value("difficulty", "medium");
	p.time_limit_ms = j.value("time_limit_ms", 1000);
	p.memory_limit_kb = j.value("memory_limit_kb", 262144);
	p.status = j.value("status", "draft");
	p.created_by = j.value("created_by", 0LL);
	p.created_at = j.value("created_at", "");
	p.updated_at = j.value("updated_at", "");
}

}  // namespace oj
