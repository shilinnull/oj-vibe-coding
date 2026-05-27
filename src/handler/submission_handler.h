#pragma once

#include "db/mysql_pool.h"
#include "judge/judge_manager.h"
#include "net/router.h"

namespace oj {

namespace handler {

void RegisterSubmissionRoutes(Router& router, MySqlPool& pool, JudgeManager& judge_manager);

}  // namespace handler

}  // namespace oj
