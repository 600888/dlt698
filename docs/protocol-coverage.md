# 协议实现进度

更新日期：2026-10-04。第三批已加入普通/列表 SET/ACTION、同步适配及串口/串行链路，M3/M4 的时间标签、周期心跳和真实设备验收仍待完成，尚非完整协议库。标准基线：DL/T 698.45—2017。阶段依据见 [实施计划](../plan/implementation-plan.md)。

已建立根目录与 `cpp/` 独立 CMake 入口、安装导出、标准向量、CTest 和三平台 CI 配置。网络模块采用用户提供的 standalone Asio 1.38.2，入口 `third/asio/include/asio.hpp`；Asio 仅为传输模块的私有编译依赖。

## 当前能力与证据

| 项目 | 线格式/实现状态 | 协议行为/限制 | 测试证据 |
| --- | --- | --- | --- |
| ByteView、Result、Reader/Writer | 已实现 | 借用视图不跨异步边界 | core |
| A-XDR 长度 | 已实现，短/长形式 | 拒绝不定长、非最小编码、溢出和超限 | core |
| 单帧长度、控制域、HCS/FCS | 已实现 | 2017 版长度保留位严格检查；功能码支持 1/3 | core、固定 GET 帧 |
| 变长 SA、逻辑地址、CA | 原始线格式已实现 | BCD 地址文本转换、组/通配的业务匹配待实现 | core，1/16 字节地址 |
| 广播地址 | 检查 AA 地址字节 | 广播无需应答的会话行为待实现 | core |
| 扰码 SC | 单帧加/减 33H 已实现 | 与实际分帧状态机组合待实现 | core |
| 流解析 | 已实现 | 半帧、粘帧、噪声、校验失败恢复；合法但不完整帧须由调用方超时 reset | core，每个切分点/噪声洪流 |
| 链路分帧格式与逐帧确认 | 未实现 | 当前 Frame 可保存分帧标志和原始链路用户数据 | 待补 |
| GET Normal/NormalList | 请求/响应已实现 | PIID、逐项 Data/DAR、可选 TimeTag；FollowReport 存在时返回不支持 | 附录 D.3.1/D.3.2 固定向量、tcp |
| SET Normal/NormalList | 请求/响应 codec 与会话已实现 | 原始 DAR、精确 OAD/顺序匹配；无重试或列表回滚 | mutation，附录 D.4.1/D.4.2、tcp |
| ACTION 普通/列表 | 请求/响应 codec 与会话已实现 | DAR/可选 Data、完整 OMD 模式匹配，区分 NULL 与没有数据 | mutation，附录 D.5.1 与规范构造列表、tcp |
| TCP 客户端/监听/双向通道 | 已实现 | DNS/连接、单个在途读、串行全量写、关闭、队列限制 | tcp，含 GET 回环与销毁测试 |
| 连接与异常 APDU | LINK/CONNECT/RELEASE/ERROR 双向 codec 已实现 | LINK 不附加 Client/Server 尾部；认证机制仅表示线格式 | connection，附录 D.1/D.2 及规范构造向量 |
| 异步执行器 | ManualExecutor、IoRuntime::executor 已实现 | 串行投递与单调计时；应用负责驱动，没有隐藏工作线程 | session、tcp，取消/异常/销毁 |
| 内存通道 | MemoryChannel 已实现 | 分块读取，接收缓存及投递前写字节/条数预算；无部分写入 | session，缓存/待写超限、EOF、未驱动销毁 |
| 安装包 | 静态/共享目标已实现 | 导出 core/session/service/transport/聚合目标；不安装 Asio 头文件，MSVC 传递 /utf-8 | installed_consumer |
| 预连接与应用会话 | 基础 Session 已实现 | 公共 CONNECT、版本/能力/尺寸协商、单次 LINK、预设连接、RELEASE/闲置通知；精确单地址、单在途事务 | session、tcp，两种拨号方向 |
| 请求路由与生命周期 | 已实现基础行为 | SA/CA/DIR/PRM/PIID/OAD/列表匹配、序号隔离、超时/取消关闭、重入；周期心跳和 TimeTag 语义待补 | session，错配/重复/迟到/耗尽/时钟异常/析构 |
| ClientService/ServerService/providers | GET/SET/ACTION 异步普通、列表已实现 | 每项 Data/DAR/可选 Data，按顺序独立执行；超时后远端副作用结果未知 | mutation、session、tcp、内存示例 |
| 同步服务 | SyncClientService 已实现 | 显式 drive 或应用后台运行线程；循环回调内调用拒绝，适配器不创建线程 | mutation、tcp，重入/超时/驱动异常/外部线程 |
| 原始串口 | SerialChannel 已实现 | 速率/字格式/流控、串行队列与关闭；真实设备收发未验证 | tcp，参数/打开失败路径及编译/安装 |
| 串行时序适配 | SerialLinkChannel 已实现 | 四 FE、收发 33 位间隔、投递前预算、方向与真实排空 hooks；无 hook 时只做时间估算 | serial_link，虚拟时间/重复排空/关闭/故障；memory_mutation 闭环 |
| RS-485 物理验收 | 未验证 | 手动切换必须有真实排空驱动；USB/流控/适配器需硬件测量 | 待设备 |
| 对象 schema/provider | 通用读写及方法 schema 已实现 | 显式 writable、方法权限/参数/返回类型、一级索引、异常映射 DAR；标准目录及单位换算待补 | mutation、session，部分成功/类型/权限/重入/异常 |
| 安全 | CONNECT 认证 CHOICE codec 已实现 | Session 仅接受 NullSecurity，拒绝其他机制；实际认证/SECURITY 封装未实现 | connection、session，非公共机制拒绝 |
| Python 绑定和分发 | 未开始 | C++ 稳定阶段之后进行 | 待补 |

