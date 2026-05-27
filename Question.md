# OJ Vibe Coding 面试题

## 网络库组件（25题）

1. 项目的网络库整体架构是怎样的？采用什么设计模式？

答案：基于 C++11 自主开发的事件驱动网络服务器框架，采用主从 Reactor 多线程模型。主 EventLoop（Acceptor）负责监听新连接，从属 EventLoop 线程池通过 RR 轮转处理已建立连接的 IO 事件。核心组件包括：Channel（事件分发）、Poller（epoll 封装）、EventLoop（事件循环）、TimerWheel（定时器）、TcpServer（服务器封装）。

2. Reactor 模型和 Proactor 模型有什么区别？为什么选择 Reactor？

答案：Reactor 是事件驱动、同步非阻塞模式——当 IO 事件就绪时通知应用程序去读写；Proactor 是异步模式——操作系统完成读写后通知应用程序。Reactor 实现更简单、更可控，配合 epoll 在 Linux 上性能优异，适合本项目的 HTTP 请求/响应模式。Proactor 需要操作系统原生异步 IO 支持（如 IOCP），跨平台兼容性差。

3. 项目中是如何实现「one loop per thread」的？

答案：主线程运行一个 EventLoop（_baseloop）负责 accept 新连接；LoopThreadPool 管理多个子线程，每个子线程运行独立的 EventLoop；`NextLoop()` 采用 RR（Round-Robin）算法将新连接分配给从属 EventLoop，实现了连接的负载均衡与 CPU 亲和性。

4. 为什么使用 epoll 的 LT（水平触发）模式而非 ET（边缘触发）模式？

答案：LT 模式编程更简单、更安全——只要缓冲区有数据，epoll_wait 就会持续通知，不容易遗漏事件；ET 模式必须一次将数据全部读完，否则不再通知，容易导致数据漏读或饥饿。LT 配合非阻塞 IO 在足够高的并发下性能差异可忽略，但代码健壮性明显更好。

5. eventfd 在网络库中起什么作用？

答案：eventfd 用于跨线程事件通知。当其他线程需要向 EventLoop 线程投递任务（如发送数据、关闭连接）时，先将任务压入`_tasks`队列，再向 eventfd 写入一个 64 位整数，唤醒 epoll_wait 阻塞，EventLoop 线程随后执行 RunAllTask 处理所有待执行任务。

6. 时间轮（TimerWheel）是如何实现定时任务的？支持哪些操作？

答案：时间轮是一个容量固定（默认 60 格）的环形数组，秒针每秒移动一格。添加任务时根据 `(当前_tick + 延迟秒数) % _capacity` 放入对应槽位，每个槽位存放 `shared_ptr<TimerTask>`。支持：TimerAdd（添加）、TimerRefresh（刷新/延迟）、TimerCancel（取消）。槽位清空时会触发 `TimerTask` 析构，在析构中执行回调，实现到期自动执行。

7. 非活跃连接自动释放是如何实现的？

答案：当连接建立并启用非活跃释放（`EnableInactiveRelease(sec)`）后，会向时间轮注册一个延迟 sec 秒的定时任务。每次连接上有任意事件触发时（HandleEvent 回调），调用 `TimerRefresh(_conn_id)` 刷新定时器——将任务重新放入时间轮对应槽位。如果连接在 sec 秒内无任何事件，定时任务到期执行 `Connection::Release` 释放连接。

8. Channel 类的作用是什么？为什么将其设计为独立的类而非放在 Connection 中？

答案：Channel 封装了一个文件描述符及其关注的事件（_events）和就绪事件（_revents），以及各类事件回调函数。独立设计的原因是：不止 Connection 需要事件管理——Acceptor（监听套接字）、EventLoop（eventfd）、TimerWheel（timerfd）都需要事件监控，Channel 可以被这些模块复用。

9. Poller 类如何实现与具体 IO 多路复用机制的解耦？

答案：Poller 对外提供 `UpdateEvent`、`RemoveEvent`、`Poll` 三个统一接口，内部调用 epoll_ctl/epoll_wait。如果将来要支持 select/poll/kqueue，只需新增一个 Poller 实现类，EventLoop 对 Poller 的使用接口完全不变（策略模式思想）。

10. Buffer 的设计有什么巧妙之处？

