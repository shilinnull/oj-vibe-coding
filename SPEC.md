# SPEC.md — 在线判题系统（OJ Vibe Coding）

## 1. 需求概述

### 1.1 用户故事

| 角色 | 需求 |
|------|------|
| 用户 | 注册/登录后浏览题目，在线编写 C/C++ 代码，支持"运行测试"看样例结果和"提交判题"跑全量测试，查看提交历史和详细判题结果 |
| 管理员 | 预置账号登录管理后台，录入/编辑题目（文字描述 + 测试用例 zip / 逐条录入），配置每种语言的编译运行参数，查看所有用户提交记录，管理用户状态 |
| 系统运维 | 单机部署，MySQL 开发自建，连接信息走配置文件 |

### 1.2 功能列表（MoSCoW）

**Must Have（核心）**
- 注册/登录/登出
- 题目列表 + 题目详情页（含 Monaco/CodeMirror 在线编辑器）
- 代码在线编辑 + "运行测试"（仅跑样例）+ "提交判题"（跑全部测试用例）
- C/C++ 代码编译执行（nsjail：Linux namespaces + seccomp + cgroups）沙箱隔离
- 标准输入输出比对（严格 diff 比对）+ 支持单题可配置时间限制和内存限制
- 判题结果展示：Accepted / Wrong Answer / TLE / MLE / RE / CE / System Error，并展示失败用例的期望输出 vs 实际输出
- 用户查看自己的提交历史
- 判题队列：最多并发 4 个判题任务，其余排队
- 管理员后台：CRUD 题目、上传测试用例 zip、配置语言编译运行参数、查看所有提交记录、用户管理

**Should Have（重要）**
- HTTP Handler 抽象层，为后续迁移 Muduo 网络库做准备
- 判题结果轮询 / loading 动画
- 管理员管理题目状态（草稿/发布/下架）
- 单元测试模块，使用 googletest；每完成一个功能模块后同步补齐对应单元测试

**Could Have（锦上添花）**
- 测试用例运行日志查看
- 排行榜

**Won't Have Now（暂不做）**
- 多语言支持（架构预留扩展接口，首版仅 C/C++）
- Muduo 网络库（下个大版本）

---

## 2. 架构图

### 2.1 整体架构（ASCII）

```
┌─────────────────────────────────────────────────────┐
│                    前端 (浏览器)                      │
│  HTML/CSS/JS + Monaco Editor                        │
│  登录 → 题目列表 → 题目详情 → 提交结果                │
└─────────┬──────────────────────────┬────────────────┘
          │ HTTP (REST API)          │ 静态文件
          ▼                          ▼
┌─────────────────────────────────────────────────────┐
│              cpp-httplib Web Server                   │
│  ┌──────────┐  ┌──────────────┐  ┌────────────────┐  │
│  │ Router   │  │ AuthMiddleware│  │ FileServer     │  │
│  │ (URL路由)│  │ (Session鉴权) │  │ (前端静态文件)  │  │
│  └────┬─────┘  └──────┬───────┘  └────────────────┘  │
│       │               │                               │
│  ┌────▼───────────────▼───────────────────────────┐  │
│  │              Handler 抽象层                      │  │
│  │  AuthHandler / ProblemHandler /                 │  │
│  │  SubmissionHandler / AdminHandler               │  │
│  └────────────────────┬───────────────────────────┘  │
│                       │                               │
│  ┌────────────────────▼───────────────────────────┐  │
│  │           JudgeManager (判题调度器)              │  │
│  │  - 并发限制: 最多 4 个同时判题                   │  │
│  │  - 任务队列 + 线程池                             │  │
│  │  - fork+exec 调用 judger CLI                     │  │
│  └────────────────────┬───────────────────────────┘  │
│                       │ fork+exec                     │
│  ┌────────────────────▼───────────────────────────┐  │
│  │          judger CLI (独立子进程)                 │  │
│  │  - 编译: g++/gcc                                │  │
│  │  - 沙箱: nsjail + cgroups + setrlimit            │  │
│  │  - 判题: 标准输入输出比对                        │  │
│  │  - 结果: JSON 写入 stdout                        │  │
│  └─────────────────────────────────────────────────┘  │
│                       │                               │
│  ┌────────────────────▼───────────────────────────┐  │
│  │              MySQL 数据库                        │  │
│  │  users / problems / test_cases /                │  │
│  │  submissions / languages / admin_logs           │  │
│  └─────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────┘
```

