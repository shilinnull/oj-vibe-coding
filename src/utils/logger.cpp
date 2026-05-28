#include "utils/logger.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace oj {
namespace {

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
		CloseFile(file_handle_);
		file_handle_ = nullptr;
	}

	log_dir_.clear();
	current_log_path_.clear();
	current_date_str_.clear();
	current_file_size_ = 0;

	if (!cfg.file.empty()) {
		log_dir_ = cfg.file;
		std::filesystem::create_directories(log_dir_);
		CreateNewLogFile();
		if (file_handle_ == nullptr) {
			throw std::runtime_error("logger: failed to create log file in directory: " + cfg.file);
		}
	}

	initialized_ = true;
}

void Logger::Shutdown() {
	std::lock_guard<std::mutex> lock(mu_);
	if (file_handle_ != nullptr) {
		CloseFile(file_handle_);
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

void Logger::GetTimestampParts(std::string& date_str, std::string& full_ts_str) {
	using namespace std::chrono;
	const auto now = system_clock::now();
	const std::time_t t = system_clock::to_time_t(now);
	std::tm tm{};
#if defined(_WIN32)
	localtime_s(&tm, &t);
#else
	localtime_r(&t, &tm);
#endif

	int y = tm.tm_year + 1900;
	int m = tm.tm_mon + 1;
	int d = tm.tm_mday;
	int h = tm.tm_hour;
	int min = tm.tm_min;
	int s = tm.tm_sec;

	std::ostringstream date_oss;
	date_oss << y << '_' << m << '_' << d;
	date_str = date_oss.str();

	std::ostringstream full_oss;
	full_oss << "log_" << y << '_' << m << '_' << d << ".txt";
	full_ts_str = full_oss.str();
}

void Logger::CreateNewLogFile() {
	if (file_handle_ != nullptr) {
		CloseFile(file_handle_);
		file_handle_ = nullptr;
	}

	GetTimestampParts(current_date_str_, current_log_path_);
	current_log_path_ = log_dir_ + "/" + current_log_path_;

	file_handle_ = std::fopen(current_log_path_.c_str(), "a");
	if (file_handle_ == nullptr) {
		return;
	}
	current_file_size_ = 0;
}

void Logger::RotateLogFile() {
	if (file_handle_ != nullptr) {
		CloseFile(file_handle_);
		file_handle_ = nullptr;
	}

	std::string archive_basename = log_dir_ + "/log_" + current_date_str_;
	std::string archive_path = archive_basename + ".tar.gz";
	int seq = 0;
	while (std::filesystem::exists(archive_path)) {
		++seq;
		archive_path = archive_basename + "." + std::to_string(seq) + ".tar.gz";
	}

	auto filename = std::filesystem::path(current_log_path_).filename().string();

	std::string cmd = "tar -czf '" + archive_path + "' -C '" + log_dir_ + "' '" + filename
	                  + "' && rm -f '" + current_log_path_ + "'";

	int ret = std::system(cmd.c_str());
	(void)ret;

	CreateNewLogFile();
}

void Logger::Log(LogLevel level, const char* file, int line, const std::string& message) {
	std::lock_guard<std::mutex> lock(mu_);
	if (!initialized_) {
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
		std::fwrite(line_out.data(), 1, line_out.size(), file_handle_);
		std::fflush(file_handle_);

		current_file_size_ += line_out.size();
		if (current_file_size_ >= cfg_.max_file_size) {
			RotateLogFile();
		}
	}
}

}  // namespace oj
