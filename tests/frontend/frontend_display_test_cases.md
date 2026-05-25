# 前端页面显示综合测试清单

## 1. 数据准备
1. 先初始化基础表结构：
   - `mysql -u root -p < scripts/init_db.sql`
2. 导入前端测试种子数据：
   - `mysql -u root -p oj < tests/frontend/seed_frontend_data.sql`
3. 启动服务后访问：
   - `http://127.0.0.1:8080/`

## 2. 登录与会话
1. 注册页面展示正常，输入框和按钮样式完整。
2. 登录成功后顶部显示用户名，不显示内部 ID（例如不应出现 `(#2)`）。
3. 顶部导航可在题目/历史间切换，激活态正确。

## 3. 题目列表页（#/problems）
1. 显示 3 道 `published` 题（900001~900003），不显示 900004（draft）。
2. 分页区域显示“第 N 页”，不出现 `offset=...`、`limit=...`。
3. 搜索关键字：
   - 输入 `Rotate` 仅显示 900002。
4. 难度筛选：
   - 选 `easy` 仅显示 900001。
   - 选 `hard` 仅显示 900003。
5. 题目卡片元信息显示完整：时间限制、内存限制、状态。

## 4. 题目详情页（#/problems/900003）
1. 题目标题显示 `#900003 Big Integer Multiply`。
2. 描述中的 `<script>alert("xss")</script>` 被转义显示，不执行脚本。
3. 样例表格显示输入/输出两列，支持多行文本。
4. 编辑器区域可输入代码，语言下拉可切换 C++17/C11。
5. 点击“运行测试（样例）”后，能跳转到提交结果页。

## 5. 提交结果页（#/submissions/{id}）
建议分别访问以下提交 ID，确认状态样式与明细渲染：

1. `920001`：Accepted，汇总 total/passed/time/memory 正确。
2. `920002`：Wrong Answer，展示 expected vs actual。
3. `920003`：Time Limit Exceeded，状态颜色为 warn。
4. `920004`：Memory Limit Exceeded，内存字段高亮风险。
5. `920005`：Runtime Error，状态为 danger。
6. `920006`：Compile Error，显示编译错误文本块。
7. `920007`：System Error，显示系统错误状态。
8. `920008`：Running，loading 动画可见并轮询。
9. `920009`：Pending，loading 动画可见并轮询。

## 6. 提交历史页（#/history）
1. 表格显示多条记录，状态徽标与时间格式正常。
2. 分页区域显示“第 N 页”，不出现 `offset/limit` 调试字样。
3. 点击提交 ID 链接可跳转到对应结果页。
4. 页面标题用户名仅显示名称，不附带 `(#id)`。

## 7. 边界与异常显示
1. 访问不存在提交（例如 `#/submissions/999999`）应显示错误提示框。
2. 断开后端或数据库异常时，页面应有错误提示，不出现空白页。
3. 提交结果页在错误场景下应停止轮询。
