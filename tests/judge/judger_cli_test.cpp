#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sstream>
#include <string>
#include <fstream>

using json = nlohmann::json;

static std::string read_fd_all(int fd) {
    std::ostringstream ss;
    char buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        ss.write(buf, n);
    }
    return ss.str();
}

static bool file_exists(const std::string& p) {
    std::ifstream f(p);
    return f.good();
}

TEST(JudgerCliTest, SimpleAdd) {
    // 构造简单的 C++ 源码
    std::string src = R"cpp(#include <iostream>
int main(){int a,b; if(!(std::cin>>a>>b)) return 0; std::cout<<a+b; return 0;}
)cpp";

    json req;
    req["language"] = "cpp";
    req["source_code"] = src;
    req["time_limit_ms"] = 1000;
    req["memory_limit_kb"] = 262144;
    req["work_dir"] = "./run";
    req["compile_cmd"] = "g++ -O2 -std=c++17 {source} -o {output}";
    req["run_cmd"] = "{binary}";
    req["test_cases"] = json::array();
    req["test_cases"].push_back({{"id", 1}, {"input", "1 2"}, {"expected_output", "3"}});

    // 寻找可执行 judger_cli 的路径（尝试 build 下的相对路径）
    std::string exe_paths[] = {"./run/judger_cli", "../run/judger_cli", "/usr/local/bin/judger_cli"};
    std::string exe;
    for (auto &p : exe_paths) if (file_exists(p)) { exe = p; break; }
    if (exe.empty()) {
        // 无法找到可执行文件，跳过测试
        GTEST_SKIP() << "无法找到 judger_cli 可执行文件，跳过测试";
    }

    int inpipe[2];
    int outpipe[2];
    ASSERT_EQ(pipe(inpipe), 0);
    ASSERT_EQ(pipe(outpipe), 0);

    pid_t pid = fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        // 子进程
        dup2(inpipe[0], STDIN_FILENO);
        dup2(outpipe[1], STDOUT_FILENO);
        close(inpipe[0]); close(inpipe[1]); close(outpipe[0]); close(outpipe[1]);
        execlp(exe.c_str(), exe.c_str(), (char*)NULL);
        _exit(127);
    }

    // 父进程
    close(inpipe[0]); close(outpipe[1]);
    std::string s = req.dump();
    write(inpipe[1], s.data(), s.size());
    close(inpipe[1]);

    std::string out = read_fd_all(outpipe[0]);
    close(outpipe[0]);

    int status = 0;
    waitpid(pid, &status, 0);

    ASSERT_FALSE(out.empty());
    json resp = json::parse(out);
    ASSERT_TRUE(resp.contains("status"));
    EXPECT_NE(resp["status"].get<std::string>(), "compile_error");
    ASSERT_TRUE(resp.contains("results"));
    ASSERT_TRUE(resp["results"].is_array());
    ASSERT_GT(resp["results"].size(), 0);
    EXPECT_EQ(resp["results"][0]["actual_output"].get<std::string>(), "3");
}

TEST(JudgerCliTest, ReportsWrongAnswerWhenOutputDiffers) {
    std::string src = R"cpp(#include <iostream>
int main(){return 0;}
)cpp";

    json req;
    req["language"] = "cpp";
    req["source_code"] = src;
    req["time_limit_ms"] = 1000;
    req["memory_limit_kb"] = 262144;
    req["work_dir"] = "./run";
    req["compile_cmd"] = "g++ -O2 -std=c++17 {source} -o {output}";
    req["run_cmd"] = "{binary}";
    req["test_cases"] = json::array();
    req["test_cases"].push_back({{"id", 1}, {"input", "1 2"}, {"expected_output", "3"}});

    std::string exe_paths[] = {"./run/judger_cli", "../run/judger_cli", "/usr/local/bin/judger_cli"};
    std::string exe;
    for (auto& p : exe_paths) if (file_exists(p)) { exe = p; break; }
    if (exe.empty()) {
        GTEST_SKIP() << "无法找到 judger_cli 可执行文件，跳过测试";
    }

    int inpipe[2];
    int outpipe[2];
    ASSERT_EQ(pipe(inpipe), 0);
    ASSERT_EQ(pipe(outpipe), 0);

    pid_t pid = fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        dup2(inpipe[0], STDIN_FILENO);
        dup2(outpipe[1], STDOUT_FILENO);
        close(inpipe[0]); close(inpipe[1]); close(outpipe[0]); close(outpipe[1]);
        execlp(exe.c_str(), exe.c_str(), (char*)NULL);
        _exit(127);
    }

    close(inpipe[0]); close(outpipe[1]);
    std::string s = req.dump();
    write(inpipe[1], s.data(), s.size());
    close(inpipe[1]);

    std::string out = read_fd_all(outpipe[0]);
    close(outpipe[0]);

    int status = 0;
    waitpid(pid, &status, 0);

    ASSERT_FALSE(out.empty());
    json resp = json::parse(out);
    ASSERT_TRUE(resp.contains("status"));
    EXPECT_EQ(resp["status"].get<std::string>(), "wrong_answer");
    ASSERT_TRUE(resp.contains("results"));
    ASSERT_TRUE(resp["results"].is_array());
    ASSERT_GT(resp["results"].size(), 0);
    EXPECT_EQ(resp["results"][0]["status"].get<std::string>(), "wrong_answer");
}
