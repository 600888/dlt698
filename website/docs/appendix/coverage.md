---
title: 协议覆盖范围
description: 已实现能力、限制、待办事项和验证状态。
---

# 协议覆盖范围

标准基线：**DL/T 698.45—2017**。当前版本是 0.x 阶段，**尚未覆盖完整协议**。

本页列出每个能力的状态和证据来源。跨设备联调前请先确认对应条目。

## 状态图例

| 标记 | 含义 |
| --- | --- |
| 已实现 | 线格式与协议行为都已实现并有测试证据 |
| 部分实现 | 主路径可用，但有明确限制 |
| 未实现 | 接口存在但会返回 `unsupported_service` 或未接线 |
| 未验证 | 代码完成但缺硬件或环境证据 |

## 基础能力

| 项目 | 状态 | 协议行为与限制 | 测试证据 |
| --- | --- | --- | --- |
| `ByteView`、`Result`、`Reader`/`Writer` | 已实现 | 借用视图不跨异步边界 | core |
| A-XDR 长度 | 已实现 | 拒绝不定长、非最小编码、溢出和超限 | core |
| 单帧长度、控制域、HCS/FCS | 已实现 | 2017 版长度保留位严格检查；功能码支持 1/3 | core、固定 GET 帧 |
| 变长 SA、逻辑地址、CA | 部分实现 | 原始线格式已实现；BCD 地址文本转换、组/通配的业务匹配待实现 | core，1/16 字节地址 |
| 广播地址 | 部分实现 | 检查 AA 地址字节；广播无需应答的会话行为待实现 | core |
| 扰码 SC | 部分实现 | 单帧加/减 33H 已实现；与实际分帧状态机组合待实现 | core |
| 流解析 | 已实现 | 半帧、粘帧、噪声、校验失败恢复；合法但不完整帧须由调用方超时 reset | core，每个切分点/噪声洪流 |
| 链路分帧格式与逐帧确认 | 已实现 | `LinkFragmenter`/`LinkReassembler` 提供；会话侧超时与重发由 `fragment_timeout`/`fragment_retries` 控制 | session、serial_link |

## 服务层

| 服务 | 状态 | 协议行为与限制 | 测试证据 |
| --- | --- | --- | --- |
| GET Normal/NormalList | 已实现 | PIID、逐项 Data/DAR、可选 TimeTag；FollowReport 存在时返回 `unsupported_service` | 附录 D.3.1/D.3.2 固定向量、tcp |
| GET Record/RecordList | 部分实现 | 请求响应与分块已实现；选择器业务语义由 provider 解释 | mutation、session、tcp |
| SET Normal/NormalList | 已实现 | 原始 DAR、精确 OAD/顺序匹配；无重试或列表回滚 | mutation，附录 D.4.1/D.4.2、tcp |
| ACTION 普通/列表 | 已实现 | DAR/可选 Data、完整 OMD 模式匹配，区分 NULL 与没有数据 | mutation、附录 D.5.1、tcp |
| LINK | 部分实现 | 登录/心跳/退出 Request 与 Response 已实现；自动周期心跳和完整重试策略待完善 | connection、session |
| CONNECT | 部分实现 | 四种认证 CHOICE 线格式和公共连接协商已实现；**非公共机制真实认证未实现**，`Session` 只接受 NullSecurity | connection、session |
| RELEASE | 已实现 | 请求/响应/通知；会话释放及空闲失效 | connection、session |
| ERROR | 已实现 | 双向异常响应 codec；保留远端类型原码 | connection |
| REPORT | 未实现 | List、RecordList、TransData 通知/确认 | 待补 |
| PROXY | 未实现 | GetList、GetRecord、SetList、SetThenGetList、ActionList、ActionThenGetList、TransCommand | 待补 |
| SECURITY | 未实现 | Request/Response 全部明文/密文/验证结果分支及真实后端 | 待补 |
| FollowReport | 未实现 | 普通结果/记录结果；带该字段的响应返回 `unsupported_service` | 待补 |
| then-get | 未实现 | SET/ACTION 的 ThenGetNormalList 请求/响应 | 待补 |

## 会话与对象

