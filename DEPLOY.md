# 部署说明

本文档描述在 Linux 环境下部署 OJ Vibe Coding 项目的步骤与注意事项，涵盖单机部署与分布式多 Worker 部署。

## 先决条件

- 操作系统：任意主流 Linux 发行版（测试使用 Ubuntu/Debian/CentOS）
- 构建工具：`cmake` >= 3.20、`make`、`gcc`/`g++`（支持 C++17）
- 系统库 / 开发包：
  - `libmysqlclient-dev`（MySQL 客户端库）
  - `libssl-dev`（OpenSSL）
  - `libyaml-cpp-dev`（yaml-cpp）
  - `nlohmann-json3-dev`（或者从 https://github.com/nlohmann/json 手动安装）
  - `pkg-config`
  - `build-essential`（或等价的编译工具链）
  - `nsjail`（可选，用于判题沙箱）

Debian/Ubuntu 安装示例：

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config libmysqlclient-dev \
  libssl-dev libyaml-cpp-dev libgtest-dev nlohmann-json3-dev git
# nsjail 推荐从源码或发行包获取
# sudo apt install nsjail || build from https://github.com/google/nsjail
```

## 构建

```bash
cd /path/to/oj-vibe-coding
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

### 构建产物

| 产物 | 路径 | 说明 |
|------|------|------|
| `oj_server` | `build/oj_server` | OJ HTTP 服务主程序 |
| `judge_worker` | `build/judge_worker` | 远程判题 Worker 独立服务 |
| `judger_cli` | `run/judger_cli` | 判片子进程（Worker 内部调用） |
| `libjudge_sandbox_preload.so` | `run/libjudge_sandbox_preload.so` | 沙箱 LD_PRELOAD 拦截库 |
| `register_admin` | `build/register_admin` | 管理员账号创建工具 |
| `reset_web_data` | `build/reset_web_data` | Web 数据重置工具 |
| `oj_tests` | `build/oj_tests` | 单元测试（需 `cmake .. -DOJ_BUILD_TESTS=ON`） |

请确认 `run/` 目录对运行服务的系统用户可写（存放临时编译产物和运行时文件）。

## 数据库准备

1. 安装并启动 MySQL / MariaDB。
2. 创建数据库与用户：

```sql
CREATE DATABASE oj DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_general_ci;
CREATE USER 'ojuser'@'localhost' IDENTIFIED BY 'strong_password';
GRANT ALL PRIVILEGES ON oj.* TO 'ojuser'@'localhost';
FLUSH PRIVILEGES;
```

3. 执行初始化脚本：

```bash
mysql -u ojuser -p oj < scripts/init_db.sql
```

## 配置

编辑 `config/config.yaml`。以下为所有配置字段的完整参考：

```yaml
server:
  host: 0.0.0.0          # 监听地址
  port: 8080              # 监听端口

mysql:
  host: 127.0.0.1         # MySQL 主机
  port: 3306              # MySQL 端口
  user: ojuser            # 数据库用户
  password: strong_password  # 数据库密码
  database: oj            # 数据库名称
  pool:
    max_connections: 10   # 连接池大小
    connect_timeout_ms: 2000  # 连接超时（毫秒）

auth:
  jwt:
    secret: "change_this_secret"  # JWT 签名密钥（务必修改）
    expires_seconds: 86400        # Token 过期时间（秒）

judge:
  max_concurrency: 4            # 判题调度线程数（控制同时处理的任务数）
  nsjail_config: ./config/nsjail.cfg  # nsjail 配置文件路径
  work_dir: ./run/judge         # 判题工作目录
  default_time_limit_ms: 1000   # 默认时间限制（毫秒）
  default_memory_limit_kb: 262144  # 默认内存限制（KB，256MB）
  request_timeout_ms: 3000      # 远程 Worker HTTP 请求超时（毫秒）
  retry_count: 3                # 判题失败重试次数
  health_check_interval_ms: 5000  # Worker 健康检查间隔（毫秒，<=0 禁用）

logging:
  level: info              # 日志级别: debug/info/warn/error
  to_stdout: false         # 是否输出到 stdout
  file: ./logs/            # 日志目录（为空则不写文件）
  max_file_size: 10        # 单文件上限（MB），达到后自动轮转压缩
```

