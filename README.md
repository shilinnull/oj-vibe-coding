# OJ Vibe Coding

轻量级在线判题系统（Online Judge），用于教学与小型竞赛场景。

后端基于 C++11 自主开发的网络库（主从 Reactor + epoll LT + eventfd）提供高性能 HTTP 服务。判题采用**分布式架构**：OJ Server 负责任务调度与负载均衡，远程 Judge Worker 独立进程执行编译与沙箱隔离。支持水平扩展 Worker 节点以提升判题并发能力。

## 功能

- 用户注册 / 登录（JWT 鉴权）
- 题目浏览与搜索
- 在线运行样例
- 提交代码并自动判题（多语言支持）
- 判题结果实时反馈（AC / WA / TLE / MLE / RE / CE）
- 管理员：题目 CRUD、测试用例上传与管理
- 分布式判题 Worker，支持水平扩展与负载均衡

## 目录结构

```
├── src/
│   ├── net/                高性能网络库（自研 muduo 风格）
│   ├── server/             HTTP 服务 + 路由注册
│   ├── handler/            业务逻辑处理
│   │   ├── auth_handler        注册 / 登录
│   │   ├── problem_handler     题目管理
│   │   ├── submission_handler  提交与判题调度
│   │   └── admin_handler       管理员接口
│   ├── judge/              判题子系统
│   │   ├── judge_manager       判题任务队列与并发控制
│   │   ├── judge_manager.h     任务调度、Worker 注册、健康检查
│   │   ├── judge_worker_balancer.h  负载均衡器（Least-Connection + Round-Robin）
│   │   ├── judge_worker_server.cpp   远程判题 Worker 独立服务
│   │   ├── judger_cli          本地判片子进程
│   │   ├── judge_executor      判题执行引擎（fork + setrlimit + 沙箱）
│   │   └── sandbox_preload     沙箱 LD_PRELOAD 拦截库
│   ├── db/                  MySQL 连接池 + DAO
│   ├── model/               数据模型
│   ├── middleware/          JWT 鉴权中间件
│   └── utils/              工具类（Config / Logger / Crypto / JsonHelper）
├── web/                    前端静态页面（SPA）
│   ├── pages/              页面模板
│   ├── js/                 JavaScript 逻辑
│   └── css/                样式
├── config/                 配置文件（config.yaml）
│   └── config.yaml         服务配置（数据库、判题、JWT 等）
├── run/                    运行时目录（判题工作区、judger_cli 二进制）
├── scripts/                数据库初始化脚本与压测工具
│   ├── init_db.sql          建表 + 种子数据
│   └── repeat_submit.py     批量提交压测脚本
├── tests/                  GTest 单元测试
└── tools/                  辅助工具
    ├── register_admin.cpp   管理员账号创建
    └── reset_web_data.cpp   数据重置
```

## 快速开始

### 依赖（Ubuntu/Debian）

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config \
  libmysqlclient-dev libssl-dev libyaml-cpp-dev libgtest-dev \
  nlohmann-json3-dev git
```

> 如果 `nlohmann-json3-dev` 不可用，可从 https://github.com/nlohmann/json 手动安装。

### 构建

```bash
git clone <repo>
cd oj-vibe-coding
mkdir -p build && cd build
cmake .. -DOJ_BUILD_TESTS=ON
make -j$(nproc)
```

构建产物：

| 产物 | 路径 | 说明 |
|------|------|------|
| `oj_server` | `build/oj_server` | OJ HTTP 服务主程序 |
| `judge_worker` | `build/judge_worker` | 远程判题 Worker 服务 |
| `judger_cli` | `run/judger_cli` | 本地判片子进程（Worker 内部调用） |
| `libjudge_sandbox_preload.so` | `run/libjudge_sandbox_preload.so` | 沙箱 preload 库 |
| `register_admin` | `build/register_admin` | 管理员创建工具 |
| `reset_web_data` | `build/reset_web_data` | 数据重置工具 |
| `oj_tests` | `build/oj_tests` | 单元测试（需 `-DOJ_BUILD_TESTS=ON`） |

### 初始化数据库

```bash
mysql -u root -p -e "CREATE DATABASE oj DEFAULT CHARACTER SET utf8mb4;"
mysql -u root -p oj < scripts/init_db.sql
```

### 配置

编辑 `config/config.yaml`，主要字段：

```yaml
server:
  host: 0.0.0.0
  port: 8080