答案：Buffer 内部使用 `vector<char>` 管理内存，维护 `_reader_idx` 和 `_writer_idx` 两个偏移量。写入数据时先检查末尾空闲空间，不足则判断整体空闲空间（末尾 + 头部），整体够就整理数据（移到头），不够才扩容。这种设计避免了频繁的内存分配与拷贝，兼顾了空间利用率和性能。

11. Connection 的状态机是如何设计的？

答案：定义了四种状态：`DISCONNECTED`（已断开）、`CONNECTING`（半连接）、`CONNECTED`（已就绪）、`DISCONNECTING`（待关闭）。连接创建时置为 CONNECTING，`EstablishedInLoop` 完成后置为 CONNECTED，`Shutdown` 置为 DISCONNECTING，`ReleaseInLoop` 置为 DISCONNECTED。各 IO 操作前检查状态，确保不向已断开的连接读写。

12. 如何保证 Connection 在事件回调中不被意外销毁？

答案：Connection 继承 `enable_shared_from_this`，在回调中通过 `shared_from_this()` 获取 `shared_ptr`，延长自身生命周期。同时提供了 `SafeSharedFromThis` 包装 `try-catch`，防止在尚未被 `shared_ptr` 管理时调用 `shared_from_this` 抛出 `bad_weak_ptr`。

13. TcpServer 如何管理所有连接的生命周期？

答案：TcpServer 内部维护 `unordered_map<uint64_t, PtrConnection> _conns` 保存所有连接的 `shared_ptr`。每个 Connection 设置 `_server_closed_callback` 绑定到 `TcpServer::RemoveConnection`，当连接释放时自动从 `_conns` 中移除。这种设计确保连接被 TcpServer 持有，不会在业务处理中途被销毁。

14. HTTP 请求解析为什么使用有限状态机？

答案：HTTP 请求的解析存在明确的阶段划分——请求行、头部、正文。状态机将解析过程拆解为 `RECV_HTTP_LINE → RECV_HTTP_HEAD → RECV_HTTP_BODY → RECV_HTTP_OVER` 四个状态，每个状态只处理当前阶段的数据，并触发状态迁移。这种设计使解析逻辑清晰、易于扩展（如支持 chunked 编码只需新增状态）。

15. 静态资源与动态请求是如何分离和处理的？

答案：`HttpServer::Route` 先判断是否为静态资源请求（条件：GET/HEAD 方法、合法路径、文件存在），是则直接读取文件返回；否则进入方法对应的路由表（_get_route/_post_route 等），通过正则匹配 URI 找到对应 Handler 执行。两者都不匹配则返回 404。

16. 长连接和短连接是如何判断和处理的？

答案：通过 `Connection` 头部字段判断——值为 `keep-alive` 则为长连接，否则为短连接（或 HTTP/1.1 默认 keep-alive）。短连接在响应发送完毕后立即调用 `conn->Shutdown()` 关闭；长连接则重置 `HttpContext` 继续等待下个请求。

17. 智能指针在项目中是如何运用的？存在循环引用问题吗？

答案：使用 `shared_ptr` 管理 Connection 对象，`weak_ptr` 在 TimerWheel 的 `_timers` 中保存定时任务弱引用以避免延长任务生命周期。Connection 通过 `enable_shared_from_this` 获取自身 `shared_ptr`，但回调中注意不要形成「TcpServer → Connection → TcpServer」循环引用——Connection 只持有 TcpServer 的弱引用形式的回调，不存在循环引用问题。

18. Any 类型的作用是什么？如何实现类型擦除？

答案：Any 用于在 Connection 中保存任意类型的上下文（如 HttpContext），采用「外敷类 + 内嵌模板子类」实现类型擦除。Holder 基类提供虚接口，`placeholder<T>` 模板子类保存具体类型，通过 `clone()` 实现拷贝。获取时通过 `typeid` 做类型安全检查。

19. 如果某个子线程的 EventLoop 发生阻塞，会影响整个服务吗？

答案：不会。每个 EventLoop 运行在独立线程中，主 EventLoop 阻塞只影响新连接 accept；某个从属 EventLoop 阻塞只影响其管理的连接，其他子线程的连接照常处理。但建议在客户端或监控层设置超时保护，避免单个连接长时间占用 EventLoop 线程。