### 配置说明

- `judge.workers`（可选）：若配置文件中的 `workers` 序列存在，OJ Server 启动时会自动加载这些 Worker 作为静态节点。未配置时，Worker 需通过 HTTP 接口动态注册。
- `max_concurrency` 建议与 CPU 核心数或 Worker 节点数量匹配。
- JWT secret 应使用足够长的随机字符串，生产环境建议通过环境变量注入。

## 运行 OJ Server

### 开发调试

```bash
cd /path/to/oj-vibe-coding
./build/oj_server --config ./config/config.yaml
```

### systemd 服务

创建 `/etc/systemd/system/oj_vibe.service`：

```ini
[Unit]
Description=OJ Vibe Coding Server
After=network.target mysql.service
Requires=mysql.service

[Service]
Type=simple
User=oj
Group=oj
WorkingDirectory=/opt/oj-vibe-coding
ExecStart=/opt/oj-vibe-coding/build/oj_server --config /opt/oj-vibe-coding/config/config.yaml
Restart=on-failure
RestartSec=5
AmbientCapabilities=CAP_SYS_CHROOT

[Install]
WantedBy=multi-user.target
```

启用并启动：

```bash
sudo systemctl daemon-reload
sudo systemctl enable oj_vibe.service
sudo systemctl start oj_vibe.service
sudo journalctl -u oj_vibe.service -f
```

## 部署判题 Worker

Worker 是独立于 OJ Server 的进程，启动后自动向 Server 注册。支持水平扩展以提升判题并发能力。

### 启动 Worker

```bash
# 单机启动 3 个 Worker（分别监听不同端口），并注册到本机 OJ Server
./build/judge_worker 9001 127.0.0.1 8080 &
./build/judge_worker 9002 127.0.0.1 8080 &
./build/judge_worker 9003 127.0.0.1 8080 &
```

启动参数格式（必填）：

```bash
./build/judge_worker <worker_port> <oj_server_host> <oj_server_port>
```

### 环境变量

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `JUDGE_WORKER_HOST` | `127.0.0.1` | Worker 自身地址（多机部署需修改） |

说明：`worker_port`、`oj_server_host`、`oj_server_port` 都必须显式传入，`judge_worker` 不再为它们提供默认值。

### 多机部署

将 Worker 部署到不同机器：

```bash
# 在 Worker 机器上启动
JUDGE_WORKER_HOST=10.0.0.2 ./build/judge_worker 9001 10.0.0.1 8080
```

### Worker systemd 服务

创建 `/etc/systemd/system/oj_worker@.service`：

```ini
[Unit]
Description=OJ Judge Worker %i
After=network.target

[Service]
Type=simple
User=oj
Group=oj
WorkingDirectory=/opt/oj-vibe-coding
Environment=OJ_SERVER_HOST=127.0.0.1
Environment=OJ_SERVER_PORT=8080
ExecStart=/opt/oj-vibe-coding/build/judge_worker %i ${OJ_SERVER_HOST} ${OJ_SERVER_PORT}
Restart=on-failure
RestartSec=3

[Install]
WantedBy=multi-user.target
```

启动多个 Worker：

```bash
sudo systemctl enable oj_worker@9001.service --now
sudo systemctl enable oj_worker@9002.service --now
sudo systemctl enable oj_worker@9003.service --now
```

## 负载均衡机制

### 架构

```
                    ┌──────────────┐
                    │  OJ Server   │
                    │ (oj_server)  │
                    │  Port 8080   │
                    └──────┬───────┘
                           │ JudgeWorkerBalancer
          ┌────────────────┼────────────────┐
          ▼                ▼                ▼
   ┌──────────┐    ┌──────────┐    ┌──────────┐
   │ Worker 1 │    │ Worker 2 │    │ Worker 3 │
   │ :9001    │    │ :9002    │    │ :9003    │
   └──────────┘    └──────────┘    └──────────┘
```

### Worker 注册

1. Worker 启动后，在后台线程中向 OJ Server 发送 `POST /api/judge/workers/register`
2. 注册成功（HTTP 200）后该线程退出，Worker 进入服务状态
3. 注册失败则每秒重试

### 选择策略