### 2.2 判题流水线

```
用户提交代码
     │
     ▼
Web Server 接收请求 → 写入 submissions 表 (status=PENDING)
     │
     ▼
JudgeManager 入队 → 队列排队等待
     │
     ▼ (轮到执行)
fork+exec judger CLI
     │
     ▼
编译阶段: g++/gcc 编译 → 失败则 CE 返回
     │
     ▼ (编译成功)
对每个测试用例:
  ├─ nsjail 启动隔离环境（user/pid/mount/net namespaces + seccomp + cgroups）
  ├─ setrlimit 设置 CPU/MEM 限制
  ├─ 通过管道/stdin 注入输入数据
  ├─ 运行用户程序
  ├─ 捕获 stdout/stderr + 资源使用量
  │
  ▼
比对输出: diff 比对 → WA/AC/TLE/MLE/RE
     │
     ▼
judger CLI 输出 JSON 结果 → JudgeManager 解析
     │
     ▼
更新 submissions 表 (status + 详细结果 JSON)
     │
     ▼
前端轮询 → 展示结果
```

### 2.3 目录结构

```
oj-vibe-coding/
├── CMakeLists.txt
├── config/
│   └── config.yaml              # MySQL连接、端口、JWT密钥等（YAML）
├── web/                         # 前端静态文件
│   ├── index.html
│   ├── css/
│   │   └── style.css
│   ├── js/
│   │   ├── app.js               # SPA 路由
│   │   ├── api.js               # API 请求封装
│   │   ├── components/
│   │   │   ├── login.js
│   │   │   ├── problem-list.js
│   │   │   ├── problem-detail.js
│   │   │   ├── submission-result.js
│   │   │   └── history.js
│   │   └── utils.js
│   └── lib/
│       └── monaco/              # Monaco Editor (或CDN引用)
├── src/
│   ├── main.cpp                 # 入口
│   ├── server.cpp/h             # httplib Server 封装
│   ├── router.cpp/h             # URL 路由注册
│   ├── middleware/
│   │   └── auth.cpp/h           # Session/JWT 鉴权中间件
│   ├── handler/                 # Handler 抽象层（为 Muduo 迁移准备）
│   │   ├── handler_base.h       # Handler 基类/接口
│   │   ├── auth_handler.cpp/h
│   │   ├── problem_handler.cpp/h
│   │   ├── submission_handler.cpp/h
│   │   └── admin_handler.cpp/h
│   ├── judge/
│   │   ├── judge_manager.cpp/h  # 判题调度器
│   │   └── judger_cli.cpp       # 判题 CLI 独立程序
│   ├── model/
│   │   ├── user.cpp/h
│   │   ├── problem.cpp/h
│   │   ├── submission.cpp/h
│   │   ├── test_case.cpp/h
│   │   └── language.cpp/h
│   ├── db/
│   │   ├── mysql_pool.cpp/h     # MySQL 连接池
│   │   └── dao/                 # 数据访问对象
│   │       ├── user_dao.cpp/h
│   │       ├── problem_dao.cpp/h
│   │       ├── submission_dao.cpp/h
│   │       ├── test_case_dao.cpp/h
│   │       └── language_dao.cpp/h
│   └── utils/
│       ├── config.cpp/h         # 配置封装（配置文件读取、环境变量扩展等）
│       ├── logger.cpp/h         # 日志封装（统一输出格式、日志级别、文件/控制台输出）
│       ├── crypto.cpp/h         # 密码哈希 (bcrypt/argon2)
│       └── json_helper.cpp/h    # JSON 序列化
├── run/                         # 运行期目录（默认用于 judger work_dir：编译产物/临时文件）
├── testdata/                    # 测试用例文件存储
│   └── {problem_id}/
│       ├── 1.in
│       ├── 1.out
│       ├── 2.in
│       └── 2.out
├── scripts/
│   └── init_db.sql
├── tests/                       # 单元测试（googletest）
│   ├── utils/
│   ├── db/
│   ├── handler/
│   └── judge/
└── SPEC.md
```

