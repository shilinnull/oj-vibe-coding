#pragma once

#include <memory>

#include <httplib.h>

#include "router.h"

namespace oj {

class HttpServer {
 public:
	HttpServer();

	Router& router() { return router_; }
	void MountRoutes();

	// 阻塞启动
	void Listen(const char* host, int port);

 private:
	httplib::Server server_{};
	Router router_{};
};

}  // namespace oj
