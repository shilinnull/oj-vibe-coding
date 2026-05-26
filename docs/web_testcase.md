# Web UI 测试用例

## 1. 文档目的

本文档用于验证在线判题系统前端页面是否满足 [SPEC.md](../SPEC.md) 中定义的核心用户场景，包括注册/登录、题目浏览、在线编辑与提交、提交结果展示、历史记录、管理员后台和异常处理。

## 2. 测试范围

- 登录页、注册页、登出流程
- 题目列表页、题目详情页
- 在线编辑器、运行测试、提交判题
- 提交结果页、提交历史页
- 管理后台页面
- 空数据、404、后端异常等边界场景
- 基础响应式与可用性检查

## 3. 测试前置条件

- 服务已启动，前端可通过浏览器访问。
- 数据库已初始化，且存在至少以下测试数据：
  - 发布题目：900001、900002、900003
  - 草稿题目：900004
  - 样例测试用例已为发布题目准备好。
- 管理员账号已预置。
- 普通用户账号可注册或已存在。
- 后端 API 默认地址为 `http://150.158.91.33:8080`。

## 4. 测试数据建议

### 4.1 账号

- 普通用户：`ui_user_20260526`
- 普通用户密码：`pass1234`
- 管理员：`admin`
- 管理员密码：admin123

### 4.2 题目

- `900001`：A + B Problem
- `900002`：Array Rotate
- `900003`：Big Integer Multiply
- `900004`：Draft Hidden Problem

### 4.3 提交结果样例

若环境中已存在提交记录，可优先使用已有提交 ID；若无，则建议准备以下状态覆盖：

- Accepted
- Wrong Answer
- Time Limit Exceeded
- Memory Limit Exceeded
- Runtime Error
- Compile Error
- System Error
- Running
- Pending

## 5. 测试用例

### 5.1 认证与会话

| 编号 | 页面 | 场景 | 操作步骤 | 预期结果 |
|------|------|------|----------|----------|
| UI-AUTH-001 | 注册页 | 注册页基础展示 | 打开注册页 | 显示用户名、密码、邮箱输入框以及注册按钮，页面无布局错位 |
| UI-AUTH-002 | 注册页 | 成功注册 | 输入未使用的用户名、密码和邮箱，点击注册 | 注册成功，返回登录态或跳转到登录页，页面提示成功信息 |
| UI-AUTH-003 | 注册页 | 重复用户名 | 使用已存在用户名注册 | 页面提示用户名已存在或等价错误，不发生跳转 |
| UI-AUTH-004 | 登录页 | 成功登录 | 输入正确用户名和密码后登录 | 登录成功，顶部显示用户名，进入题目相关页面 |
| UI-AUTH-005 | 登录页 | 错误密码 | 输入正确用户名、错误密码登录 | 页面提示认证失败，不登录成功 |
| UI-AUTH-006 | 顶部导航 | 登出 | 登录后点击登出 | 会话清理，返回未登录状态，受保护页面不再可直接访问 |

### 5.2 题目列表页

| 编号 | 页面 | 场景 | 操作步骤 | 预期结果 |
|------|------|------|----------|----------|
| UI-PROB-001 | 题目列表页 | 基础列表加载 | 登录后打开题目列表页 | 正常显示题目卡片列表，数据来自后端接口 |
| UI-PROB-002 | 题目列表页 | 仅显示已发布题目 | 检查列表内容 | 显示 900001、900002、900003，不显示 900004 草稿题 |
| UI-PROB-003 | 题目列表页 | 分页信息展示 | 切换分页 | 页面显示当前页码，不暴露 offset、limit 等调试参数 |
| UI-PROB-004 | 题目列表页 | 搜索题目 | 输入关键字 `Rotate` 搜索 | 仅显示匹配题目，至少可筛出 900002 |
| UI-PROB-005 | 题目列表页 | 难度筛选 easy | 选择 easy | 仅显示 easy 题目，例如 900001 |
| UI-PROB-006 | 题目列表页 | 难度筛选 hard | 选择 hard | 仅显示 hard 题目，例如 900003 |
| UI-PROB-007 | 题目列表页 | 卡片元信息 | 查看任一题目卡片 | 显示时间限制、内存限制、状态等信息 |
| UI-PROB-008 | 题目列表页 | 点击题目卡片 | 点击题目 900001 | 跳转到对应题目详情页 |

### 5.3 题目详情页与编辑器

