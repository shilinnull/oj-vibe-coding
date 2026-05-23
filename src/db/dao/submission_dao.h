#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "db/mysql_pool.h"
#include "model/submission.h"

namespace oj {

class SubmissionDao {
 public:
	explicit SubmissionDao(MySqlPool& pool);

	std::int64_t Create(const Submission& s);
	std::optional<Submission> GetById(std::int64_t id);
	std::vector<Submission> ListByUser(std::int64_t user_id, int limit, int offset);

	bool UpdateResult(std::int64_t id,
										const std::string& status,
										const std::string& result_json,
										int time_ms,
										int memory_kb);

 private:
	MySqlPool& pool_;
};

}  // namespace oj
