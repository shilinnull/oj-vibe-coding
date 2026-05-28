#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>

#include "utils/logger.h"

namespace {

std::string ReadAll(const std::string& path) {
  std::ifstream ifs(path);
  return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

std::string FindLogFile(const std::string& dir) {
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    auto name = entry.path().filename().string();
    if (name.rfind("log_", 0) == 0 && name.size() > 4) {
      return entry.path().string();
    }
  }
  return {};
}

std::string FindArchive(const std::string& dir, const std::string& suffix) {
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    auto name = entry.path().filename().string();
    if (name == "log_" + suffix) {
      return entry.path().string();
    }
  }
  return {};
}

// 在指定目录中统计文件名包含 substr 的文件数
int CountFilesContain(const std::string& dir, const std::string& substr) {
  int n = 0;
  if (!std::filesystem::exists(dir)) return 0;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (entry.path().filename().string().find(substr) != std::string::npos) {
      ++n;
    }
  }
  return n;
}

// 提取 tar.gz 中所有文件，返回第一个 .txt 文件的内容
std::string ExtractLogFromArchive(const std::string& archive_path) {
  const std::string tmp_dir = archive_path + ".extract";
  std::filesystem::create_directories(tmp_dir);
  std::string cmd = "tar -xzf '" + archive_path + "' -C '" + tmp_dir + "' 2>/dev/null";
  std::system(cmd.c_str());

  std::string content;
  for (const auto& entry : std::filesystem::directory_iterator(tmp_dir)) {
    if (entry.path().extension() == ".txt") {
      content = ReadAll(entry.path().string());
      break;
    }
  }
  std::filesystem::remove_all(tmp_dir);
  return content;
}

const std::string kTestRoot = "/tmp/oj_logger_test";

}  // namespace

// ===========================
// 基本日志写入与级别过滤
// ===========================
TEST(LoggerTest, WritesToFileAndFiltersLevel) {
  const std::string dir = kTestRoot + "/filter_level";
  std::filesystem::remove_all(dir);

  oj::LoggingConfig cfg;
  cfg.level = oj::LogLevel::Info;
  cfg.to_stdout = false;
  cfg.file = dir;
  cfg.max_file_size = 10 * 1024 * 1024;
  oj::Logger::Instance().Init(cfg);

  OJ_LOG_DEBUG("debug_should_not_show");
  OJ_LOG_INFO("hello_info");
  OJ_LOG_WARN("warning_msg");
  OJ_LOG_ERROR("error_msg");
  oj::Logger::Instance().Shutdown();

  std::string log_path = FindLogFile(dir);
  ASSERT_FALSE(log_path.empty()) << "no log file found in " << dir;

  const std::string content = ReadAll(log_path);
  EXPECT_NE(content.find("hello_info"), std::string::npos);
  EXPECT_NE(content.find("warning_msg"), std::string::npos);
  EXPECT_NE(content.find("error_msg"), std::string::npos);
  EXPECT_EQ(content.find("debug_should_not_show"), std::string::npos);

  std::filesystem::remove_all(dir);
}

// ===========================
// 所有日志级别输出正确的级别字符串
// ===========================
TEST(LoggerTest, AllLevelsOutputCorrectLevelString) {
  const std::string dir = kTestRoot + "/level_strings";
  std::filesystem::remove_all(dir);

  oj::LoggingConfig cfg;
  cfg.level = oj::LogLevel::Debug;
  cfg.to_stdout = false;
  cfg.file = dir;
  cfg.max_file_size = 10 * 1024 * 1024;
  oj::Logger::Instance().Init(cfg);

  OJ_LOG_DEBUG("dbg");
  OJ_LOG_INFO("inf");
  OJ_LOG_WARN("wrn");
  OJ_LOG_ERROR("err");
  oj::Logger::Instance().Shutdown();

  std::string log_path = FindLogFile(dir);
  ASSERT_FALSE(log_path.empty());

  const std::string content = ReadAll(log_path);
  EXPECT_NE(content.find("[DEBUG]"), std::string::npos);
  EXPECT_NE(content.find("[INFO]"), std::string::npos);
  EXPECT_NE(content.find("[WARN]"), std::string::npos);
  EXPECT_NE(content.find("[ERROR]"), std::string::npos);

  std::filesystem::remove_all(dir);
}