| 编号 | 页面 | 场景 | 操作步骤 | 预期结果 |
|------|------|------|----------|----------|
| UI-DETAIL-001 | 题目详情页 | 标题展示 | 打开题目 900003 详情页 | 标题包含题号与题名，如 `#900003 Big Integer Multiply` |
| UI-DETAIL-002 | 题目详情页 | 描述渲染 | 查看题面描述 | 描述完整展示，特殊字符和 HTML 内容被正确转义，不执行脚本 |
| UI-DETAIL-003 | 题目详情页 | 样例展示 | 查看样例区域 | 样例输入输出按表格或等价结构展示，支持多行文本 |
| UI-DETAIL-004 | 题目详情页 | 编辑器加载 | 打开编辑器区域 | 编辑器可正常输入代码，不出现空白或报错 |
| UI-DETAIL-005 | 题目详情页 | 切换语言 | 切换语言下拉框 | 可切换 C++17 / C11 等语言选项，选项变化生效 |
| UI-DETAIL-006 | 题目详情页 | 代码编辑保留 | 输入代码后切换页面再返回 | 已输入内容保持或按设计明确提示是否清空 |
| UI-DETAIL-007 | 题目详情页 | 题目权限 | 未登录访问题目详情 | 若设计要求登录后访问，则应跳转登录页或提示无权限；若允许匿名浏览，则仍可查看题面但不可提交 |

### 5.4 运行测试与提交判题

| 编号 | 页面 | 场景 | 操作步骤 | 预期结果 |
|------|------|------|----------|----------|
| UI-JUDGE-001 | 题目详情页 | 运行测试按钮 | 输入一段可通过样例的代码并点击运行测试 | 创建运行测试任务，页面进入 loading 或轮询状态 |
| UI-JUDGE-002 | 题目详情页 | 运行测试结果跳转 | 运行测试完成 | 跳转到提交结果页，显示样例测试结果 |
| UI-JUDGE-003 | 题目详情页 | 提交判题按钮 | 输入完整代码并点击提交判题 | 创建正式提交任务，页面进入 loading 或轮询状态 |
| UI-JUDGE-004 | 提交结果页 | Accepted | 打开 AC 提交结果 | 状态为 Accepted，汇总信息 total/passed/time/memory 正确 |
| UI-JUDGE-005 | 提交结果页 | Wrong Answer | 打开 WA 提交结果 | 显示失败用例的 expected vs actual 输出 |
| UI-JUDGE-006 | 提交结果页 | Time Limit Exceeded | 打开 TLE 提交结果 | 状态展示为 TLE，页面中应有超时提示或风险样式 |
| UI-JUDGE-007 | 提交结果页 | Memory Limit Exceeded | 打开 MLE 提交结果 | 状态展示为 MLE，内存相关字段有高亮或警示 |
| UI-JUDGE-008 | 提交结果页 | Runtime Error | 打开 RE 提交结果 | 状态展示为 RE，并显示运行错误信息 |
| UI-JUDGE-009 | 提交结果页 | Compile Error | 打开 CE 提交结果 | 显示编译错误文本块，错误信息可完整查看 |
| UI-JUDGE-010 | 提交结果页 | System Error | 打开 System Error 提交结果 | 显示系统错误状态，且提示任务未正常完成 |
| UI-JUDGE-011 | 提交结果页 | Running | 打开 Running 提交结果 | 显示正在判题中的 loading 动画或状态指示，并持续轮询 |
| UI-JUDGE-012 | 提交结果页 | Pending | 打开 Pending 提交结果 | 显示排队中的 loading 状态，并持续轮询 |
| UI-JUDGE-013 | 提交结果页 | 轮询停止 | 打开一个终态提交结果 | 结果稳定后停止轮询，不持续重复请求 |
| UI-JUDGE-014 | 提交结果页 | 无效提交 ID | 访问不存在的提交结果页 | 显示错误提示，不出现空白页或无限 loading |

### 5.5 提交历史页

| 编号 | 页面 | 场景 | 操作步骤 | 预期结果 |
|------|------|------|----------|----------|
| UI-HIST-001 | 提交历史页 | 列表展示 | 打开提交历史页 | 显示当前用户的提交记录列表 |
| UI-HIST-002 | 提交历史页 | 状态展示 | 检查多条历史记录 | 状态徽标、时间、题目、结果信息显示正常 |
| UI-HIST-003 | 提交历史页 | 分页展示 | 翻页查看历史 | 页面显示页码，不出现 offset、limit 等调试字样 |
| UI-HIST-004 | 提交历史页 | 结果跳转 | 点击某条提交 ID | 可跳转到对应提交结果页 |
| UI-HIST-005 | 提交历史页 | 用户隔离 | 切换到其他用户登录后查看 | 只能看到当前用户自己的提交历史 |

