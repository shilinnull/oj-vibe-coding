# OJ Vibe Coding

轻量级在线判题系统（Online Judge），用于教学与小型竞赛场景。后端基于 C++11 自主开发的高性能事件驱动网络服务器框架提供 HTTP 服务，采用非阻塞 IO + epoll 实现主从 Reactor 多线程模型，支持高并发网络连接处理。判题采用独立子进程 + nsjail 沙箱隔离架构，适合教学与本地部署测试。

## 功能

- 用户注册 / 登录（JWT 鉴权）
- 题目浏览与搜索
- 在线运行样例
- 提交代码并自动判题（多语言支持）
- 判题结果实时反馈（AC / WA / TLE / MLE / RE / CE）
- 管理员：题目 CRUD、测试用例上传与管理

## 目录结构

```
├── src/
│   ├── net/                高性能网络库（自研 muduo 风格）
│   ├── server/             HTTP 服务启动入口
│   ├── handler/            业务逻辑
│   │   ├── auth_handler        注册 / 登录
│   │   ├── problem_handler     题目管理
│   │   ├── submission_handler  提交与判题
│   │   └── admin_handler       管理员接口
│   ├── judge/              判题调度
│   │   ├── judge_manager       判题任务队列与并发控制
│   │   ├── judger_cli          判题子进程
│   │   └── sandbox_preload     沙箱 preload 库
│   ├── db/                  MySQL 连接池 + DAO
│   ├── model/               数据模型（User / Problem / Submission / TestCase / Language）
│   ├── middleware/          JWT 鉴权中间件
│   └── utils/              工具类（Config / Logger / Crypto / JsonHelper）
├── web/                    前端静态页面
│   ├── pages/              页面模板
│   ├── js/                 JavaScript 逻辑
│   └── css/                样式
├── config/                 配置文件（config.yaml）
├── run/                    运行时目录（判题工作区、judger 二进制）
├── scripts/                数据库初始化脚本
├── tests/                  GTest 单元测试
└── tools/                  辅助工具（register_admin、reset_web_data）
```

## 快速开始

### 依赖（Ubuntu/Debian）

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config \
  libmysqlclient-dev libssl-dev libyaml-cpp-dev libgtest-dev git
```

### 构建

```bash
git clone <repo>
cd oj-vibe-coding
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

### 初始化数据库

```bash
mysql -u root -p -e "CREATE DATABASE oj_vibe DEFAULT CHARACTER SET utf8mb4;"
mysql -u root -p oj_vibe < ../scripts/init_db.sql
```

### 配置

编辑 `config/config.yaml`：

```yaml
server:
  host: 0.0.0.0
  port: 8080

mysql:
  host: 127.0.0.1
  port: 3306
  user: ojuser
  password: your_password
  database: oj_vibe

judge:
  max_concurrency: 4
  nsjail_config: ./config/nsjail.cfg
  work_dir: ./run
  default_time_limit_ms: 1000
  default_memory_limit_kb: 262144

auth:
  jwt:
    secret: "change_this_secret"
    expires_seconds: 86400
```

### 运行服务

```bash
./build/oj_server --config ./config/config.yaml
```

### 创建管理员

```bash
./build/tools/register_admin --username admin --password your_password
```

## 开发

### 构建选项

```bash
cmake .. -DOJ_BUILD_TESTS=ON   # 启用单元测试
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
- nsjail 沙箱配置与注意事项
- 日志与排错指南
- 备份与升级流程

## 技术栈

| 领域 | 技术 |
|------|------|
| 编程语言 | C++11 |
| 网络框架 | 自研高性能网络库（主从 Reactor + epoll LT + eventfd） |
| 构建工具 | CMake + Makefile |
| 调试工具 | g++、gdb |
| 数据库 | MySQL / MariaDB |
| 持久化 | 自研连接池 + 参数化 SQL |
| 鉴权 | JWT（自研签名） |
| 判题沙箱 | nsjail + 独立子进程 |
| 前端 | HTML / CSS / JavaScript + 代码编辑器 |
| 测试 | GTest |
| 版本控制 | git |

## 许可证

MIT
