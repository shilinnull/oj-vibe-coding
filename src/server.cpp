#include "server.h"

#include <filesystem>

#include "utils/logger.h"
#include "handler/auth_handler.h"
#include "handler/problem_handler.h"
#include "handler/submission_handler.h"

namespace oj {

namespace {

std::string ResolveExistingPath(std::initializer_list<const char*> candidates) {
	for (const char* candidate : candidates) {
		std::error_code ec;
		if (std::filesystem::exists(candidate, ec)) {
			return std::filesystem::path(candidate).lexically_normal().string();
		}
	}
	return std::string();
}

}  // namespace

HttpServer::HttpServer(const oj::AppConfig& cfg) {
	// 健康检查接口，给部署环境和容器探活用。
	router_.Get("/healthz", [](const httplib::Request&, httplib::Response& res) {
		res.set_content("ok", "text/plain; charset=utf-8");
	});

	const std::string web_dir = ResolveExistingPath({"./web", "../web", "../../web"});
	if (!web_dir.empty()) {
		if (!server_.set_mount_point("/", web_dir)) {
			OJ_LOG_WARN(std::string("failed to mount static web dir: ") + web_dir);
		} else {
			OJ_LOG_INFO(std::string("mounted static web dir: ") + web_dir);
		}
	} else {
		OJ_LOG_WARN("web directory not found; static frontend is unavailable");
	}

	// 服务启动时先初始化数据库连接池，再把认证相关路由挂进去。
	try {
		pool_ = std::make_unique<MySqlPool>(cfg.mysql);
		handler::RegisterAuthRoutes(router_, cfg.auth.jwt.secret, *pool_);
		handler::RegisterProblemRoutes(router_, *pool_);
		handler::RegisterSubmissionRoutes(router_, *pool_);
	} catch (const std::exception& e) {
		OJ_LOG_ERROR(std::string("failed init db or auth routes: ") + e.what());
		throw;
	}
}

void HttpServer::MountRoutes() {
	// 统一把业务路由挂到 httplib server 上，避免各处零散注册。
	router_.Mount(server_);
}

void HttpServer::Listen(const char* host, int port) {
	MountRoutes();
	// listen() 会阻塞，所以这里就是服务真正开始接收请求的地方。
	OJ_LOG_INFO(std::string("listening on ") + host + ":" + std::to_string(port));
	server_.listen(host, port);
}

}  // namespace oj
