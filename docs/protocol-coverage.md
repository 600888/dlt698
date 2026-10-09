# 协议实现进度

更新日期：2026-10-07。M4/M5 及 M6 高级服务软件实现已完成，含 REPORT、ThenGet、PROXY、MD5、FollowReport/ACD 与安全后端状态机；真实串口/RS-485 和独立设备互操作仍未验收，尚非完整协议库。标准基线：DL/T 698.45—2017。阶段依据见 [实施计划](../plan/implementation-plan.md)。

已建立根目录与 `cpp/` 独立 CMake 入口、安装导出、标准向量、CTest 和三平台 CI 配置。网络模块采用用户提供的 standalone Asio 1.38.2，入口 `third/asio/include/asio.hpp`；Asio 仅为传输模块的私有编译依赖。

## 当前能力与证据

| 项目 | 线格式/实现状态 | 协议行为/限制 | 测试证据 |
| --- | --- | --- | --- |
| ByteView、Result、Reader/Writer | 已实现 | 借用视图不跨异步边界 | core |
| A-XDR 长度 | 已实现，短/长形式 | 拒绝不定长、非最小编码、溢出和超限 | core |
| 单帧长度、控制域、HCS/FCS | 已实现 | 2017 版长度保留位严格检查；功能码支持 1/3 | core、固定 GET 帧 |
| 变长 SA、逻辑地址、CA | 原始线格式已实现 | BCD 地址文本转换、组/通配的业务匹配待实现 | core，1/16 字节地址 |
| 广播地址 | 检查 AA 地址字节 | 广播无需应答的会话行为待实现 | core |
| 扰码 SC | 单帧和分帧头/片段加减 33H 已实现 | 校验在线上扰码字节上计算 | core、m4m5 固定分帧字段组合 |
| 流解析 | 已实现 | 半帧、粘帧、噪声、校验失败恢复；合法但不完整帧须由调用方超时 reset | core，每个切分点/噪声洪流 |
| 链路分帧格式与逐帧确认 | LinkFragmenter/Reassembler、Session 已实现 | 起始/中间逐片确认、末片交付；12 位回绕、有界重组/发送、有限重发与关闭回收 | m4m5，独立字段、回绕、丢失 ACK、重复/乱序/超限/超时 |
| GET Record/RecordList/Next | 双向 codec、会话、同步/异步服务已实现 | 完整单元分块、单份快照、PIID/序号/表头匹配；应用块可继续链路分帧 | m4m5，D.3.3 向量、组合分段、DAR10/11、缓存回收、installed_consumer |
| GET Normal/NormalList | 请求/响应已实现 | PIID、逐项 Data/DAR、可选 TimeTag；两类 FollowReport 尾部及观察回调 | 附录 D.3.1/D.3.2 固定向量、tcp |
| SET Normal/NormalList | 请求/响应 codec 与会话已实现 | 原始 DAR、精确 OAD/顺序匹配；无重试或列表回滚 | mutation，附录 D.4.1/D.4.2、tcp |
| ACTION 普通/列表 | 请求/响应 codec 与会话已实现 | DAR/可选 Data、完整 OMD 模式匹配，区分 NULL 与没有数据 | mutation，附录 D.5.1 与规范构造列表、tcp |
| TCP 客户端/监听/双向通道 | 已实现 | DNS/连接、单个在途读、串行全量写、关闭、队列限制 | tcp，含 GET 回环与销毁测试 |
| 连接与异常 APDU | LINK/CONNECT/RELEASE/ERROR 双向 codec 已实现 | LINK 不附加 Client/Server 尾部；认证机制仅表示线格式 | connection，附录 D.1/D.2 及规范构造向量 |
| 异步执行器 | ManualExecutor、IoRuntime::executor 已实现 | 串行投递与单调计时；应用负责驱动，没有隐藏工作线程 | session、tcp，取消/异常/销毁 |
| 内存通道 | MemoryChannel 已实现 | 分块读取，接收缓存及投递前写字节/条数预算；无部分写入 | session，缓存/待写超限、EOF、未驱动销毁 |
| 安装包 | 静态/共享目标已实现 | 导出单一 dlt698 库及旧组件兼容别名；不安装 Asio 头文件，MSVC 传递 /utf-8 | installed_consumer |
| 预连接与应用会话 | 基础 Session 已实现 | 公共 CONNECT、版本/能力/尺寸协商、单次/自动周期 LINK、预设连接、RELEASE/闲置通知；精确单地址、单在途事务 | session、tcp，两种拨号方向 |
| 请求路由与生命周期 | 已实现基础行为 | SA/CA/DIR/PRM/PIID/OAD/列表匹配、序号隔离、超时/取消关闭、重入；自动周期心跳、日历 TimeTag 判断/回传及响应匹配已实现 | session，错配/重复/迟到/耗尽/时钟异常/析构 |
| ClientService/ServerService/providers | GET/SET/ACTION 普通、列表及记录异步服务已实现 | 每项 Data/DAR/可选 Data，按顺序独立执行；超时后远端副作用结果未知 | mutation、session、tcp、内存示例 |
| 同步服务 | SyncClientService 及记录读取已实现 | 显式 drive 或应用后台运行线程；循环回调内调用拒绝，适配器不创建线程 | mutation、tcp，重入/超时/驱动异常/外部线程 |
| 原始串口 | SerialChannel 已实现 | 速率/字格式/流控、串行队列与关闭；真实设备收发未验证 | tcp，参数/打开失败路径及编译/安装 |
| 串行时序适配 | SerialLinkChannel 已实现 | 四 FE、收发 33 位间隔、投递前预算、方向与真实排空 hooks；无 hook 时只做时间估算 | serial_link，虚拟时间/重复排空/关闭/故障；memory_mutation 闭环 |
| RS-485 物理验收 | 未验证 | 手动切换必须有真实排空驱动；USB/流控/适配器需硬件测量 | 待设备 |
| 对象 schema/provider | 通用读写及方法 schema 已实现 | 显式 writable、方法权限/参数/返回类型、一级索引、异常映射 DAR | mutation、session，部分成功/类型/权限/重入/异常 |
| 常用标准点位 | 122 个普通 OI（原 118 个常用点位与 F100/F101/F200/F201 安全/端口对象）的元数据、OAD、精确倍率与只读绑定已实现 | 接线/费率/最高谐波次数显式配置；字段顺序、数组/位串/字符串长度、资源校验；需量时间保留；不自动提供数据或推断远端支持 | standard_points、installed_consumer、dlt698_standard_points、dlt698_standard_points_extended；详细范围见 [固定点位](../website/docs/protocol/standard-points.md) |
| 安全 | CONNECT / SECURITY codec、IBackend 注入及认证状态机已实现 | 真正 MAC/签名/加解密由 ESAM 厂商后端执行；SDK、凭据及硬件验收待提供；默认拒绝明文降级 | advanced，状态机测试替身；非真实密码/硬件证据 |
| 标准记录与能力筛选 | 4 个记录入口、5 个记录列，完整目录共 131 个 OI；模板及 MemoryRecords、读取计划和显式点位探测已实现 | 日/月冻结、掉电/初始化事件投影；RSD 0/1/2/9、平面 OAD、预算及原子快照；按行 GET Next；全零功能位按未知，不推断 OAD 存在 | standard_records_test、installed_consumer、dlt698_standard_records；范围见[标准记录](../website/docs/protocol/standard-records.md) |
| 传输层 CLI | tcp_server/client、rtu_server/client、master/terminal 已实现 | 服务端与客户端各一个独立程序，共用模拟对象目录；TCP 拨号方向独立；串口无连接概念，服务端直接应答 | 独立进程 TCP：tcp_server 接受 4 个客户端 × get/set/action/record；master/terminal 双向拨号共 8 次 |
| Python 绑定和分发 | 未开始 | C++ 稳定阶段之后进行 | 待补 |
| 托管两端与设备数据 | app::Server、app::Client、service::Device、app 安装目标及配对示例已实现 | TCP 多连接、自动 LINK/CONNECT、同步读写/方法/记录、阶段超时及取消；数据跨连接/停止保留；串口自动适配；高层服务器值只读；provider/外部运行时/重拨待后续 | device、app、installed_consumer；真实串口和独立设备互操作未验证；见[服务器](../website/docs/session/server.md)和[客户端](../website/docs/session/client.md) |

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
| 82 | ROAD | 已实现/已测（RecordData 不可变有类型节点） |
| 83 | OMD | 已实现/已测 |
| 84 | TI | 已实现/已测 |
| 85 | TSA | 原始地址字节已实现/已测 |
| 86 | MAC | 已实现/已测；密码语义由后端负责 |
| 87 | RN | 已实现/已测；密码语义由后端负责 |
| 88 | Region | 已实现/已测（RecordData 不可变有类型节点） |
| 89 | Scaler_Unit | 已实现/已测；standard::unit_symbol 收录本批普通点位使用的单位，完整枚举目录待补 |
| 90 | RSD | 已实现/已测（RecordData 不可变有类型节点） |
| 91 | CSD | 已实现/已测（RecordData 不可变有类型节点） |
| 92 | MS | 已实现/已测（RecordData 不可变有类型节点） |
| 93 | SID | 已实现/已测；密码语义由后端负责 |
| 94 | SID_MAC | 已实现/已测；密码语义由后端负责 |
| 95 | COMDCB | 已实现/已测；COMDCB 检查枚举范围 |
| 96 | RCSD | 已实现/已测（RecordData 不可变有类型节点） |