20. LoopThread 中 EventLoop 的创建为什么需要条件变量同步？

答案：`LoopThread::ThreadEntry` 中创建 EventLoop 后调用 `loop.Start()`（永久循环），外部线程调用 `GetLoop()` 时需要等待 EventLoop 对象创建完毕。使用 `condition_variable` + 互斥锁确保 `_loop` 指针赋值对 `GetLoop` 可见，避免获取到空指针。

21. 如果 epoll_wait 被信号中断（EINTR），项目是如何处理的？

答案：Poller::Poll 中 epoll_wait 返回 -1 且 errno 为 EINTR 时，直接 return 而不调用 abort()。EventLoop 会继续下一轮循环，重新执行 epoll_wait。这是信号安全的标准处理方式。

22. Socket 类是如何封装非阻塞 IO 的？

答案：`CreateServer` 中调用 `NonBlock()`（设置 O_NONBLOCK），`Recv`/`Send` 默认是阻塞版本，同时提供 `NonBlockRecv`/`NonBlockSend` 指定 `MSG_DONTWAIT` 标志。非阻塞 IO 配合 epoll LT 模式，确保不会在单个连接上阻塞，从而保证 EventLoop 的吞吐能力。

23. HTTP 解析中的 URL 编解码是如何处理的？

答案：`Util::UrlEncode` 将特殊字符转成 `%HH` 格式（字母、数字、`.`、`-`、`_`、`~` 不编码），空格可选择编码为 `+`；`UrlDecode` 反向解码，将 `+` 可转为空格。资源路径解码时不转空格，查询字符串解码时转空格，符合 W3C 标准。

24. 如果客户端发送超大 HTTP 头部或 URI，系统如何防护？

答案：`MAX_LINE = 8192` 限制单行最大长度；如果缓冲区中已有超过 8192 字节仍未读到完整行，`RecvHttpLine`/`RecvHttpHead` 直接将状态置为 `RECV_HTTP_ERROR`，响应 `414 URI Too Long`，并关闭连接防止攻击。

25. 如果要支持 WebSocket 升级，需要怎么扩展当前框架？

答案：在 HttpServer 中检测 `Upgrade: websocket` 头部，调用 Connection::Upgrade 切换上下文：重置 `Any` 为 WebSocket 上下文，替换 `_connected_callback`/`_message_callback`/`_closed_callback` 为 WebSocket 处理函数。Upgrade 必须在 EventLoop 线程内立即执行（AssertInLoop），防止切换前新事件到达使用旧协议处理。

---

## OJ 业务相关（20题）

26. 项目整体架构是怎样的？各模块分别负责什么？

答案：项目采用 C++ 后端，自主开发的高性能网络库（src/net/）提供 HTTP 服务；Handler 负责业务处理，JudgeManager 负责判题调度；判题通过独立的 `judger_cli` 子进程执行，并放在 `nsjail` 沙箱内；MySQL 负责持久化用户、题目与提交数据；`web/` 提供静态前端页面。

27. 提交代码后，判题的完整链路是什么？

答案：用户提交代码 -> 后端接收请求并保存提交记录 -> JudgeManager 将任务加入队列 -> fork/exec 启动 `judger_cli` -> `judger_cli` 在沙箱中编译/运行 -> 输出 JSON 结果 -> 主服务解析结果并更新数据库 -> 前端展示判题状态。

28. 为什么要把判题逻辑放在独立进程里，而不是直接写在主服务中？

答案：独立进程可以隔离崩溃、内存越界和文件描述符等风险，避免用户代码影响主服务；同时便于更严格地施加资源限制和沙箱约束，安全边界更清晰。

29. `nsjail` 在这个项目中的核心作用是什么？

答案：`nsjail` 用来做命名空间隔离、权限收敛和文件系统隔离，配合 cgroups、`setrlimit`、seccomp 等机制限制 CPU、内存、网络、文件访问和系统调用，减少沙箱逃逸风险。

30. 如何设计判题并发控制，避免系统在高峰期被打满？

答案：通常由 JudgeManager 维护一个固定并发上限和任务队列，超出部分排队；可按用户、题目或优先级做限流，也可以给出背压机制，避免判题任务把 CPU、内存和磁盘 IO 吃满。

