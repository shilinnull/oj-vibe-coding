#include "server.h"

#include "utils/logger.h"

namespace oj {

HttpServer::HttpServer() {
	// 最小健康检查路由
	router_.Get("/healthz", [](const httplib::Request&, httplib::Response& res) {
		res.set_content("ok", "text/plain; charset=utf-8");
	});
}

void HttpServer::MountRoutes() { router_.Mount(server_); }

void HttpServer::Listen(const char* host, int port) {
	MountRoutes();
	OJ_LOG_INFO(std::string("listening on ") + host + ":" + std::to_string(port));
	server_.listen(host, port);
}

}  // namespace oj