保留/未知标签返回 `unsupported_tag`，不会猜测长度后跳过。`max_elements` 为单个 Data 树的节点总数，包含根节点；`max_depth` 从根的 0 开始。完整输入 codec 拒绝尾随字节。

## 记录选择器分支

以下每行均有独立值模型、双向 codec、分支/截断检查与 RecordData 标签测试（m4m5）。选择条件作为拥有型值传入 provider，具体业务筛选由应用解释。

| 类型/分支 | 内容 | 状态 |
| --- | --- | --- |
| RSD 0 | 不选择 | 已实现/已测 |
| RSD 1 | OAD 与指定 Data 值 | 已实现/已测，D.3.3(1) |
| RSD 2 | OAD、起止 Data、间隔 Data | 已实现/已测 |
| RSD 3 | Selector2 组合 | 已实现/已测 |
| RSD 4 | 采集启动时间、MS | 已实现/已测 |
| RSD 5 | 采集存储时间、MS | 已实现/已测，D.3.3(2) |
| RSD 6 | 启动时间区间、TI、MS | 已实现/已测 |
| RSD 7 | 存储时间区间、TI、MS | 已实现/已测 |
| RSD 8 | 成功时间区间、TI、MS | 已实现/已测 |
| RSD 9 | 上第 n 次记录 | 已实现/已测，独立字段向量 |
| RSD 10 | 最近 n 条、MS | 已实现/已测 |
| MS 0 | 无表计 | 已实现/已测 |
| MS 1 | 全部表计 | 已实现/已测 |
| MS 2 | 用户类型集合 | 已实现/已测，独立 Data 向量 |
| MS 3 | TSA 集合 | 已实现/已测，D.3.3(2) |
| MS 4 | 配置序号集合 | 已实现/已测 |
| MS 5 | UInt8 类型区间 | 已实现/已测，类型/边界拒绝 |
| MS 6 | TSA 地址区间 | 已实现/已测 |
| MS 7 | UInt16 配置序号区间 | 已实现/已测 |
| CSD 0/1、RCSD | OAD/ROAD 列及列集合 | 已实现/已测，D.3.3、独立字段向量 |
| Region 0–3 | 四种开闭边界、精确 Data 端点 | 已实现/已测，全树深度/节点预算 |

