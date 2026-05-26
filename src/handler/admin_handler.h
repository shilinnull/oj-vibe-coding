#pragma once

#include <string>

#include "db/mysql_pool.h"
#include "router.h"

namespace oj {

namespace handler {

void RegisterAdminRoutes(Router& router, const std::string& jwt_secret, MySqlPool& pool);

}  // namespace handler

}  // namespace oj
