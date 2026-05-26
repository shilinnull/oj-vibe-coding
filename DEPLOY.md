# 部署说明（DEPLOY.md）

本文档描述在 Linux 单机环境下部署 OJ Vibe Coding 项目的步骤与注意事项。

## 先决条件
- 操作系统：任意主流 Linux 发行版（测试使用 Ubuntu/Debian/CentOS）。
- 构建工具：`cmake` >= 3.20、`make`、`gcc`/`g++`（支持 C++17）。
- 必要的系统库 / 开发包：
  - `libmysqlclient` / `libmysqlclient-dev`（MySQL 客户端库）
  - `libssl-dev`（OpenSSL）
  - `libyaml-cpp-dev`（yaml-cpp）
  - `nlohmann_json`（或包管理器中的对应包）
  - `pkg-config`
  - `build-essential`（或等价的编译工具链）
  - `nsjail`（用于判题沙箱）

安装示例（Debian/Ubuntu）：

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config libmysqlclient-dev libssl-dev libyaml-cpp-dev libgtest-dev git
# 安装 nsjail: 推荐从源码或发行包获取并放到 /usr/local/bin
# sudo apt install nsjail || build nsjail from https://github.com/google/nsjail
```

## 数据库准备
1. 安装并启动 MySQL / MariaDB。
2. 创建数据库与用户，例如：

```sql
CREATE DATABASE oj_vibe DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_general_ci;
CREATE USER 'ojuser'@'localhost' IDENTIFIED BY 'strong_password';
GRANT ALL PRIVILEGES ON oj_vibe.* TO 'ojuser'@'localhost';
FLUSH PRIVILEGES;
```

3. 执行初始化脚本来创建表结构：

```bash
mysql -u ojuser -p oj_vibe < scripts/init_db.sql
```

## 配置
- 编辑 `config/config.yaml`，设置数据库连接、服务监听端口、JWT secret、判题配置等。配置字段参考 `docs/SPEC.md` 中的示例字段。
- 重要字段示例：

```yaml
server:
  host: 0.0.0.0
  port: 8080

mysql:
  host: 127.0.0.1
  port: 3306
  user: ojuser
  password: strong_password
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

## 构建
在仓库根目录执行以下命令：

```bash
mkdir -p build
cd build
cmake ..
make -j$(nproc)
```

构建产物：
- `build/oj_server`：Web 服务二进制
- `run/judger_cli`：判题子进程二进制（CMake 配置会把该可执行放到源目录的 `run/`）
- `run/libjudge_sandbox_preload.so`：判题 preload 库

请确认 `run/` 目录对运行服务的系统用户可写（用来存放临时编译产物和运行时文件）。

## 初始化管理员账号
构建完成后使用工具创建管理员账号：

```bash
./build/register_admin --help
# 或者直接运行二进制（示例）
./tools/register_admin
```

## 启动服务（开发/调试）
直接在 `build/` 下启动：

```bash
cd /path/to/oj-vibe-coding
./build/oj_server --config ./config/config.yaml
```

如需在后台或生产环境运行，建议使用 `systemd` 管理服务（示例见下）。

## systemd 单机示例
创建 `/etc/systemd/system/oj_vibe.service`：

```ini
[Unit]
Description=OJ Vibe Coding Server
After=network.target

[Service]
Type=simple
User=oj
Group=oj
WorkingDirectory=/opt/oj-vibe-coding
ExecStart=/opt/oj-vibe-coding/build/oj_server --config /opt/oj-vibe-coding/config/config.yaml
Restart=on-failure
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

注意：`ExecStart` 的路径和 `User`/`Group` 请按实际部署调整。

## 判题沙箱注意事项
- 确保 `nsjail` 可执行且 `judge.nsjail` 配置正确，`config/config.yaml` 中 `nsjail_config` 字段指向有效文件。
- 判题运行时需要写权限的 `work_dir`（默认 `run/`），并且该目录要严格控制权限以防止越权访问。
- 在部分系统中需要给 `nsjail` 或判题二进制额外能力（慎用），优先使用 namespaces + cgroups + nsjail 自身配置而非提升二进制权限。

## 日志与排错
- 服务日志：程序会输出到 stdout/stderr，可用 `systemd/journalctl` 或重定向到文件。
- 常见问题：
  - 无法连接 MySQL：确认 `config/config.yaml` 中连接信息正确并且 MySQL 允许来自服务主机的连接。
  - judger_cli 找不到：确认 `run/judger_cli` 存在且可执行，JudgeManager 使用相对路径时以服务的 WorkingDirectory 为准。
  - nsjail 报错：检查 nsjail 配置文件路径与权限。

## 运行单元测试
如果在 CMake 时启用了 `OJ_BUILD_TESTS=ON`，可以在 `build/` 中运行：

```bash
ctest --output-on-failure
```

或直接运行：

```bash
./build/oj_tests
```

## 备份与升级
- 升级前请备份 MySQL 数据库与 `config/`、`run/` 目录下重要数据。
- 升级流程通常为：拉取新代码 → 停服 → 备份 → 编译 → 部署新二进制 → 启动并检查日志。
