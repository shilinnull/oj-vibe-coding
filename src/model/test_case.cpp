#include "model/test_case.h"

namespace oj {

void to_json(nlohmann::json& j, const TestCase& tc) {
	j = nlohmann::json{{"id", tc.id},
										 {"problem_id", tc.problem_id},
										 {"is_sample", tc.is_sample},
										 {"input", tc.input},
										 {"output", tc.output},
										 {"sort_order", tc.sort_order},
										 {"created_at", tc.created_at}};
}

void from_json(const nlohmann::json& j, TestCase& tc) {
	tc.id = j.value("id", 0LL);
	tc.problem_id = j.value("problem_id", 0LL);
	tc.is_sample = j.value("is_sample", false);
	tc.input = j.value("input", "");
	tc.output = j.value("output", "");
	tc.sort_order = j.value("sort_order", 0);
	tc.created_at = j.value("created_at", "");
}

}  // namespace oj
