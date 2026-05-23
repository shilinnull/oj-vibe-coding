#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "db/mysql_pool.h"
#include "model/test_case.h"

namespace oj {

class TestCaseDao {
 public:
	explicit TestCaseDao(MySqlPool& pool);

	std::int64_t Add(const TestCase& tc);
	std::vector<TestCase> ListByProblem(std::int64_t problem_id, bool only_sample);

 private:
	MySqlPool& pool_;
};

}  // namespace oj
