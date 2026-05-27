#pragma once

#include "db/mysql_pool.h"
#include "net/router.h"

namespace oj {

namespace handler {

void RegisterProblemRoutes(Router& router, MySqlPool& pool);

}  // namespace handler

}  // namespace oj
