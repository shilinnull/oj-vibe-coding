#pragma once

#include <cstdint>
#include <string>
#include <vector>

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
	int request_timeout_ms{3000};
	int retry_count{3};
	int health_check_interval_ms{5000};
};

struct JudgeWorkerConfig {
	std::string host{"127.0.0.1"};
	int port{9001};
	std::string health_path{"/healthz"};
	std::string judge_path{"/judge"};
	int timeout_ms{3000};
};

struct LoggingConfig {
	LogLevel level{LogLevel::Info};
	bool to_stdout{true};
	std::string file{};  // 日志目录路径（非空时启用文件日志）
	std::size_t max_file_size{10 * 1024 * 1024};  // 单文件上限，默认 10MB
};

struct AppConfig {
	ServerConfig server{};
	MysqlConfig mysql{};
	AuthConfig auth{};
	JudgeConfig judge{};
	LoggingConfig logging{};
	std::vector<JudgeWorkerConfig> judge_workers{};
};

class Config {
 public:
	static AppConfig LoadFromFile(const std::string& path);
};

LogLevel ParseLogLevel(const std::string& level);
std::string ToString(LogLevel level);

}  // namespace oj