### 2.4 配置文件（YAML）

统一使用 `config/config.yaml` 作为服务端配置入口；`utils/config.cpp/h` 负责解析 YAML（可选支持 `${ENV_VAR}` 形式的环境变量展开）。

**字段结构（仅字段名）：**

```yaml
server:
  host:
  port:

mysql:
  host:
  port:
  user:
  password:
  database:
  pool:
    max_connections:
    connect_timeout_ms:

auth:
  jwt:
    secret:
    expires_seconds:

judge:
  max_concurrency:
  nsjail_config:
  work_dir:
  default_time_limit_ms:
  default_memory_limit_kb:

logging:
  level:
  to_stdout:
  file:
```

---

## 3. 模块详细设计

### 3.1 判题沙箱设计（judger CLI）

**独立进程架构：**
- Web Server 通过 `fork() + exec()` 调用 `judger_cli` 独立可执行文件
- 输入：JSON 格式参数（通过命令行参数或 stdin 传入）
- 输出：JSON 格式判题结果（写入 stdout，Web Server 解析）

**输入格式（judger CLI）：**
```json
{
  "language": "cpp",
  "source_code": "#include<iostream>...",
  "time_limit_ms": 1000,
  "memory_limit_kb": 262144,
  "test_cases": [
    {"input": "1 2", "expected_output": "3", "id": 1}
  ],
  "nsjail_config": "./config/nsjail.cfg",
  "work_dir": "./run/judge",
  "compile_cmd": "g++ -O2 -std=c++17 {source} -o {output}",
  "run_cmd": "{binary}"
}
```

**输出格式（judger CLI）：**
```json
{
  "status": "ACCEPTED",
  "compile_error": null,
  "results": [
    {
      "test_case_id": 1,
      "status": "WRONG_ANSWER",
      "time_ms": 12,
      "memory_kb": 4096,
      "actual_output": "4",
      "expected_output": "3"
    }
  ],
  "summary": {
    "total": 10,
    "passed": 8,
    "total_time_ms": 120,
    "peak_memory_kb": 8192
  }
}
```

**沙箱策略：**

| 机制 | 用途 |
|------|------|
| nsjail（namespaces + seccomp） | 进程/挂载/用户/网络等隔离，限制系统调用面 |
| cgroups | CPU/内存/进程数等资源隔离与统计 |
| `setrlimit(RLIMIT_CPU)` | CPU 时间硬限制 |
| `setrlimit(RLIMIT_AS)` | 地址空间/内存硬限制（防止内存溢出） |
| `setrlimit(RLIMIT_NPROC)` | 限制子进程数量（防止 fork 炸弹） |
| `setrlimit(RLIMIT_FSIZE)` | 限制输出文件大小 |
| `setrlimit(RLIMIT_NOFILE)` | 限制打开文件描述符数量 |
| 重定向 `stdin/stdout/stderr` | 通过管道注入输入、捕获输出 |
| `alarm()` 作为 `setrlimit` 的补充 | 粗粒度超时兜底 |