## APDU 分支与剩余验收

| 服务 | 已实现 | 剩余工作 |
| --- | --- | --- |
| LINK | 登录/心跳/退出及自动周期心跳 | 应用重拨/重登录、现场验收 |
| CONNECT | 四种机制 codec、公共协商、可注入认证后端及失败关闭 | 厂商 ESAM SDK 与真实策略联调 |
| RELEASE | 请求/响应/通知，明文与安全会话释放清理 | 真实设备验证 |
| GET | Normal/List、Record/List、Next、MD5 | 型号对象与数据库语义 |
| SET / ACTION | 普通/列表及 ThenGetNormalList | 目标 provider 与业务授权 |
| REPORT | List、RecordList、TransData 通知/确认、重发与去重 | 现场上报配置/持久化及设备验收 |
| PROXY | 七类双向 codec、目标路由、超时/取消、透明 provider | 目标设备与真实端口驱动联调 |
| SECURITY | 明文/密文/验证 DAR、全部验证分支、后端保护/解封 | 真实 ESAM MAC/签名/加解密、密钥生命周期和硬件验收 |
| FollowReport / ACD | 普通/记录结果、分块尾部、观察器及服务器 ACD 设置 | 应用事件 OAD/配置与处理策略 |
| ERROR | 双向 codec、远端原码与 TimeTag | 无新增软件缺项 |

