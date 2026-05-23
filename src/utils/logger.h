#pragma once

#include <mutex>
#include <optional>
#include <string>

#include "utils/config.h"

namespace oj {

class Logger {
 public:
	static Logger& Instance();

	void Init(const LoggingConfig& cfg);
	void Shutdown();

	void Log(LogLevel level, const char* file, int line, const std::string& message);

	LogLevel level() const;

 private:
	Logger() = default;

	bool ShouldLog(LogLevel level) const;
	static std::string FormatTimestamp();
	static const char* LevelString(LogLevel level);

	mutable std::mutex mu_{};
	LoggingConfig cfg_{};
	bool initialized_{false};
	std::optional<std::string> file_path_{};
	void* file_handle_{nullptr};
};

}  // namespace oj

#define OJ_LOG_DEBUG(msg) ::oj::Logger::Instance().Log(::oj::LogLevel::Debug, __FILE__, __LINE__, (msg))
#define OJ_LOG_INFO(msg) ::oj::Logger::Instance().Log(::oj::LogLevel::Info, __FILE__, __LINE__, (msg))
#define OJ_LOG_WARN(msg) ::oj::Logger::Instance().Log(::oj::LogLevel::Warn, __FILE__, __LINE__, (msg))
#define OJ_LOG_ERROR(msg) ::oj::Logger::Instance().Log(::oj::LogLevel::Error, __FILE__, __LINE__, (msg))
