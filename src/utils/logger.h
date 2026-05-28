#pragma once

#include <cstdio>
#include <mutex>
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

	void CreateNewLogFile();
	void RotateLogFile();
	static void GetTimestampParts(std::string& date_str, std::string& full_ts_str);

	mutable std::mutex mu_{};
	LoggingConfig cfg_{};
	bool initialized_{false};

	// 文件日志相关
	std::string log_dir_{};            // 日志目录路径
	std::string current_log_path_{};   // 当前日志文件的完整路径
	std::string current_date_str_{};   // 当前文件的日期部分，如 "2026_5_28"
	std::size_t current_file_size_{0}; // 当前文件已写入字节数
	FILE* file_handle_{nullptr};
};

}  // namespace oj

#define OJ_LOG_DEBUG(msg) ::oj::Logger::Instance().Log(::oj::LogLevel::Debug, __FILE__, __LINE__, (msg))
#define OJ_LOG_INFO(msg) ::oj::Logger::Instance().Log(::oj::LogLevel::Info, __FILE__, __LINE__, (msg))
#define OJ_LOG_WARN(msg) ::oj::Logger::Instance().Log(::oj::LogLevel::Warn, __FILE__, __LINE__, (msg))
#define OJ_LOG_ERROR(msg) ::oj::Logger::Instance().Log(::oj::LogLevel::Error, __FILE__, __LINE__, (msg))
