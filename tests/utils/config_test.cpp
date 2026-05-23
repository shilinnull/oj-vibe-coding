#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <string>

#include "utils/config.h"

namespace {

std::string WriteTempFile(const std::string& content) {
  const char* path = "/tmp/oj_config_test.yaml";
  std::ofstream ofs(path, std::ios::trunc);
  ofs << content;
  ofs.close();
  return path;
}

}  // namespace

TEST(ConfigTest, LoadsDefaultsAndEnvExpansion) {
  ::setenv("OJ_TEST_MYSQL_PW", "pw_from_env", 1);

  const std::string yaml = R"(
server:
  host: 127.0.0.1
  port: 18080
mysql:
  host: localhost
  port: 3306
  user: root
  password: ${OJ_TEST_MYSQL_PW}
  database: oj
  pool:
    max_connections: 7
    connect_timeout_ms: 3000
logging:
  level: debug
  to_stdout: false
  file: ""
)";

  const std::string path = WriteTempFile(yaml);
  auto cfg = oj::Config::LoadFromFile(path);

  EXPECT_EQ(cfg.server.host, "127.0.0.1");
  EXPECT_EQ(cfg.server.port, 18080);

  EXPECT_EQ(cfg.mysql.host, "localhost");
  EXPECT_EQ(cfg.mysql.password, "pw_from_env");
  EXPECT_EQ(cfg.mysql.pool.max_connections, 7u);
  EXPECT_EQ(cfg.mysql.pool.connect_timeout_ms, 3000);

  EXPECT_EQ(cfg.logging.to_stdout, false);
  EXPECT_EQ(cfg.logging.level, oj::LogLevel::Debug);
}

TEST(ConfigTest, ParsesAndFormatsLogLevels) {
  EXPECT_EQ(oj::ParseLogLevel("debug"), oj::LogLevel::Debug);
  EXPECT_EQ(oj::ParseLogLevel("WARNING"), oj::LogLevel::Warn);
  EXPECT_EQ(oj::ParseLogLevel("Error"), oj::LogLevel::Error);
  EXPECT_EQ(oj::ToString(oj::LogLevel::Info), "info");
}