## Data 标签覆盖

“已测”表示该标签具有固定字节预期、编码及解码验证；日期时间目前保持原始字段，日历范围和通配语义校验将在对象/会话阶段补齐。

| 标签 | 类型 | codec/测试 |
| --- | --- | --- |
| 0 | NULL | 已实现/已测 |
| 1 | array | 已实现/已测 |
| 2 | structure | 已实现/已测 |
| 3 | bool | 已实现/已测，限制 0/1 |
| 4 | bit-string | 已实现/已测，保留位数、检查填充位 |
| 5 | double-long | 已实现/已测 |
| 6 | double-long-unsigned | 已实现/已测 |
| 9 | octet-string | 已实现/已测 |
| 10 | visible-string | 已实现/已测，检查可见 ASCII |
| 12 | UTF8-string | 已实现/已测，拒绝非法 UTF-8 |
| 15 | integer | 已实现/已测 |
| 16 | long | 已实现/已测 |
| 17 | unsigned | 已实现/已测 |
| 18 | long-unsigned | 已实现/已测 |
| 20 | long64 | 已实现/已测 |
| 21 | long64-unsigned | 已实现/已测 |
| 22 | enum | 已实现/已测 |
| 23 | float32 | 已实现/已测，含 NaN 原始位往返 |
| 24 | float64 | 已实现/已测 |
| 25 | date_time | 原始 10 字节已实现/已测 |
| 26 | date | 原始 5 字节已实现/已测 |
| 27 | time | 原始 3 字节已实现/已测 |
| 28 | date_time_s | 原始 7 字节已实现/已测 |
| 80 | OI | 已实现/已测 |
| 81 | OAD | 已实现/已测 |
| 82 | ROAD | 未实现 |
| 83 | OMD | 已实现/已测 |
| 84 | TI | 已实现/已测 |
| 85 | TSA | 原始地址字节已实现/已测 |
| 86 | MAC | 未实现 |
| 87 | RN | 未实现 |
| 88 | Region | 未实现 |
| 89 | Scaler_Unit | 已实现/已测，单位枚举目录待补 |
| 90 | RSD | 未实现，需逐个选择器分支登记 |
| 91 | CSD | 未实现 |
| 92 | MS | 未实现，需逐个集合分支登记 |
| 93 | SID | 未实现 |
| 94 | SID_MAC | 未实现 |
| 95 | COMDCB | 未实现 |
| 96 | RCSD | 未实现 |

保留/未知标签返回 `unsupported_tag`，不会猜测长度后跳过。`max_elements` 为单个 Data 树的节点总数，包含根节点；`max_depth` 从根的 0 开始。完整输入 codec 拒绝尾随字节。

## APDU 分支待办

| 服务 | 已实现 | 尚待实现 |
| --- | --- | --- |
| LINK | 登录/心跳/退出 Request，Response；单次会话交互 | 自动周期心跳、完整心跳重试策略 |
| CONNECT | Request/Response、四种认证 CHOICE 线格式；公共连接协商 | 非公共机制真实认证及安全策略 |
| RELEASE | Request/Response/Notification；会话释放及空闲失效 | TimeTag 语义 |
| GET | Normal、NormalList 请求/响应 | Record、RecordList、Next、MD5 请求/响应 |
| SET | Normal、NormalList 请求/响应及服务 | ThenGetNormalList 请求/响应 |
| ACTION | 普通、列表请求/响应及服务 | ThenGetNormalList 请求/响应 |
| REPORT | 无 | List、RecordList、TransData 通知/确认 |
| PROXY | 无 | GetList、GetRecord、SetList、SetThenGetList、ActionList、ActionThenGetList、TransCommand 请求/响应 |
| SECURITY | 无 | Request/Response 全部明文/密文/验证结果分支及真实后端 |
| FollowReport | 无 | 普通结果/记录结果 |
| ERROR | 客户机/服务器异常响应 codec；保留远端类型原码 | TimeTag 语义 |

下一批继续 M3/M4：补周期心跳、TimeTag 语义及独立 TCP/串口命令行示例，完成真实设备验收。then-get、链路分帧与 GET Next 进入后续组合服务阶段；不提前宣告对应能力。

## 本地验证范围

本批 Windows x64 验证：MSVC 19.39 Release 共享库、严格警告，通过 core、connection、mutation、serial_link、session、tcp、installed_consumer 共 7 项；MinGW GCC 15.1 Release 静态库、关闭 Asio 传输、严格警告，通过相应 6 项。安装消费方独立使用同步 SET、串行适配及原始串口导出符号。memory_mutation 运行输出 SET DAR=0、ACTION DAR=0、GET UInt16=42。

项目自身全部 C/C++ 源码使用根目录 .clang-format 格式化，并运行 --style=file --dry-run --Werror；公开头文件使用中文 Doxygen，关键实现有中文注释。第一批曾验证的 MinGW TCP/共享配置不视为本批结果；本机 MinGW TCP 当前因线程创建错误未通过，本批真实 TCP 证据来自 MSVC。未修改系统环境或第三方源码。

GitHub Actions 已配置 Ubuntu/Windows/macOS 静态/共享及纯核心检查；尚未在远程执行，不能据此宣告 Linux/macOS 已验证。串口目前只完成软件模拟/打开失败测试，真实串口、RS-485 设备、安全后端、sanitizer 和 fuzz 验证均未开展。
