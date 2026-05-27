#include "server/server.h"

#include <exception>
#include <filesystem>

namespace {

std::string ResolveConfigPath() {
	for (const char* candidate : {"./config/config.yaml", "../config/config.yaml", "../../config/config.yaml"}) {
		std::error_code ec;
		if (std::filesystem::exists(candidate, ec)) {
			return std::filesystem::path(candidate).lexically_normal().string();
		}
	}
	return "./config/config.yaml";
}

}  // namespace

#include "utils/config.h"
#include "utils/logger.h"

int main(int argc, char** argv) {
	(void)argc;
	(void)argv;

	try {
		// 启动顺序：先读配置，再初始化日志，最后构造并启动 HTTP 服务。
		auto cfg = oj::Config::LoadFromFile(ResolveConfigPath());
		oj::Logger::Instance().Init(cfg.logging);

		oj::HttpServer server(cfg);
		server.Listen(cfg.server.host.c_str(), cfg.server.port);
		return 0;
	} catch (const std::exception& e) {
		// 如果 logger 未初始化，stderr 输出兜底
		std::fprintf(stderr, "fatal: %s\n", e.what());
		return 1;
	}
}