31. 时间限制和内存限制是如何实现的？

答案：时间限制一般结合 CPU 时间的 `setrlimit`、外部超时监控和进程回收；内存限制通常依赖 cgroups 或 `setrlimit` 的地址空间限制。两者要结合起来看，因为只靠单一机制往往不够可靠。

32. 如何判断一个提交是 TLE、MLE、RE 还是 AC？

答案：通过子进程退出状态、信号类型、运行时耗时和内存峰值来判定。超时一般判 TLE，超过内存限制判 MLE，异常退出或被信号杀死通常判 RE，所有样例和测试点通过则为 AC。

33. `judger_cli` 和主服务之间如何通信？

答案：常见方式是主服务启动 `judger_cli` 后，通过标准输入、临时文件或命令行参数传递任务信息，`judger_cli` 再通过标准输出返回结构化 JSON，主服务负责解析、落库和返回结果。

34. MySQL 中哪些表是这个系统最核心的？应该怎么设计索引？

答案：核心表通常包括 `users`、`problems`、`submissions`、`testcases`、`languages`。`submissions` 表通常按 `user_id`、`problem_id`、`created_at` 建索引，便于查历史提交、题目提交列表和按时间分页。

35. 如何避免 SQL 注入？

答案：不要直接拼接用户输入，必须使用预编译语句或参数化查询；同时对输入做长度和格式校验，并限制数据库账号权限，减少注入后的破坏面。

36. JWT 鉴权在这个项目里有什么优点和风险？

答案：优点是无状态、易扩展、服务端压力小；风险是 token 泄露后可被直接使用，因此要配合短过期时间、刷新机制、HTTPS 传输和安全的密钥管理。

37. C++ 后端里最需要警惕的内存和并发问题有哪些？

答案：包括空指针、悬空指针、重复释放、数据竞争、死锁和未定义行为。应尽量使用 RAII、智能指针、明确的锁粒度，并通过 ASAN、TSAN 等工具辅助发现问题。

38. 这个项目适合怎么做单元测试和集成测试？

答案：业务逻辑层适合用 GTest 做单元测试，例如 JudgeManager 的调度逻辑、参数解析和数据库封装；集成测试则可以启动真实服务与 `judger_cli` 跑端到端流程，覆盖 AC、WA、TLE、MLE、RE、CE 等场景。

39. 如果编译失败了，系统应该怎样把错误信息展示给用户？

答案：应截获编译器 stderr，做长度限制和 HTML/JSON 转义后返回给前端；同时避免泄露服务器绝对路径、内部配置和敏感信息，只展示足够定位问题的摘要。

40. 题目测试用例上传时需要注意什么安全问题？

答案：要限制文件类型和大小，校验内容格式，防止上传可执行文件、设备文件或恶意压缩包；上传后最好在隔离环境中进行校验，并为测试用例做版本和哈希管理。

41. 这个系统在部署时有哪些权限和目录方面的注意事项？

答案：判题工作目录、测试数据目录和临时目录都应归非特权用户所有；服务最好以专用用户运行；`nsjail` 需要对应的内核支持和权限配置；配置文件中的密钥和数据库账号也要限制访问权限。

42. 如何监控这个判题系统是否健康？

答案：可以观察判题队列长度、并发运行数、平均判题时长、超时率、失败率、CPU/内存/磁盘使用率和数据库连接数；同时记录请求 ID、提交 ID 和错误日志，方便追踪问题。

43. 如果用户提交了一个死循环程序，系统如何保证不会拖垮服务？

答案：通过 CPU 时间限制、wall-clock 超时监控、进程回收和任务队列并发上限控制，让单个任务最多占用受限资源，超时后立即终止并标记为 TLE。

44. 如何保证判题环境可复现？

答案：固定编译器和运行时版本，保存 nsjail 配置和资源限制参数，记录编译命令与运行参数；对于随机算法题，可以固定随机种子，方便复现和排查。

45. 如果以后要横向扩展判题能力，你会怎么设计？

答案：把提交任务抽象成消息放入队列，让多个判题 worker 独立消费；测试数据和临时文件放共享存储；主服务只负责接收请求、写库和查询结果，这样就能通过增加 worker 数量横向扩展吞吐量。