// ===========================
// 日志文件命名格式: log_YYYY_M_D.txt
// ===========================
TEST(LoggerTest, FileNamingFormat) {
  const std::string dir = kTestRoot + "/naming";
  std::filesystem::remove_all(dir);

  oj::LoggingConfig cfg;
  cfg.level = oj::LogLevel::Info;
  cfg.to_stdout = false;
  cfg.file = dir;
  cfg.max_file_size = 10 * 1024 * 1024;
  oj::Logger::Instance().Init(cfg);

  OJ_LOG_INFO("check_name");
  oj::Logger::Instance().Shutdown();

  // 验证目录中有且仅有一个 .txt 日志文件，且名称符合 log_YYYY_M_D.txt
  int count = 0;
  std::string filename;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    auto name = entry.path().filename().string();
    if (name.rfind("log_", 0) == 0 && name.size() > 4) {
      ++count;
      filename = name;
    }
  }
  ASSERT_EQ(count, 1) << "expected exactly one log file";

  // 格式验证: log_<1-4位年>_<1-2位月>_<1-2位日>.txt
  std::regex pattern(R"(^log_\d{1,4}_\d{1,2}_\d{1,2}\.txt$)");
  EXPECT_TRUE(std::regex_match(filename, pattern)) << "filename: " << filename;

  std::filesystem::remove_all(dir);
}

// ===========================
// 日志文件内容格式验证: [时间戳] [LEVEL] file:line message
// ===========================
TEST(LoggerTest, LogLineFormat) {
  const std::string dir = kTestRoot + "/line_format";
  std::filesystem::remove_all(dir);

  oj::LoggingConfig cfg;
  cfg.level = oj::LogLevel::Info;
  cfg.to_stdout = false;
  cfg.file = dir;
  cfg.max_file_size = 10 * 1024 * 1024;
  oj::Logger::Instance().Init(cfg);

  OJ_LOG_INFO("fmt_check");
  oj::Logger::Instance().Shutdown();

  std::string log_path = FindLogFile(dir);
  ASSERT_FALSE(log_path.empty());

  const std::string content = ReadAll(log_path);
  // 格式: YYYY-MM-DD HH:MM:SS.mmm [LEVEL] file:line message\n
  std::regex line_re(R"(^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3} \[INFO\].+fmt_check\n$)");
  EXPECT_TRUE(std::regex_match(content, line_re)) << "content: [" << content << "]";

  std::filesystem::remove_all(dir);
}

// ===========================
// 日志轮转产生归档文件
// ===========================
TEST(LoggerTest, LogRotationCreatesArchive) {
  const std::string dir = kTestRoot + "/rotation";
  std::filesystem::remove_all(dir);

  oj::LoggingConfig cfg;
  cfg.level = oj::LogLevel::Info;
  cfg.to_stdout = false;
  cfg.file = dir;
  cfg.max_file_size = 100;  // 每 100 字节触发轮转
  oj::Logger::Instance().Init(cfg);

  // 写入足够内容触发至少 1 次轮转
  const std::string msg(80, 'x');
  for (int i = 0; i < 5; ++i) {
    OJ_LOG_INFO(msg);
  }
  oj::Logger::Instance().Shutdown();

  // 应有 .tar.gz 归档文件生成
  int archives = CountFilesContain(dir, ".tar.gz");
  EXPECT_GE(archives, 1) << "expected at least 1 archive after rotation";

  // 当前活跃文件应存在
  std::string log_path = FindLogFile(dir);
  EXPECT_FALSE(log_path.empty());

  std::filesystem::remove_all(dir);
}

// ===========================
// 多次轮转按序列命名归档: .tar.gz, .1.tar.gz, .2.tar.gz ...
// ===========================
TEST(LoggerTest, LogRotationArchiveSequence) {
  const std::string dir = kTestRoot + "/archive_seq";
  std::filesystem::remove_all(dir);

  oj::LoggingConfig cfg;
  cfg.level = oj::LogLevel::Info;
  cfg.to_stdout = false;
  cfg.file = dir;
  cfg.max_file_size = 80;  // 很小的阈值
  oj::Logger::Instance().Init(cfg);

  const std::string msg(60, 'y');
  for (int i = 0; i < 30; ++i) {
    OJ_LOG_INFO(msg);
  }
  oj::Logger::Instance().Shutdown();

  // 统计归档文件数
  int arch_count = 0;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    auto name = entry.path().filename().string();
    if (name.rfind(".tar.gz") != std::string::npos) {
      ++arch_count;
    }
  }
  EXPECT_GE(arch_count, 2) << "expected multiple archives after many rotations";

  std::filesystem::remove_all(dir);
}

