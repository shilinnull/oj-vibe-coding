#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "db/mysql_pool.h"
#include "model/problem.h"

namespace oj {

class ProblemDao {
 public:
	explicit ProblemDao(MySqlPool& pool);

	std::int64_t Create(const Problem& p);
	bool Update(std::int64_t id, const Problem& p);
	std::optional<Problem> GetById(std::int64_t id);

	std::vector<Problem> List(int limit, int offset, const std::string& status_filter = "");

 private:
	MySqlPool& pool_;
};

}  // namespace oj
