#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <string>

#include "utils/logger.h"

namespace {

std::string ReadAll(const std::string& path) {
  std::ifstream ifs(path);
  return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

}  // namespace

TEST(LoggerTest, WritesToFileAndFiltersLevel) {
  const std::string log_path = "/tmp/oj_logger_test.log";
  std::remove(log_path.c_str());

  oj::LoggingConfig cfg;
  cfg.level = oj::LogLevel::Info;
  cfg.to_stdout = false;
  cfg.file = log_path;
  oj::Logger::Instance().Init(cfg);

  OJ_LOG_DEBUG("debug_should_not_show");
  OJ_LOG_INFO("hello_info");
  oj::Logger::Instance().Shutdown();

  const std::string content = ReadAll(log_path);
  EXPECT_EQ(content.find("hello_info") != std::string::npos, true);
  EXPECT_EQ(content.find("debug_should_not_show") == std::string::npos, true);
}