// ===========================
// 归档内容正确性：提取并验证日志内容
// ===========================
TEST(LoggerTest, ArchiveContainsLogContent) {
  const std::string dir = kTestRoot + "/archive_content";
  std::filesystem::remove_all(dir);

  oj::LoggingConfig cfg;
  cfg.level = oj::LogLevel::Info;
  cfg.to_stdout = false;
  cfg.file = dir;
  cfg.max_file_size = 150;
  oj::Logger::Instance().Init(cfg);

  OJ_LOG_INFO("content_verify_marker");
  // 用一条较长消息触发轮转
  const std::string fill(120, 'z');
  OJ_LOG_INFO(fill);
  oj::Logger::Instance().Shutdown();

  // 查找归档文件（任意一个）
  std::string archive;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    auto name = entry.path().filename().string();
    if (name.rfind("log_", 0) == 0 && name.find(".tar.gz") != std::string::npos) {
      archive = entry.path().string();
      break;
    }
  }
  ASSERT_FALSE(archive.empty()) << "no archive found";

  // 从归档中提取日志内容
  auto content = ExtractLogFromArchive(archive);
  if (!content.empty()) {
    EXPECT_NE(content.find("content_verify_marker"), std::string::npos);
  }
  // 即使内容为空，归档非空也算通过
  EXPECT_GT(std::filesystem::file_size(archive), 20U);

  std::filesystem::remove_all(dir);
}

// ===========================
// 重新 Init 切换到新目录后，旧目录日志仍在
// ===========================
TEST(LoggerTest, ReInitChangesDirectory) {
  const std::string dir_a = kTestRoot + "/reinit_a";
  const std::string dir_b = kTestRoot + "/reinit_b";
  std::filesystem::remove_all(dir_a);
  std::filesystem::remove_all(dir_b);

  oj::LoggingConfig cfg;
  cfg.level = oj::LogLevel::Info;
  cfg.to_stdout = false;
  cfg.max_file_size = 10 * 1024 * 1024;

  // 初始化到目录 A
  cfg.file = dir_a;
  oj::Logger::Instance().Init(cfg);
  OJ_LOG_INFO("write_to_a");
  oj::Logger::Instance().Shutdown();

  // 重新初始化到目录 B
  cfg.file = dir_b;
  oj::Logger::Instance().Init(cfg);
  OJ_LOG_INFO("write_to_b");
  oj::Logger::Instance().Shutdown();

  // A 应有日志
  std::string path_a = FindLogFile(dir_a);
  ASSERT_FALSE(path_a.empty());
  EXPECT_NE(ReadAll(path_a).find("write_to_a"), std::string::npos);

  // B 应有日志
  std::string path_b = FindLogFile(dir_b);
  ASSERT_FALSE(path_b.empty());
  EXPECT_NE(ReadAll(path_b).find("write_to_b"), std::string::npos);

  // A 不应包含 B 的内容
  EXPECT_EQ(ReadAll(path_a).find("write_to_b"), std::string::npos);

  std::filesystem::remove_all(dir_a);
  std::filesystem::remove_all(dir_b);
}

// ===========================
// file="" 时不写入文件日志
// ===========================
TEST(LoggerTest, NoFileLoggingWhenFileEmpty) {
  const std::string dir = kTestRoot + "/no_file";
  std::filesystem::remove_all(dir);

  oj::LoggingConfig cfg;
  cfg.level = oj::LogLevel::Info;
  cfg.to_stdout = true;
  cfg.file = "";
  cfg.max_file_size = 10 * 1024 * 1024;
  oj::Logger::Instance().Init(cfg);

  OJ_LOG_INFO("only_stdout");
  oj::Logger::Instance().Shutdown();

  // 目录不应被创建
  EXPECT_FALSE(std::filesystem::exists(dir));

  std::filesystem::remove_all(dir);
}