mysql:
  host: 127.0.0.1
  port: 3306
  user: ojuser
  password: your_password
  database: oj

judge:
  max_concurrency: 4           # 判题并发线程数
  request_timeout_ms: 3000     # Worker HTTP 请求超时
  retry_count: 3               # 判题失败重试次数
  health_check_interval_ms: 5000  # Worker 健康检查间隔

auth:
  jwt:
    secret: "change_this_secret"
    expires_seconds: 86400
```

完整配置字段参见 [DEPLOY.md](DEPLOY.md#配置参考)。

### 运行服务

```bash
# 启动 OJ Server（监听 8080 端口）
./build/oj_server --config ./config/config.yaml
```

### 启动判题 Worker

Worker 以独立进程运行，启动后自动向 OJ Server 注册：

```bash
# 启动 3 个 Worker，分别监听 9001、9002、9003，并注册到 10.0.0.1:8080
./build/judge_worker 9001 10.0.0.1 8080 &
./build/judge_worker 9002 10.0.0.1 8080 &
./build/judge_worker 9003 10.0.0.1 8080 &
```

启动参数格式（必填）：

```bash
./build/judge_worker <worker_port> <oj_server_host> <oj_server_port>
```

环境变量配置 Worker 注册信息：

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `JUDGE_WORKER_HOST` | `127.0.0.1` | Worker 自身地址 |

说明：`worker_port`、`oj_server_host`、`oj_server_port` 都必须显式传入，`judge_worker` 不再为它们提供默认值。

### 创建管理员

```bash
./build/register_admin --username admin --password your_password
```

### 压测

使用 `scripts/repeat_submit.py` 测试判题流程与负载均衡：

```bash
python3 scripts/repeat_submit.py \
  --base-url http://127.0.0.1:8080 \
  --problem-id 1 \
  --user-id 1 \
  --language-id 1 \
  --count 10 \
  --source-file ./sample.cpp
```

## 架构

### 判题流程

```
用户 → POST /api/submissions
        ↓
  Submission Handler → JudgeManager.submit() → 任务队列
        ↓
  Worker Thread → ExecuteJob()
        ↓
  JudgeWorkerBalancer.SelectWorker()  ← Least-Connection + Round-Robin
        ↓
  Judge Worker (HTTP POST /judge) → judger_cli → 编译 → 运行 → 判题
        ↓
  SubmissionDao.UpdateResult() → 写入数据库
```

### 负载均衡

- **Least-Connection**：优先选择当前负载最低的 Worker
- **Round-Robin Tiebreaker**：当多个 Worker 负载相同时，依序轮转分配
- **健康检查**：定期探测 Worker `/healthz` 端点，自动摘除异常节点
- **失败重试**：请求失败后自动重试其他可用 Worker

## 开发

### 构建选项

```bash
cmake .. -DOJ_BUILD_TESTS=ON   # 启用单元测试
cmake .. -DCMAKE_BUILD_TYPE=Debug  # 调试模式
```

### 运行测试

```bash
cd build
ctest --output-on-failure
# 或直接运行
./oj_tests
```

## 部署

生产环境部署详见 [DEPLOY.md](DEPLOY.md)，包含：

- systemd 服务单元示例
- 多 Worker 部署与负载均衡配置
- nsjail 沙箱配置与注意事项
- 日志与排错指南
- 备份与升级流程

## 技术栈

| 领域 | 技术 |
|------|------|
| 编程语言 | C++11 / C++17 |
| 网络框架 | 自研高性能网络库（主从 Reactor + epoll LT + eventfd） |
| HTTP 服务 | 自研 HTTP 解析与路由 |
| 构建工具 | CMake + Makefile |
| 数据库 | MySQL / MariaDB |
| 持久化 | 自研连接池（mutex + CV）+ 参数化 SQL（Prepared Statement） |
| 鉴权 | JWT（自研 HMAC-SHA256 签名） |
| 判题引擎 | 独立子进程 + fork/setrlimit + nsjail 沙箱 |
| 判题 Worker | 独立 HTTP 服务，支持水平扩展 |
| 负载均衡 | Least-Connection + Round-Robin + 健康检查 |
| 前端 | HTML / CSS / JavaScript + Monaco Editor |
| 测试 | GTest（单元测试）+ Python（集成测试） |
| 版本控制 | git |

## 许可证

MIT