| 项目 | 状态 | 协议行为与限制 | 测试证据 |
| --- | --- | --- | --- |
| 预连接与应用会话 | 已实现 | 公共 CONNECT、版本/能力/尺寸协商、单次 LINK、预设连接、RELEASE/闲置通知；精确单地址、单在途事务 | session、tcp，两种拨号方向 |
| 请求路由与生命周期 | 已实现 | SA/CA/DIR/PRM/PIID/OAD/列表匹配、序号隔离、超时/取消关闭、重入 | session，错配/重复/迟到/耗尽/时钟异常/析构 |
| 异步执行器 | 已实现 | `ManualExecutor`、`IoRuntime::executor` 串行投递与单调计时；**应用负责驱动，没有隐藏工作线程** | session、tcp |
| 对象 schema/provider | 已实现 | 显式 writable、方法权限/参数/返回类型、一级索引、异常映射 DAR；**标准对象目录及单位换算待补** | mutation、session |
| `ClientService`/`ServerService` | 已实现 | GET/SET/ACTION 异步普通与列表；每项 Data/DAR/可选 Data，按顺序独立执行 | mutation、session、tcp |
| 同步服务 | 已实现 | 显式 drive 或应用后台线程；循环回调内调用返回 `busy` | mutation、tcp |
| 周期心跳与 TimeTag 语义 | 部分实现 | `heartbeat_seconds` 可配置自动周期；TimeTag 可按原字段编解码，但**延时与有效期语义未验证** | session |

## 传输

| 项目 | 状态 | 协议行为与限制 | 测试证据 |
| --- | --- | --- | --- |
| TCP 客户端/监听/双向通道 | 已实现 | DNS/连接、单个在途读、串行全量写、关闭、队列限制；不自动重试或设置超时 | tcp，含 GET 回环与销毁测试 |
| 内存通道 | 已实现 | 分块读取，接收缓存及投递前写字节/条数预算；无部分写入 | session，缓存/待写超限、EOF、未驱动销毁 |
| 原始串口 | 部分实现 | 速率/字格式/流控、串行队列与关闭；**真实设备收发未验证** | tcp，参数/打开失败路径 |
| 串行时序适配 | 已实现 | 四 FE、收发 33 位间隔、投递前预算、方向与真实排空 hooks | serial_link、memory_mutation 闭环 |
| RS-485 物理验收 | **未验证** | 手动切换必须有真实排空驱动；USB/流控/适配器需硬件测量 | 待设备 |
| 安装包 | 已实现 | 导出 core/session/service/transport/聚合目标；不安装 Asio 头文件，MSVC 传递 `/utf-8` | installed_consumer |

## Data 标签覆盖

「已测」表示该标签具有固定字节预期、编码及解码验证。

| 标签 | 类型 | 状态 |
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

保留或未知标签返回 `unsupported_tag`，**不会**猜测长度后跳过。

`max_elements` 是单个 Data 树的**节点总数**（含根节点），`max_depth` 从根的 0 开始。完整输入 codec 拒绝尾随字节。

## 待办

按优先级排列：

1. **真实设备验收。** 串口收发、RS-485 硬件时序、安全后端。
2. **标准对象目录扩充。** 已内置 [14 个常用 OI](../protocol/standard-points.md) 的属性定义、类型/布局校验、精确倍率和只读绑定；需量、状态、谐波、冻结/事件目录及业务仍待扩充。
3. **周期心跳与 TimeTag 语义。** 延时、有效期和时钟可信的完整行为。
4. **记录选择器。** RSD/CSD/MS/RCSD 各分支的登记与语义。
5. **REPORT、PROXY、SECURITY。** 三个服务族尚未开始。
6. **then-get 与 GET Next 独立暴露。** 目前由 `Session` 自动完成分块。
7. **Python 绑定和分发。** 在 C++ 稳定阶段之后进行。

## 本地验证范围

最近一批 Windows x64 验证：

| 配置 | 结果 |
| --- | --- |
| MSVC 19.39 Release 共享库、严格警告 | 通过 core、connection、mutation、serial_link、session、tcp、installed_consumer 共 7 项 |
| MinGW GCC 15.1 Release 静态库、关闭 Asio 传输、严格警告 | 通过相应 6 项 |

安装消费方独立验证了同步 SET、串行适配及原始串口导出符号。`memory_mutation` 运行输出 `SET DAR=0 ACTION DAR=0 GET UInt16=42`。

GitHub Actions 已配置 Ubuntu/Windows/macOS 静态/共享及纯核心检查，但**尚未在远程执行**，不能据此宣告 Linux/macOS 已验证。串口目前只完成软件模拟和打开失败测试；真实串口、RS-485 设备、安全后端、sanitizer 和 fuzz 验证均未开展。

## 稳定性承诺

0.x 阶段**允许接口调整**。公开头文件使用中文 Doxygen 注释，本文档与其保持一致。涉及行为的变更会同步更新源码注释与本文档。