- **Least-Connection**：优先选择当前处理中任务数最少的 Worker
- **Round-Robin Tiebreaker**：当多个 Worker 负载相同时，依序轮转分配，确保均匀分布
- **健康检查**：每隔 `health_check_interval_ms`（默认 5000ms）探测所有 Worker 的 `/healthz` 端点，异常节点自动摘除
- **失败重试**：单次判题请求失败后，自动重试其他可用 Worker（最多 `retry_count` 次）

### 日志查看

查看负载均衡状态：

```bash
journalctl -u oj_vibe.service | grep "JudgeManager: selected worker"
```

正常情况应看到 Worker 被均匀分配：

```
JudgeManager: selected worker 127.0.0.1:9001, load_before=0
JudgeManager: selected worker 127.0.0.1:9002, load_before=0
JudgeManager: selected worker 127.0.0.1:9003, load_before=0
```

## 创建管理员

```bash
./build/register_admin --username admin --password your_password
```

查看帮助：

```bash
./build/register_admin --help
```

## 压测

使用 `scripts/repeat_submit.py` 验证判题流水线与负载均衡：

```bash
python3 scripts/repeat_submit.py \
  --base-url http://127.0.0.1:8080 \
  --problem-id 1 \
  --user-id 1 \
  --language-id 1 \
  --count 20 \
  --interval 0.1 \
  --source-file ./sample.cpp
```

参数说明：

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--base-url` | `http://127.0.0.1:8080` | OJ Server 地址 |
| `--problem-id` | 必填 | 题目 ID |
| `--user-id` | 必填 | 提交用户 ID |
| `--language-id` | 必填 | 语言 ID |
| `--count` | 10 | 提交次数 |
| `--interval` | 0.0 | 每次提交间隔（秒） |
| `--poll-interval` | 0.0 | 轮询判题结果间隔（秒） |
| `--timeout` | 120.0 | 等待判题超时（秒） |
| `--mode` | `run` | 判题模式：`run`（运行样例）或 `submit`（提交判题） |
| `--source-file` | - | 源代码文件路径 |
| `--source` | - | 内联源代码 |

## 判题沙箱

- 确保 `nsjail` 可执行且 `judge.nsjail_config` 配置正确
- 判题运行时 `work_dir`（默认 `run/judge`）需要写权限，且应严格控制权限
- 可选的 `LD_PRELOAD` 沙箱（`run/libjudge_sandbox_preload.so`）通过 hook 系统调用（`open`/`fork`/`socket` 等）阻止恶意行为，作为 nsjail 的补充

## 日志与排错

- 服务日志位置由配置 `logging.file` 指定，默认 `./logs/`，按 `max_file_size` 自动轮转
- 启用 `logging.to_stdout: true` 可将日志同时输出到终端

### 常见问题

| 问题 | 排查 |
|------|------|
| 无法连接 MySQL | 确认 `config.yaml` 连接信息正确，MySQL 允许来自服务主机的连接 |
| Worker 注册失败 | 确认 OJ Server 已启动且端口正确，检查网络连通性 |
| Worker 健康检查失败 | 确认 Worker 进程存活，`/healthz` 返回 200 + "ok" |
| 所有 Worker 离线 | 检查 Worker 进程与网络，日志会显示 `no online worker available` |
| judger_cli 找不到 | 确认 `run/judger_cli` 存在且可执行，Worker 的工作目录包含 `run/` |
| nsjail 报错 | 检查 `nsjail` 配置文件路径与权限，确认 `CAP_SYS_CHROOT` 能力已设置 |

## 运行单元测试

```bash
cd build
ctest --output-on-failure
# 或直接运行
./oj_tests
```

## 备份与升级

### 备份

- MySQL 数据库：`mysqldump -u ojuser -p oj > backup_$(date +%Y%m%d).sql`
- 配置文件：`cp -r config/ backup/config/`
- 运行时数据：`cp -r run/ backup/run/`

### 升级流程

1. 拉取新代码：`git pull`
2. 停服：`sudo systemctl stop oj_vibe.service && sudo systemctl stop 'oj_worker@*'`
3. 备份（见上方）
4. 编译：`cd build && cmake .. && make -j$(nproc)`
5. 启动：`sudo systemctl start oj_vibe.service && sudo systemctl start 'oj_worker@*'`
6. 检查日志：`journalctl -u oj_vibe.service -f`
