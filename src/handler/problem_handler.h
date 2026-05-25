#pragma once

#include "db/mysql_pool.h"
#include "router.h"

namespace oj {

namespace handler {

void RegisterProblemRoutes(Router& router, MySqlPool& pool);

}  // namespace handler

}  // namespace oj
