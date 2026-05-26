# OJ Vibe Coding

轻量级在线判题系统（Online Judge），用于教学与小型竞赛场景。项目以 C++ 编写，采用独立判题子进程（`judger_cli`）+ `nsjail` 沙箱隔离的架构，适合教学与本地部署测试。

主要功能：用户注册/登录、题目浏览、在线运行样例、提交判题、管理员题目管理与测试用例上传。

完整设计与接口见：`docs/SPEC.md`。

目录概览
- `src/`：服务端源代码（Router / Handler / JudgeManager / judger_cli）
- `web/`：前端静态文件（HTML/CSS/JS，包含编辑器集成）
- `config/`：配置文件（`config.yaml`）
- `run/`：运行时目录（判题工作目录、judger 可执行输出）
- `scripts/`：辅助脚本（如 `init_db.sql`）
- `tests/`：单元测试（GTest）
- `tools/`：辅助工具（`register_admin`、`reset_web_data`）

快速开始（开发）

1) 安装依赖（Debian/Ubuntu 示例）：

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config libmysqlclient-dev libssl-dev libyaml-cpp-dev libgtest-dev git
```

2) 克隆并构建：

```bash
git clone <repo>
cd oj-vibe-coding
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

3) 运行服务（示例）：

```bash
./oj_server --config ../config/config.yaml
```

注意：`judger_cli` 可执行文件会被放到 `run/` 目录（由 CMake 配置），请确保 `config/config.yaml` 中 `judge.nsjail_config` 与 `judge.work_dir` 等字段正确指向运行环境。

配置

主要运行时配置在 `config/config.yaml`：
- `server`：监听地址与端口
- `mysql`：数据库连接信息
- `auth.jwt`：JWT 密钥与过期时间
- `judge`：判题并发、nsjail 配置、work_dir、默认时限/内存限制

初始化数据库

创建数据库并执行：

```bash
mysql -u <user> -p <database> < scripts/init_db.sql
```

使用 `tools/register_admin` 创建管理员账号。

运行单元测试

在 `build/` 下（启用 `OJ_BUILD_TESTS=ON`）运行：

```bash
ctest --output-on-failure
# 或者
./oj_tests
```

部署

详见 [DEPLOY.md](DEPLOY.md)（包含系统依赖、systemd 示例、nsjail 注意事项等）。

贡献与联系方式

- 欢迎通过 Pull Request 或 Issue 提交修复与功能建议。
- 需要我协助生成 Dockerfile、systemd 单元或 nsjail 示例配置，请告知目标发行版与运行用户。

---
更多细节见 `docs/SPEC.md`。
