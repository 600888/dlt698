# 协议实现进度

更新日期：2026-10-04。当前为第一批开发实现，尚非完整协议库。标准基线：DL/T 698.45—2017。阶段依据见 [实施计划](../plan/implementation-plan.md)。

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
| TCP 客户端/监听/双向通道 | 已实现 | DNS/连接、单个在途读、串行全量写、关闭、队列限制 | tcp，含 GET 回环与销毁测试 |
| 异步执行器 | IoRuntime 已实现 | 应用负责 run/run_for；没有隐藏工作线程 | tcp |
| 安装包 | 静态/共享目标已实现 | 导出 core/transport/聚合目标；不安装 Asio 头文件 | installed_consumer |
| 预连接与应用会话 | 未实现 | LINK/CONNECT/RELEASE、协商、心跳、请求匹配/超时均待补 | 待补 |
| ClientService/ServerService/providers | 未实现 | 当前 TCP 测试使用测试端的 GET 应答逻辑 | 待补 |
| 串口与 RS-485 时序 | 未实现 | 不宣告 RTU 通信已支持 | 待补 |
| 对象目录及单位换算 | 未实现 | 目前仅保留原始 OAD/OMD 和精确 Data | 待补 |
| 安全 | 未实现 | 没有 mock 认证成功或实际密码后端 | 待补 |
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
| LINK | 无 | Request：登录/心跳/退出；Response |
| CONNECT | 无 | Request/Response，各认证 CHOICE、版本/能力/尺寸协商 |
| RELEASE | 无 | Request/Response/Notification |
| GET | Normal、NormalList 请求/响应 | Record、RecordList、Next、MD5 请求/响应 |
| SET | 无 | Normal、NormalList、ThenGetNormalList 请求/响应 |
| ACTION | 无 | Normal、NormalList、ThenGetNormalList 请求/响应 |
| REPORT | 无 | List、RecordList、TransData 通知/确认 |
| PROXY | 无 | GetList、GetRecord、SetList、SetThenGetList、ActionList、ActionThenGetList、TransCommand 请求/响应 |
| SECURITY | 无 | Request/Response 全部明文/密文/验证结果分支及真实后端 |
| FollowReport | 无 | 普通结果/记录结果 |
| ERROR | 无 | 客户机与服务器异常响应 |

下一批优先实现 LINK/CONNECT/RELEASE、基础 Session（PIID 匹配、超时和关闭）、对象 provider 与 ClientService/ServerService；再加入 SET/ACTION 和串口。链路分帧与 GET Next 分别进入 M5。

## 本地验证范围

Windows x64 已验证配置：MinGW GCC 15.1 Debug 静态库、Release 共享库、关闭传输的 Release 纯核心；MSVC 19.39 Release 共享库。每个适用配置执行 core、tcp、installed_consumer。MSVC 使用本机 SDK/Ninja 环境解决安装路径差异，没有修改系统环境或第三方源码。

GitHub Actions 已配置 Ubuntu/Windows/macOS 静态/共享及纯核心检查；尚未在远程执行，不能据此宣告 Linux/macOS 已验证。真实设备、串口、安全后端、sanitizer 和 fuzz 验证均未开展。