**判题流程伪代码：**
```
1. 将源代码写入 work_dir/{submission_id}.cpp
2. fork() → 子进程（编译阶段，建议也放入 nsjail 内执行）:
  a. setrlimit(...)  // 设置各项资源限制
  b. execvp 执行：nsjail --config {nsjail_config} -- {compile_cmd}
3. 父进程等待编译完成 → 检查退出码
4. 编译失败 → 收集 stderr → 返回 CE
5. 编译成功 → 对每个测试用例:
  a. fork() → 子进程在 nsjail 内执行用户程序
   b. 通过管道写入 .in 数据到子进程 stdin
   c. 收集 stdout + 资源使用量 (wait4 + getrusage)
   d. diff 比对 actual_output vs expected_output
   e. 任一用例失败则提前终止（或全跑完，看需求）
6. 汇总结果 → JSON 写入 stdout
```

### 3.2 API 端点设计

**认证模块：**

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/auth/register` | 注册（username + password + email） |
| POST | `/api/auth/login` | 登录，返回 session token |
| POST | `/api/auth/logout` | 登出 |

**题目模块：**

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/problems` | 题目列表（分页 + 搜索） |
| GET | `/api/problems/{id}` | 题目详情（含描述、时间/内存限制、样例） |

**提交模块：**

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/submissions` | 提交代码（source_code + problem_id + language_id + mode: "run"\|"submit"） |
| GET | `/api/submissions/{id}` | 查询判题结果（轮询） |
| GET | `/api/submissions?user_id=x` | 用户提交历史 |

**管理后台：**

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/admin/problems` | 创建题目 |
| PUT | `/api/admin/problems/{id}` | 编辑题目 |
| DELETE | `/api/admin/problems/{id}` | 删除题目 |
| POST | `/api/admin/problems/{id}/testcases` | 逐条添加测试用例 |
| POST | `/api/admin/problems/{id}/testcases/upload` | 上传测试用例 zip |
| GET | `/api/admin/submissions` | 查看所有用户提交 |
| GET | `/api/admin/languages` | 查询语言配置列表 |
| POST | `/api/admin/languages` | 添加语言配置 |
| PUT | `/api/admin/languages/{id}` | 编辑语言配置 |
| PUT | `/api/admin/users/{id}/status` | 禁用/解封用户 |

### 3.3 数据库表结构

```sql
-- 用户表
CREATE TABLE users (
    id          BIGINT AUTO_INCREMENT PRIMARY KEY,
    username    VARCHAR(64)  NOT NULL UNIQUE,
    password    VARCHAR(256) NOT NULL,              -- bcrypt/argon2 hash
    email       VARCHAR(128) NOT NULL DEFAULT '',
    role        ENUM('student', 'admin') DEFAULT 'student',
    status      ENUM('active', 'banned') DEFAULT 'active',
    created_at  DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at  DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
);

-- 编程语言配置表（管理员后台配置，方便扩展）
CREATE TABLE languages (
    id          INT AUTO_INCREMENT PRIMARY KEY,
    name        VARCHAR(32)  NOT NULL,              -- e.g. "C++17"
    extension   VARCHAR(8)   NOT NULL,              -- e.g. "cpp"
    compile_cmd VARCHAR(512) NOT NULL,              -- e.g. "g++ -O2 -std=c++17 {source} -o {output}"
    run_cmd     VARCHAR(512) NOT NULL,              -- e.g. "{binary}"
    enabled     TINYINT(1) DEFAULT 1,
    created_at  DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- 题目表
CREATE TABLE problems (
    id              BIGINT AUTO_INCREMENT PRIMARY KEY,
    title           VARCHAR(256) NOT NULL,
    description     TEXT         NOT NULL,          -- Markdown 格式题目描述
    difficulty      ENUM('easy', 'medium', 'hard') DEFAULT 'medium',
    time_limit_ms   INT          NOT NULL DEFAULT 1000,
    memory_limit_kb INT          NOT NULL DEFAULT 262144, -- 256MB
    status          ENUM('draft', 'published', 'archived') DEFAULT 'draft',
    created_by      BIGINT,
    created_at      DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at      DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    FOREIGN KEY (created_by) REFERENCES users(id)
);

-- 测试用例表
CREATE TABLE test_cases (
    id          BIGINT AUTO_INCREMENT PRIMARY KEY,
    problem_id  BIGINT NOT NULL,
    is_sample   TINYINT(1) DEFAULT 0,               -- 样例用例（Run Code 模式使用）
    input       TEXT   NOT NULL,
    output      TEXT   NOT NULL,
    sort_order  INT DEFAULT 0,
    created_at  DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (problem_id) REFERENCES problems(id) ON DELETE CASCADE
);

-- 提交记录表
CREATE TABLE submissions (
    id          BIGINT AUTO_INCREMENT PRIMARY KEY,
    user_id     BIGINT NOT NULL,
    problem_id  BIGINT NOT NULL,
    language_id INT    NOT NULL,
    source_code TEXT   NOT NULL,
    mode        ENUM('run', 'submit') DEFAULT 'submit',
    status      ENUM('pending', 'running', 'accepted', 'wrong_answer',
                     'time_limit_exceeded', 'memory_limit_exceeded',
                     'runtime_error', 'compile_error', 'system_error')
                     DEFAULT 'pending',
    result_json JSON,                                  -- 详细判题结果
    time_ms     INT DEFAULT 0,
    memory_kb   INT DEFAULT 0,
    created_at  DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id)     REFERENCES users(id),
    FOREIGN KEY (problem_id)  REFERENCES problems(id),
    FOREIGN KEY (language_id) REFERENCES languages(id)
);
```