### 5.6 管理后台

| 编号 | 页面 | 场景 | 操作步骤 | 预期结果 |
|------|------|------|----------|----------|
| UI-ADMIN-001 | 管理后台首页 | 入口可访问 | 使用管理员账号登录后访问后台 | 成功进入管理后台首页或仪表盘 |
| UI-ADMIN-002 | 管理后台首页 | 普通用户限制 | 使用普通用户访问后台地址 | 被拒绝访问、跳转登录页或显示无权限提示 |
| UI-ADMIN-003 | 题目管理页 | 新建题目 | 创建新题目并保存 | 题目成功创建，列表中可见 |
| UI-ADMIN-004 | 题目管理页 | 编辑题目 | 修改题目标题或描述并保存 | 修改成功，详情页展示更新内容 |
| UI-ADMIN-005 | 题目管理页 | 状态管理 | 将题目从 draft 切换到 published | 题目状态更新，前台列表可见性变化正确 |
| UI-ADMIN-006 | 测试用例页 | 逐条录入 | 新增一组测试用例并保存 | 用例成功保存，题目详情页样例同步可见或按设计生效 |
| UI-ADMIN-007 | 测试用例页 | zip 上传 | 上传测试用例 zip | 上传成功并解压入库，页面给出成功反馈 |
| UI-ADMIN-008 | 语言配置页 | 新增语言 | 添加新的语言配置 | 新语言出现在语言列表，并可在前端选择 |
| UI-ADMIN-009 | 用户管理页 | 禁用用户 | 禁用某个普通用户账号 | 用户状态变更后无法继续正常登录或提交 |
| UI-ADMIN-010 | 提交记录页 | 查看全部提交 | 打开管理员提交记录页 | 可查看所有用户提交记录，不受普通用户隔离限制 |

### 5.7 异常与边界场景

| 编号 | 页面 | 场景 | 操作步骤 | 预期结果 |
|------|------|------|----------|----------|
| UI-ERR-001 | 全局 | 后端断开 | 在后端不可用时刷新页面 | 页面显示错误提示，不出现浏览器白屏 |
| UI-ERR-002 | 全局 | 数据库异常 | 模拟数据库连接失败后访问页面 | 页面显示明确错误信息或统一错误页 |
| UI-ERR-003 | 全局 | 404 页面 | 访问不存在的前端路由 | 显示统一 404 页面或回退到首页并提示 |
| UI-ERR-004 | 提交结果页 | 错误后停止轮询 | 触发结果查询错误 | 页面停止轮询并展示错误状态 |
| UI-ERR-005 | 题目详情页 | 非法输入 | 在编辑器中提交空代码或极短代码 | 前端应拦截或后端返回明确校验错误 |

### 5.8 基础体验与兼容性

| 编号 | 页面 | 场景 | 操作步骤 | 预期结果 |
|------|------|------|----------|----------|
| UI-UX-001 | 全局 | 首屏加载 | 首次打开首页 | 页面在可接受时间内完成首屏渲染 |
| UI-UX-002 | 全局 | 导航一致性 | 在题目、历史、后台之间切换 | 导航高亮状态正确，路径与页面内容一致 |
| UI-UX-003 | 题目详情页 | 小屏适配 | 使用窄屏浏览器窗口访问 | 布局不严重溢出，核心操作可用 |
| UI-UX-004 | 提交结果页 | 信息层级 | 查看复杂结果页 | 状态、摘要、失败明细层级清晰，便于定位错误 |
| UI-UX-005 | 全局 | 文本转义 | 查看题面或结果中的特殊字符 | HTML 标签、脚本内容不直接执行，显示为文本 |

## 6. 验收建议

- 先执行认证、题目列表、题目详情、提交结果四条主链路。
- 再补齐管理员后台、异常页和移动端/窄屏适配检查。
- 若提交结果页存在轮询逻辑，需额外确认终态后停止请求，避免重复刷接口。

## 7. 备注

- 若实际测试数据与本文档中的题号或提交 ID 不一致，可保留测试步骤不变，仅替换对应数据 ID。
- 本文档偏向手工 UI 验收，也可直接转化为自动化 E2E 用例。

## 8. Playwright 可执行测试清单

