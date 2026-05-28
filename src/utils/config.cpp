#include "utils/config.h"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>

#include <yaml-cpp/yaml.h>

namespace oj {
namespace {

std::string ToLower(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return s;
}

std::string ExpandEnvVars(const std::string& input) {
	// Replace occurrences of ${VAR} with getenv("VAR") (or empty string if not set).
	std::string out;
	out.reserve(input.size());

	for (std::size_t i = 0; i < input.size();) {
		if (input[i] == '$' && i + 1 < input.size() && input[i + 1] == '{') {
			std::size_t end = input.find('}', i + 2);
			if (end == std::string::npos) {
				out.append(input.substr(i));
				break;
			}

			std::string var = input.substr(i + 2, end - (i + 2));
			const char* value = std::getenv(var.c_str());
			if (value != nullptr) {
				out.append(value);
			}
			i = end + 1;
			continue;
		}

		out.push_back(input[i]);
		++i;
	}
	return out;
}

template <typename T>
T ScalarOrDefault(const YAML::Node& node, const T& default_value) {
	if (!node || node.IsNull()) {
		return default_value;
	}
	return node.as<T>();
}

std::string ScalarStringOrDefaultExpanded(const YAML::Node& node,
																					const std::string& default_value) {
	if (!node || node.IsNull()) {
		return default_value;
	}
	if (!node.IsScalar()) {
		throw std::runtime_error("config: expected scalar string");
	}
	return ExpandEnvVars(node.as<std::string>());
}

JudgeWorkerConfig ParseJudgeWorker(const YAML::Node& node) {
	JudgeWorkerConfig worker;
	if (auto host = node["host"]; host && host.IsScalar()) {
		worker.host = ScalarStringOrDefaultExpanded(host, worker.host);
	}
	worker.port = ScalarOrDefault<int>(node["port"], worker.port);
	if (auto health_path = node["health_path"]; health_path && health_path.IsScalar()) {
		worker.health_path = ScalarStringOrDefaultExpanded(health_path, worker.health_path);
	}
	if (auto judge_path = node["judge_path"]; judge_path && judge_path.IsScalar()) {
		worker.judge_path = ScalarStringOrDefaultExpanded(judge_path, worker.judge_path);
	}
	worker.timeout_ms = ScalarOrDefault<int>(node["timeout_ms"], worker.timeout_ms);
	return worker;
}

}  // namespace

LogLevel ParseLogLevel(const std::string& level) {
	const std::string lv = ToLower(level);
	if (lv == "debug") return LogLevel::Debug;
	if (lv == "info") return LogLevel::Info;
	if (lv == "warn" || lv == "warning") return LogLevel::Warn;
	if (lv == "error") return LogLevel::Error;
	throw std::runtime_error("config: unknown logging.level: " + level);
}

std::string ToString(LogLevel level) {
	switch (level) {
		case LogLevel::Debug:
			return "debug";
		case LogLevel::Info:
			return "info";
		case LogLevel::Warn:
			return "warn";
		case LogLevel::Error:
			return "error";
	}
	return "info";
}

AppConfig Config::LoadFromFile(const std::string& path) {
	YAML::Node root = YAML::LoadFile(path);
	AppConfig cfg;

	// server
	if (auto server = root["server"]) {
		cfg.server.host = ScalarStringOrDefaultExpanded(server["host"], cfg.server.host);
		cfg.server.port = ScalarOrDefault<int>(server["port"], cfg.server.port);
	}

	// mysql
	if (auto mysql = root["mysql"]) {
		cfg.mysql.host = ScalarStringOrDefaultExpanded(mysql["host"], cfg.mysql.host);
		cfg.mysql.port = ScalarOrDefault<int>(mysql["port"], cfg.mysql.port);
		cfg.mysql.user = ScalarStringOrDefaultExpanded(mysql["user"], cfg.mysql.user);
		cfg.mysql.password = ScalarStringOrDefaultExpanded(mysql["password"], cfg.mysql.password);
		cfg.mysql.database = ScalarStringOrDefaultExpanded(mysql["database"], cfg.mysql.database);

		if (auto pool = mysql["pool"]) {
			cfg.mysql.pool.max_connections =
					ScalarOrDefault<std::size_t>(pool["max_connections"], cfg.mysql.pool.max_connections);
			cfg.mysql.pool.connect_timeout_ms =
					ScalarOrDefault<int>(pool["connect_timeout_ms"], cfg.mysql.pool.connect_timeout_ms);
		}
	}

	// auth
	if (auto auth = root["auth"]) {
		if (auto jwt = auth["jwt"]) {
			cfg.auth.jwt.secret = ScalarStringOrDefaultExpanded(jwt["secret"], cfg.auth.jwt.secret);
			cfg.auth.jwt.expires_seconds =
					ScalarOrDefault<int>(jwt["expires_seconds"], cfg.auth.jwt.expires_seconds);
		}
	}

	// judge
	if (auto judge = root["judge"]) {
		cfg.judge.max_concurrency =
				ScalarOrDefault<int>(judge["max_concurrency"], cfg.judge.max_concurrency);
		cfg.judge.nsjail_config =
				ScalarStringOrDefaultExpanded(judge["nsjail_config"], cfg.judge.nsjail_config);
		cfg.judge.work_dir = ScalarStringOrDefaultExpanded(judge["work_dir"], cfg.judge.work_dir);
		cfg.judge.default_time_limit_ms =
				ScalarOrDefault<int>(judge["default_time_limit_ms"], cfg.judge.default_time_limit_ms);
		cfg.judge.default_memory_limit_kb =
				ScalarOrDefault<int>(judge["default_memory_limit_kb"], cfg.judge.default_memory_limit_kb);
		cfg.judge.request_timeout_ms =
				ScalarOrDefault<int>(judge["request_timeout_ms"], cfg.judge.request_timeout_ms);
		cfg.judge.retry_count = ScalarOrDefault<int>(judge["retry_count"], cfg.judge.retry_count);
		cfg.judge.health_check_interval_ms =
				ScalarOrDefault<int>(judge["health_check_interval_ms"], cfg.judge.health_check_interval_ms);

		if (auto workers = judge["workers"]; workers && workers.IsSequence()) {
			for (const auto& worker_node : workers) {
				cfg.judge_workers.push_back(ParseJudgeWorker(worker_node));
			}
		}
	}

	// logging
	if (auto logging = root["logging"]) {
		if (auto level = logging["level"]; level && level.IsScalar()) {
			cfg.logging.level = ParseLogLevel(level.as<std::string>());
		}
		cfg.logging.to_stdout = ScalarOrDefault<bool>(logging["to_stdout"], cfg.logging.to_stdout);
		cfg.logging.file = ScalarStringOrDefaultExpanded(logging["file"], cfg.logging.file);
		// max_file_size in MB (default 10 MB)
		auto mfs = ScalarOrDefault<std::size_t>(logging["max_file_size"], 10);
		cfg.logging.max_file_size = mfs * 1024 * 1024;
	}

	return cfg;
}

}  // namespace oj