---

## 4. TODO 清单（按优先级排序）

### P0 — 基础设施
- [ ] **P0-1** 搭建 CMake 项目骨架，引入 cpp-httplib、nlohmann/json、MySQL connector
- [ ] **P0-2** 实现配置封装模块 `config.cpp/h`
- [ ] **P0-3** 实现日志封装模块 `logger.cpp/h`
- [ ] **P0-4** 实现 MySQL 连接池 `mysql_pool.cpp/h`
- [ ] **P0-5** 编写 `scripts/init_db.sql` 建表脚本并初始化数据库
- [ ] **P0-6** 引入 googletest，搭建 `tests/` 单元测试目录与基础测试入口
- [ ] **P0-7** 实现各 DAO 层（user_dao、problem_dao、submission_dao、test_case_dao、language_dao）

### P1 — 判题核心
- [ ] **P1-1** 实现 judger CLI：编译模块（g++/gcc 调用）
- [ ] **P1-2** 实现 judger CLI：沙箱执行（方案 A：nsjail + seccomp + cgroups + setrlimit）
- [ ] **P1-3** 实现 judger CLI：输出比对（严格 diff）
- [ ] **P1-4** 实现 judger CLI：结果汇总 JSON 输出
- [ ] **P1-5** 实现 JudgeManager：队列 + 并发控制（4 上限） + fork/exec 调用 judger CLI
- [ ] **P1-6** 准备 nsjail 配置（seccomp policy、挂载白名单/只读、网络隔离、cgroups 限制）

### P2 — 用户系统 & 鉴权
- [ ] **P2-1** 实现密码哈希工具 `crypto.cpp/h`（bcrypt/argon2）
- [ ] **P2-2** 实现注册/登录/登出 API
- [ ] **P2-3** 实现 Auth 中间件（Session/Token 机制）
- [ ] **P2-4** 实现 Handler 抽象基类 `handler_base.h`

### P3 — 题目 & 提交 API
- [ ] **P3-1** 实现 ProblemHandler：题目列表（分页）、题目详情
- [ ] **P3-2** 实现 SubmissionHandler：提交代码（Run/Submit 模式差异化处理）
- [ ] **P3-3** 实现 SubmissionHandler：判题结果查询（轮询）、提交历史
- [ ] **P3-4** 实现"运行测试"模式：仅跑 `is_sample=1` 的测试用例

