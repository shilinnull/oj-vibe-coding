#pragma once

#include <cstdint>
#include <string>

namespace oj {

enum class LogLevel {
	Debug = 0,
	Info = 1,
	Warn = 2,
	Error = 3,
};

struct ServerConfig {
	std::string host{"0.0.0.0"};
	int port{8080};
};

struct MysqlPoolConfig {
	std::size_t max_connections{10};
	int connect_timeout_ms{2000};
};

struct MysqlConfig {
	std::string host{"127.0.0.1"};
	int port{3306};
	std::string user{"root"};
	std::string password{};
	std::string database{"oj"};
	MysqlPoolConfig pool{};
};

struct AuthJwtConfig {
	std::string secret{""};
	int expires_seconds{3600};
};

struct AuthConfig {
	AuthJwtConfig jwt{};
};

struct JudgeConfig {
	int max_concurrency{4};
	std::string nsjail_config{"./config/nsjail.cfg"};
	std::string work_dir{"./run/judge"};
	int default_time_limit_ms{1000};
	int default_memory_limit_kb{262144};
};

struct LoggingConfig {
	LogLevel level{LogLevel::Info};
	bool to_stdout{true};
	std::string file{};
};

struct AppConfig {
	ServerConfig server{};
	MysqlConfig mysql{};
	AuthConfig auth{};
	JudgeConfig judge{};
	LoggingConfig logging{};
};

class Config {
 public:
	static AppConfig LoadFromFile(const std::string& path);
};

LogLevel ParseLogLevel(const std::string& level);
std::string ToString(LogLevel level);

}  // namespace oj
