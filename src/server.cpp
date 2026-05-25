#include "server.h"

#include "utils/logger.h"
#include "handler/auth_handler.h"

namespace oj {

HttpServer::HttpServer(const oj::AppConfig& cfg) {
	// 最小健康检查路由
	router_.Get("/healthz", [](const httplib::Request&, httplib::Response& res) {
		res.set_content("ok", "text/plain; charset=utf-8");
	});

	// 创建数据库连接池并注册 auth 路由
	try {
		pool_ = std::make_unique<MySqlPool>(cfg.mysql);
		handler::RegisterAuthRoutes(router_, cfg.auth.jwt.secret, *pool_);
	} catch (const std::exception& e) {
		OJ_LOG_ERROR(std::string("failed init db or auth routes: ") + e.what());
		throw;
	}
}

void HttpServer::MountRoutes() { router_.Mount(server_); }

void HttpServer::Listen(const char* host, int port) {
	MountRoutes();
	OJ_LOG_INFO(std::string("listening on ") + host + ":" + std::to_string(port));
	server_.listen(host, port);
}

}  // namespace oj