高级服务使用分层 Session API，详见 [接入说明](advanced-services.md)。安全完成范围及缺少材料见 [ESAM 接入](esam-integration.md)。尚未提供具体电表/集中器型号及 OI/方法清单，不能宣称覆盖目标设备全部业务；记录选择器的采集/数据库语义仍由 provider 解释。

## 本次 M6 软件验证

MSVC Release 静态全量 11 项 CTest（包括真实本机 TCP 回环）、MSVC 共享纯核心 8 项、MinGW GCC 纯核心 8 项均通过；库目标通过 MSVC `/WX` 和 GCC `-Werror`，全部项目 C/C++ 源码通过格式检查；新增固定报文覆盖标准附录 D 的 ThenGet/PROXY，其他分支覆盖截断、尾随字节、资源上限、超时、取消、REPORT 重发、FollowReport/ACD 和安全验证失败。安全测试使用明确标注的测试替身，不能证明真实 ESAM 密码实现或独立设备互操作。安装消费方使用新增 MD5、SECURITY 和高级服务导出 API。

下面保留之前批次的验证范围；它们不替代本次新增功能的跨平台/硬件验收。

## 本地验证范围

本批 Windows x64 验证：MSVC 19.39 Release 共享/静态及 MinGW GCC 15.1 Release 静态、开启 Asio 三组完整构建与 11 项 CTest 均通过：installed_consumer、common、codec、protocol_link、protocol_apdu、service、device、session、transport、transport_tcp、app。MinGW 静态、关闭传输的 8 项也通过。新库和 device/app 测试通过 `/WX` / `-Werror`；完整现有测试采用普通告警配置，已有窄化转换、变量遮蔽和未使用函数告警另行保留。安装消费方验证只链接 `dlt698::app` 以及关闭传输时的 Device；原描述符、分帧和同步服务导出仍有回归。

项目自身全部 C/C++ 源码通过根目录 .clang-format 的 --style=file --dry-run --Werror 检查；公开头文件使用中文 Doxygen，关键实现有中文注释。MinGW 静态网络崩溃已定位为 Asio 有线程/无线程类型因包含顺序而混用，通过 transport 的私有编译定义统一线程配置，transport_tcp 已恢复默认执行。未修改第三方源码；真实 TCP 回环证据来自 MSVC 与 MinGW。

GitHub Actions 已配置 Ubuntu/Windows/macOS 静态/共享及纯核心检查；尚未在远程执行，不能据此宣告 Linux/macOS 已验证。串口目前只完成软件模拟/打开失败测试，真实串口、RS-485 设备、真实 ESAM 安全后端、sanitizer 和 fuzz 验证均未开展。

托管客户端更新重新验证上述构建组合，app 新增 9 项客户端用例，覆盖实际读写/方法、400 行记录分块、阶段超时、取消、原码和回调销毁；独立进程 `dlt698_server` / `dlt698_client` 配对读到频率 5000 并正常退出。详见[本批验收](../plan/application-api-plan.md)。
