#include "server.h"

#include <exception>

#include "utils/config.h"
#include "utils/logger.h"

int main(int argc, char** argv) {
	(void)argc;
	(void)argv;

	try {
		auto cfg = oj::Config::LoadFromFile("./config/config.yaml");
		oj::Logger::Instance().Init(cfg.logging);

		oj::HttpServer server;
		server.Listen(cfg.server.host.c_str(), cfg.server.port);
		return 0;
	} catch (const std::exception& e) {
		// 如果 logger 未初始化，stderr 输出兜底
		std::fprintf(stderr, "fatal: %s\n", e.what());
		return 1;
	}
}
