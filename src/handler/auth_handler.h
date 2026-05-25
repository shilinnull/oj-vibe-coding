#pragma once

#include <httplib.h>
#include "router.h"
#include "db/mysql_pool.h"

namespace oj {

namespace handler {
    void RegisterAuthRoutes(Router& router, const std::string& jwt_secret, MySqlPool& pool);
}

}  // namespace oj
