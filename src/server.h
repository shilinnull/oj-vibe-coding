#pragma once

#include <memory>

#include <httplib.h>

#include "router.h"
#include "utils/config.h"
#include "db/mysql_pool.h"

namespace oj {

class HttpServer {
 public:
 	 explicit HttpServer(const AppConfig& cfg);

 	Router& router() { return router_; }
 	void MountRoutes();

 	// 阻塞启动
 	void Listen(const char* host, int port);

 private:
 	httplib::Server server_{};
 	Router router_{};
	std::unique_ptr<MySqlPool> pool_{};
};

}  // namespace oj