### 8.1 执行约定

- 基础地址：`http://127.0.0.1:8080`
- 路由方式：hash 路由，例如 `#/login`、`#/problems`、`#/history`
- 推荐断言优先级：`id` > `data-link` > 文本内容 > 列表项位置
- 建议在测试前清理浏览器 `localStorage`，避免登录态互相污染

### 8.2 建议拆分的 Playwright 文件

- `tests/e2e/auth.spec.ts`
- `tests/e2e/problem-list.spec.ts`
- `tests/e2e/problem-detail.spec.ts`
- `tests/e2e/submission-result.spec.ts`
- `tests/e2e/history.spec.ts`
- `tests/e2e/admin.spec.ts`
- `tests/e2e/error-handling.spec.ts`

### 8.3 公共定位器清单

- 登录页：`#login-form`、`#login-submit`
- 注册页：`#register-form`、`#register-submit`
- 顶部栏：`#user-badge`、`#logout-btn`、`#topnav`、`#admin-link`
- 题目列表页：`#problem-list`、`#problem-count`、`#problem-pager-info`、`#problem-search`、`#problem-difficulty`、`#problem-prev`、`#problem-next`、`.problem-card`
- 题目详情页：`#problem-title`、`#problem-description`、`#problem-meta`、`#problem-samples`、`#language-select`、`#btn-run`、`#btn-submit`、`#btn-back-list`
- 提交结果页：`#result-title`、`#result-status`、`#result-loading`、`#result-summary`、`#result-cases`、`#compile-error`、`#btn-to-history`、`#btn-to-problem`
- 提交历史页：`#history-user`、`#history-body`、`#history-pager-info`、`#history-prev`、`#history-next`
- 管理后台首页：`#admin-refresh`、`#admin-user-id`、`#admin-user-status`、`#admin-user-status-form`、`#admin-submissions-loading`、`#admin-submissions-tbody`
- 题目编辑页：`#admin-problem-select`、`#admin-problem-new`、`#admin-problem-save`、`#admin-problem-delete`、`#admin-add-tc`、`#admin-upload-zip`、`#admin-problem-preview`
- 语言配置页：`#admin-lang-refresh`、`#admin-lang-new`、`#admin-lang-save`、`#admin-lang-tbody`

### 8.4 可直接落地的测试清单

