#pragma once

#include <memory>

#include <httplib.h>

#include "router.h"
#include "utils/config.h"
#include "db/mysql_pool.h"

namespace oj {

// HttpServer 负责把“配置 + 路由 + 数据库连接池”组装成一个可启动的 HTTP 服务。
class HttpServer {
 public:
	explicit HttpServer(const AppConfig& cfg);

	// 业务代码通过这个入口注册路由，启动前统一 Mount 到 server。
	Router& router() { return router_; }
	void MountRoutes();

	// 阻塞启动服务，直到进程退出或 server 停止。
	void Listen(const char* host, int port);

 private:
	httplib::Server server_{};
	Router router_{};
	std::unique_ptr<MySqlPool> pool_{};
};

}  // namespace oj