### P4 — 前端
- [ ] **P4-1** 搭建静态页面框架 SPA（index.html + app.js 路由）
- [ ] **P4-2** 实现登录/注册页
- [ ] **P4-3** 实现题目列表页（分页、搜索、难度标记）
- [ ] **P4-4** 实现题目详情页 + 集成 Monaco Editor
- [ ] **P4-5** 实现提交结果展示页（loading 动画 → 结果表格，含失败用例详情）
- [ ] **P4-6** 实现用户提交历史页
- [ ] **P4-7** 实现 API 请求封装（api.js）与错误处理

### P5 — 管理后台
- [ ] **P5-1** 实现 AdminHandler：题目 CRUD（含 Markdown 渲染预览）
- [ ] **P5-2** 实现 AdminHandler：测试用例逐条录入 + zip 上传解压
- [ ] **P5-3** 实现 AdminHandler：语言配置 CRUD
- [ ] **P5-4** 实现 AdminHandler：查看所有提交记录
- [ ] **P5-5** 实现用户管理（禁用/解封）
- [ ] **P5-6** 管理员前端管理后台页面

### P6 — 打磨 & 预迁移
- [ ] **P6-1** 完善错误页面与全局异常处理
- [ ] **P6-2** 判题超时、系统异常等边缘案例完善
- [ ] **P6-3** 性能优化：数据库索引、连接复用
- [ ] **P6-4** cpp-httplib 静态文件 serving 配置
- [ ] **P6-5** Handler 抽象层 Review，确保 Muduo 迁移友好

---

## 5. 验收标准

### 5.1 功能验收

| 测试点 | 验收条件 |
|--------|----------|
| 用户注册/登录 | 注册成功后可正常登录，错误密码登录被拒绝 |
| 题目浏览 | 分页正确，题目详情包含完整描述和样例 |
| 代码运行测试 | 点击"Run Code"，样例用例在判题沙箱中执行，返回实际输出 |
| 代码提交判题 | 点击"Submit"，全部测试用例执行，返回 AC/WA/TLE/... 状态 |
| WA 详情展示 | Wrong Answer 时，显示失败用例的 expected vs actual 输出 |
| CE 展示 | 编译错误时，显示编译器完整错误信息 |
| 并发判题 | 10 人同时提交，仅有 4 个任务并发执行，其余排队 |
| 判题队列 | 排队任务顺序执行，结果正确 |
| 管理后台 | 管理员可以新增题目、上传 zip 测试用例、配置语言参数 |
| 提交历史 | 用户可查看自己所有提交记录及每次提交的判题结果 |
| 语言扩展 | 在管理后台新增语言配置后，前端可选择新语言提交 |

### 5.2 性能验收

| 测试点 | 验收条件 |
|--------|----------|
| 判题耗时 | 100 人同时提交时，95% 的简单算法题在 3 秒内返回结果 |
| 前端首屏 | 题目列表页首次加载 ≤ 2 秒 |
| 数据库 | 1000 条提交记录下查询历史响应 ≤ 500ms |
| 内存泄漏 | 连续运行 24 小时，内存无持续增长趋势 |

### 5.3 安全验收

| 测试点 | 验收条件 |
|--------|----------|
| nsjail 隔离 | 用户程序无法读取宿主机 `/etc/passwd` 等敏感文件（挂载隔离/白名单挂载） |
| 资源限制 | 提交死循环代码 (`while(1){}`) 在指定时间限制内被杀死 |
| 内存限制 | 提交无限 `malloc` 代码在内存限制内被杀死 |
| fork 炸弹 | 提交 `fork()` 循环代码，系统进程数不爆炸 |
| SQL 注入 | 所有数据库操作使用参数化查询，`' OR '1'='1` 无法绕过登录 |
| XSS | 题目描述中的 HTML 标签被正确转义，不存在存储型 XSS |
| 密码安全 | 数据库中密码以 bcrypt/argon2 hash 存储，不以明文存储 |
| 权限校验 | 普通用户无法访问 `/api/admin/*` 接口 |
