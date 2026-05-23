#include "utils/logger.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace oj {
namespace {

FILE* OpenAppendFile(const std::string& path) {
	FILE* f = std::fopen(path.c_str(), "a");
	return f;
}

void CloseFile(FILE* f) {
	if (f != nullptr) {
		std::fclose(f);
	}
}

}  // namespace

Logger& Logger::Instance() {
	static Logger inst;
	return inst;
}

void Logger::Init(const LoggingConfig& cfg) {
	std::lock_guard<std::mutex> lock(mu_);

	cfg_ = cfg;

	if (file_handle_ != nullptr) {
		CloseFile(static_cast<FILE*>(file_handle_));
		file_handle_ = nullptr;
	}

	file_path_.reset();
	if (!cfg.file.empty()) {
		file_path_ = cfg.file;
		file_handle_ = OpenAppendFile(cfg.file);
		if (file_handle_ == nullptr) {
			throw std::runtime_error("logger: failed to open log file: " + cfg.file);
		}
	}

	initialized_ = true;
}

void Logger::Shutdown() {
	std::lock_guard<std::mutex> lock(mu_);
	if (file_handle_ != nullptr) {
		CloseFile(static_cast<FILE*>(file_handle_));
		file_handle_ = nullptr;
	}
	initialized_ = false;
}

LogLevel Logger::level() const {
	std::lock_guard<std::mutex> lock(mu_);
	return cfg_.level;
}

bool Logger::ShouldLog(LogLevel level) const {
	return static_cast<int>(level) >= static_cast<int>(cfg_.level);
}

const char* Logger::LevelString(LogLevel level) {
	switch (level) {
		case LogLevel::Debug:
			return "DEBUG";
		case LogLevel::Info:
			return "INFO";
		case LogLevel::Warn:
			return "WARN";
		case LogLevel::Error:
			return "ERROR";
	}
	return "INFO";
}

std::string Logger::FormatTimestamp() {
	using namespace std::chrono;
	const auto now = system_clock::now();
	const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
	const std::time_t t = system_clock::to_time_t(now);
	std::tm tm{};
#if defined(_WIN32)
	localtime_s(&tm, &t);
#else
	localtime_r(&t, &tm);
#endif

	std::ostringstream oss;
	oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << '.' << std::setw(3) << std::setfill('0')
			<< ms.count();
	return oss.str();
}

void Logger::Log(LogLevel level, const char* file, int line, const std::string& message) {
	std::lock_guard<std::mutex> lock(mu_);
	if (!initialized_) {
		// Default behavior if not initialized: log to stdout at INFO.
		cfg_.level = LogLevel::Info;
		cfg_.to_stdout = true;
	}

	if (!ShouldLog(level)) {
		return;
	}

	std::ostringstream oss;
	oss << FormatTimestamp() << " [" << LevelString(level) << "] " << file << ':' << line << " "
			<< message << '\n';
	const std::string line_out = oss.str();

	if (cfg_.to_stdout) {
		std::fwrite(line_out.data(), 1, line_out.size(), stdout);
		std::fflush(stdout);
	}

	if (file_handle_ != nullptr) {
		FILE* f = static_cast<FILE*>(file_handle_);
		std::fwrite(line_out.data(), 1, line_out.size(), f);
		std::fflush(f);
	}
}

}  // namespace oj
