#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <string>
#include <cstdlib>
#include <unistd.h>
#include <sys/types.h>
#include <sstream>
#include "utils/crypto.h"

using json = nlohmann::json;

static std::string read_file(const std::string& p) {
    std::ifstream ifs(p);
    if (!ifs) return std::string();
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

static json run_judger_cli(const json& req) {
    pid_t pid = getpid();
    unsigned long t = static_cast<unsigned long>(time(nullptr));
    std::ostringstream base;
    base << "/tmp/oj_test_" << pid << "_" << t;
    std::string in_path = base.str() + "_in.json";
    std::string out_path = base.str() + "_out.json";

    // write input
    {
        std::ofstream ofs(in_path);
        ofs << req.dump();
    }

    // run judger_cli (project places binary at ./run/judger_cli)
    std::string cmd = std::string("./run/judger_cli < ") + in_path + " > " + out_path + " 2>/dev/null";
    int r = std::system(cmd.c_str());
    (void)r;

    std::string out = read_file(out_path);
    if (out.empty()) return json();
    try {
        return json::parse(out);
    } catch (...) {
        return json();
    }
}

TEST(SecurityAcceptance, PasswordHash_PBKDF2_Verify) {
    std::string pw = "correcthorsebatterystaple";
    std::string hash = oj::GeneratePasswordHash(pw);
    EXPECT_NE(hash, pw);
    EXPECT_TRUE(oj::VerifyPassword(pw, hash));
    EXPECT_FALSE(oj::VerifyPassword(std::string("wrongpw"), hash));
}

TEST(SecurityAcceptance, Sandbox_ForkDenied) {
    std::string src = R"CPP(
#include <unistd.h>
#include <stdlib.h>
int main(){
    if (fork() == -1) return 123;
    return 0;
}
)CPP";
    json req;
    req["language"] = "cpp";
    req["source_code"] = src;
    req["time_limit_ms"] = 2000;
    req["memory_limit_kb"] = 65536;
    req["work_dir"] = "./run/security_fork";
    req["test_cases"] = json::array();
    req["test_cases"].push_back({{"id", 1}, {"input", ""}, {"expected_output", ""}});

    json out = run_judger_cli(req);
    ASSERT_TRUE(out.contains("results"));
    ASSERT_TRUE(out["results"].is_array());
    auto res = out["results"][0];
    EXPECT_EQ(res["status"].get<std::string>(), "runtime_error");
}

TEST(SecurityAcceptance, Sandbox_OpenDenied) {
    std::string src = R"CPP(
#include <stdio.h>
#include <stdlib.h>
int main(){
    FILE* f = fopen("/etc/passwd", "r");
    if (!f) return 123;
    fclose(f);
    return 0;
}
)CPP";
    json req;
    req["language"] = "cpp";
    req["source_code"] = src;
    req["time_limit_ms"] = 2000;
    req["memory_limit_kb"] = 65536;
    req["work_dir"] = "./run/security_open";
    req["test_cases"] = json::array();
    req["test_cases"].push_back({{"id", 1}, {"input", ""}, {"expected_output", ""}});

    json out = run_judger_cli(req);
    ASSERT_TRUE(out.contains("results"));
    auto res = out["results"][0];
    EXPECT_EQ(res["status"].get<std::string>(), "runtime_error");
}

TEST(SecurityAcceptance, Sandbox_SocketDenied) {
    std::string src = R"CPP(
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
int main(){
    int s = socket(2, 1, 0);
    if (s == -1) return 123;
    close(s);
    return 0;
}
)CPP";
    json req;
    req["language"] = "cpp";
    req["source_code"] = src;
    req["time_limit_ms"] = 2000;
    req["memory_limit_kb"] = 65536;
    req["work_dir"] = "./run/security_socket";
    req["test_cases"] = json::array();
    req["test_cases"].push_back({{"id", 1}, {"input", ""}, {"expected_output", ""}});

    json out = run_judger_cli(req);
    ASSERT_TRUE(out.contains("results"));
    auto res = out["results"][0];
    EXPECT_EQ(res["status"].get<std::string>(), "runtime_error");
}

TEST(SecurityAcceptance, Resource_CPU_TimeLimit) {
    std::string src = R"CPP(
#include <unistd.h>
int main(){
    while(1) { }
    return 0;
}
)CPP";
    json req;
    req["language"] = "cpp";
    req["source_code"] = src;
    req["time_limit_ms"] = 500;
    req["memory_limit_kb"] = 65536;
    req["work_dir"] = "./run/security_cpu";
    req["test_cases"] = json::array();
    req["test_cases"].push_back({{"id", 1}, {"input", ""}, {"expected_output", ""}});

    json out = run_judger_cli(req);
    ASSERT_TRUE(out.contains("results"));
    auto res = out["results"][0];
    EXPECT_EQ(res["status"].get<std::string>(), "time_limit_exceeded");
}

TEST(SecurityAcceptance, Resource_MemoryLimit) {
    std::string src = R"CPP(
#include <stdlib.h>
#include <string.h>
int main(){
    // try to allocate large memory
    size_t n = 200 * 1024 * 1024ULL; // 200MB
    char* p = (char*)malloc(n);
    if (!p) return 123;
    memset(p, 0, n);
    return 0;
}
)CPP";
    json req;
    req["language"] = "cpp";
    req["source_code"] = src;
    req["time_limit_ms"] = 2000;
    req["memory_limit_kb"] = 64 * 1024; // 64MB limit
    req["work_dir"] = "./run/security_mem";
    req["test_cases"] = json::array();
    req["test_cases"].push_back({{"id", 1}, {"input", ""}, {"expected_output", ""}});

    json out = run_judger_cli(req);
    ASSERT_TRUE(out.contains("results"));
    auto res = out["results"][0];
    // memory limit can manifest as runtime_error or memory_limit_exceeded depending on OS; accept either
    std::string status = res["status"].get<std::string>();
    EXPECT_TRUE(status == "memory_limit_exceeded" || status == "runtime_error");
}