| 文件 | 用例名 | 路由 | 建议操作 | 关键断言 |
|------|--------|------|----------|----------|
| auth.spec.ts | 注册页可访问 | `#/register` | 打开注册页 | `#register-form`、`#register-submit` 可见 |
| auth.spec.ts | 注册成功后自动登录 | `#/register` | 填写新用户名并提交 | 跳转到 `#/problems`，`#user-badge` 显示用户名 |
| auth.spec.ts | 登录成功 | `#/login` | 输入正确账号密码并提交 | 跳转到 `#/problems`，`#user-badge` 非“未登录” |
| auth.spec.ts | 错误密码提示 | `#/login` | 输入错误密码并提交 | 页面出现 toast 或错误提示，仍停留在登录页 |
| auth.spec.ts | 登出后回到登录页 | 任意已登录页 | 点击 `#logout-btn` | `#user-badge` 变为“未登录”，hash 变为 `#/login` |
| problem-list.spec.ts | 列表基础加载 | `#/problems` | 进入题目列表页 | `#problem-list` 有题目卡片，`#problem-count` 大于 0 |
| problem-list.spec.ts | 仅显示已发布题 | `#/problems` | 读取列表内容 | 出现 `#900001/#900002/#900003`，不出现 `900004` |
| problem-list.spec.ts | 搜索过滤 | `#/problems` | 在 `#problem-search` 输入 `Rotate` | 只保留匹配题目，至少包含 `#900002` |
| problem-list.spec.ts | 难度过滤 | `#/problems` | 在 `#problem-difficulty` 选 `easy` / `hard` | 题目卡片只保留对应难度 |
| problem-list.spec.ts | 翻页信息 | `#/problems` | 点击 `#problem-next` / `#problem-prev` | `#problem-pager-info` 显示“第 N 页” |
| problem-detail.spec.ts | 题目详情加载 | `#/problems/900003` | 打开题目详情 | `#problem-title` 为 `#900003 Big Integer Multiply` |
| problem-detail.spec.ts | 题面转义 | `#/problems/900003` | 检查描述区域 | `#problem-description` 显示转义后的 HTML，不触发脚本 |
| problem-detail.spec.ts | 样例表展示 | `#/problems/900003` | 检查 `#problem-samples` | 表格存在输入/输出列，样例支持多行文本 |
| problem-detail.spec.ts | 语言切换 | `#/problems/900003` | 切换 `#language-select` | 编辑器语言和下拉选项联动生效 |
| problem-detail.spec.ts | 运行测试并跳转 | `#/problems/900001` | 输入可通过样例的代码，点 `#btn-run` | 跳转到 `#/submissions/{id}`，结果页显示运行中/结果 |
| problem-detail.spec.ts | 提交判题并跳转 | `#/problems/900001` | 输入完整代码，点 `#btn-submit` | 跳转到 `#/submissions/{id}` |
| submission-result.spec.ts | Accepted 结果 | `#/submissions/{id}` | 打开 AC 提交 | `#result-status` 显示 Accepted，汇总区有 total/passed/time/memory |
| submission-result.spec.ts | Wrong Answer 结果 | `#/submissions/{id}` | 打开 WA 提交 | `#result-cases` 中出现 expected vs actual |
| submission-result.spec.ts | Compile Error 结果 | `#/submissions/{id}` | 打开 CE 提交 | `#compile-error` 可见且包含编译器错误文本 |
| submission-result.spec.ts | Running/Pending 轮询 | `#/submissions/{id}` | 打开未终态提交 | `#result-loading` 可见，结果页定时刷新 |
| submission-result.spec.ts | 终态后停止轮询 | `#/submissions/{id}` | 等待结果变成终态 | `#result-loading` 隐藏，不再继续轮询 |
| history.spec.ts | 历史页基础展示 | `#/history` | 打开提交历史 | `#history-user` 显示用户名，`#history-body` 有记录 |
| history.spec.ts | 历史页分页 | `#/history` | 点击 `#history-next` / `#history-prev` | `#history-pager-info` 显示当前页码 |
| history.spec.ts | 历史页跳转结果 | `#/history` | 点击提交 ID 链接 | 跳转到 `#/submissions/{id}` |
| admin.spec.ts | 管理后台入口可见 | `#/admin` | 使用管理员账号登录后打开 | `#admin-link` 可见，进入后台页面 |
| admin.spec.ts | 普通用户拒绝访问后台 | `#/admin` | 使用普通用户访问 | 被重定向到题目页或登录页，并提示无权限 |
| admin.spec.ts | 题目创建与保存 | `#/admin/problems` | 选择新建，填写题目并保存 | 新题目创建成功，进入可编辑状态 |
| admin.spec.ts | 题目状态切换 | `#/admin/problems` | 编辑题目状态为 published | 保存后前台列表可见性变化符合预期 |
| admin.spec.ts | 测试用例逐条录入 | `#/admin/problems` | 添加一条样例用例 | `#admin-testcases-tbody` 刷新后可见新记录 |
| admin.spec.ts | zip 上传测试用例 | `#/admin/problems` | 选择 zip 文件并上传 | 上传成功提示出现，测试用例列表更新 |
| admin.spec.ts | 语言配置新增 | `#/admin/languages` | 新增语言并保存 | `#admin-lang-tbody` 中出现新语言 |
| admin.spec.ts | 用户禁用 | `#/admin` | 提交用户 ID 与禁用状态 | 用户状态更新成功，前端提示成功 |
| error-handling.spec.ts | 404 路由 | `#/not-found-page` | 打开不存在路由 | 显示统一 404 页面或页面不存在提示 |
| error-handling.spec.ts | 无效提交 ID | `#/submissions/999999` | 打开不存在提交 | 显示错误提示，不出现白屏 |
| error-handling.spec.ts | 后端异常提示 | 任意 API 失败场景 | 模拟后端不可用 | 页面显示错误提示并停止加载 |

### 8.5 建议的断言顺序

1. 先断言路由是否正确跳转，再断言核心容器是否存在。
2. 再断言列表数量、标题文本、按钮状态和提示文案。
3. 对轮询型页面，最后断言 loading 状态在终态后消失。

### 8.6 适合补充到自动化脚本的共用检查

- 登录后 `localStorage` 中应存在 `oj_token`、`oj_user_id`、`oj_username`。
- 管理员账号登录后应有 `#admin-link` 可见。
- 题目详情页点击返回列表后，hash 应回到 `#/problems`。
- 历史页点击提交 ID 后，应能稳定跳转到结果页。
- 提交结果页在终态后应停止重复请求。
