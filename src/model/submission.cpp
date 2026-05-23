#include "model/submission.h"

namespace oj {

void to_json(nlohmann::json& j, const Submission& s) {
	j = nlohmann::json{{"id", s.id},
										 {"user_id", s.user_id},
										 {"problem_id", s.problem_id},
										 {"language_id", s.language_id},
										 {"source_code", s.source_code},
										 {"mode", s.mode},
										 {"status", s.status},
										 {"result_json", s.result_json},
										 {"time_ms", s.time_ms},
										 {"memory_kb", s.memory_kb},
										 {"created_at", s.created_at}};
}

void from_json(const nlohmann::json& j, Submission& s) {
	s.id = j.value("id", 0LL);
	s.user_id = j.value("user_id", 0LL);
	s.problem_id = j.value("problem_id", 0LL);
	s.language_id = j.value("language_id", 0);
	s.source_code = j.value("source_code", "");
	s.mode = j.value("mode", "submit");
	s.status = j.value("status", "pending");
	s.result_json = j.value("result_json", "");
	s.time_ms = j.value("time_ms", 0);
	s.memory_kb = j.value("memory_kb", 0);
	s.created_at = j.value("created_at", "");
}

}  // namespace oj
