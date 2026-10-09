---
title: 示例程序
description: 仓库示例的分类、用途、构建与运行方式。
---

# 示例程序

示例源码位于 `cpp/examples/`，按用途分为四个目录。普通服务器和客户端优先使用 `app/`，其余目录用于分层接入与高级定制：

| 目录 | 层次 | 依赖 | 是否需要设备 |
| --- | --- | --- | --- |
| `app/` | 托管服务器/客户端，直接设置、连接和读取 | `dlt698::dlt698` | TCP 不需要，串口需要 |
| `codec/` | 链路帧与 APDU 编解码 | `dlt698::dlt698` | 否 |
| `service/` | 对象目录、读写方法、记录查询 | `dlt698::dlt698` | 否 |
| `transport/` | 真实 TCP 与串口链路 | `dlt698::dlt698` | TCP 不需要，串口需要 |

构建后输出到 `bin/`（Visual Studio 多配置构建为 `bin/Release/`）。
`app/` 和 `transport/` 示例的电表地址均在源码中配置，默认 `000000000000`（六个 `0x00` 字节），
逻辑地址及客户端地址 CA 均为 0，两端必须一致。地址按线序填写，低有效字节在前，
例如 `123456789012` 对应 `{0x12, 0x90, 0x78, 0x56, 0x34, 0x12}`。
托管服务端读取属性 `4001/2/0` 时返回同一配置的地址字节。
构建示例需要 `-DDLT698_BUILD_EXAMPLES=ON`（默认开启），`app/` 和 `transport/` 下的程序还需要
`-DDLT698_BUILD_TRANSPORT=ON`（默认开启）。

:::tip 调试助手与配套客户端的区别
两个 TCP 服务端示例默认主动发起 LINK 登录，配套客户端会处理应答。普通调试助手若只连接后发送 GET，未完成的登录事务仍会在默认 5 秒后关闭连接。当前示例没有 profile 命令行开关；如何改为预设关联、重新构建及发送固定 GET，见 [TCP 手动报文调试](../transport/tcp-debug.md)。
:::

## 总览

### 普通服务器入口

`dlt698_server`：无参数监听 `0.0.0.0:6980`，传串口名时使用默认 9600/8E1；发布频率、三相电压/电流、有功/无功功率、功率因数、分费率电能和通信地址共 8 组固定模拟属性，按 Enter 停止。源码注释列出每组数据的 OAD、类型、倍率、实际值及数组顺序。库管理运行时和会话，核心业务只需设置和启动。完整说明见[托管服务器](../session/server.md)。

`dlt698_client`：无参数连接 `127.0.0.1:6980`，批量读取上述属性以及 A 相电压、费率 1 电能两个元素，按标准倍率显示实际值和单位后断开；传串口名时使用本地串口。数组顺序与服务端源码注释一致：电压/电流为 A/B/C，功率及功率因数为总/A/B/C，电能为总/费率 1～4。配对时先运行 `dlt698_server`，再在另一终端运行客户端。完整说明见[托管客户端](../session/client.md)。旧 TCP 客户端固定访问 `2000`，仍与原 tcp_server 配对。

### 编解码层

| 程序 | 用途 |
| --- | --- |
| `dlt698_decode` | 解一帧给定十六进制数据 |

### 对象服务层

全部跑在 `MemoryChannel::pair` 上，不需要真实设备，用于理解对象服务与会话的分层接线。

| 程序 | 用途 |
| --- | --- |
| `dlt698_memory_get` | CONNECT → GET NormalList（含部分成功）→ RELEASE |
| `dlt698_memory_mutation` | 串行链路适配下的同步 CONNECT → SET → ACTION → GET → RELEASE |
| `dlt698_standard_points` | 标准点位绑定、OAD 构造、逐项 DAR |
| `dlt698_standard_points_extended` | 分相电能、带时间的最大需量、谐波、状态字和参数 |
| `dlt698_standard_records` | 日冻结行列查询、GET Next 收齐、能力配置和显式点位探测 |

### 传输层

服务端与客户端各一个独立程序，**两侧使用同一份模拟对象目录**（`transport/demo_device.hpp`），
因此可以直接配对运行验证，不需要真实设备。RTU 一对需要各占一个真实串口。

| 程序 | 协议角色 | 链路建立方式 |
| --- | --- | --- |
| `dlt698_tcp_server` | 服务器 | `TcpListener` 监听，循环接受客户机 |
| `dlt698_tcp_client` | 客户机 | `TcpChannel::connect` 主动拨号 |
| `dlt698_rtu_server` | 服务器 | 打开串口，`SerialLinkChannel` 适配 |
| `dlt698_rtu_client` | 客户机 | 打开串口，`SerialLinkChannel` 适配 |
| `dlt698_master` / `dlt698_terminal` | 由宏决定 | TCP 监听或拨号，串口，用于验证双向拨号 |

传输层程序只在 `DLT698_BUILD_TRANSPORT=ON` 时构建。`dlt698_master` 和 `dlt698_terminal`
由同一份 `transport/peer.cpp` 编译，通过 `DLT698_TERMINAL` 宏区分角色。

## 编解码层

### dlt698_decode

解一帧固定测试数据，观察链路层和 APDU 的解析结果。

```sh
dlt698_decode "68 17 00 43 05 07 09 19 05 16 20 00 15 60 05 01 01 40 01 02 00 00 C6 07 16"
```

这帧来自标准附录 D.3.1 的 GET 请求固定向量。适合用来验证：

- 本地构建的编解码与标准向量一致；
- 链路层字段（地址、长度、控制字、HCS/FCS）解析正确；
- APDU 层的 GET Normal 请求结构符合预期。

## 对象服务层

### dlt698_memory_get

内存通道上的完整异步示例：注册对象、启动两端、CONNECT、读取数据与未知对象、RELEASE、关闭。

运行后可以观察精确 Data 字节和 `DAR=4`（未定义对象）的返回。

```sh
dlt698_memory_get
```

对应的 API 用法见：

- [对象目录与 Provider](../session/object.md)
- [Client/ServerService](../session/service.md)

### dlt698_standard_points 与 dlt698_standard_points_extended

基础示例读取电压、电流、电能和通信地址；扩充示例读取分相电能、最大需量及发生时间、B 相 2 次谐波、状态字、表号和需量周期。两者均通过内存链路完成 CONNECT → 批量 GET → RELEASE，见[标准固定点位](../protocol/standard-points.md)。

```sh
dlt698_standard_points
dlt698_standard_points_extended
```

### dlt698_standard_records

记录示例 `dlt698_standard_records` 使用 80 字节协商 APDU，输出 12 行冻结值，以及 `frequency=Data, voltage=DAR 4` 的逐项探测结果；详见[标准记录与能力筛选](../protocol/standard-records.md)。

```sh
dlt698_standard_records
```

### dlt698_memory_mutation

同步客户机 + 串行链路适配的完整示例：内存链路两端都经过 FE 前导和 33 位间隔处理，然后完成同步 CONNECT → SET → ACTION → GET → RELEASE。

```sh
dlt698_memory_mutation
```

输出：

```text
SET DAR=0 ACTION DAR=0 GET UInt16=42
```

这个示例是理解整体接线最好的入口：

- `ManualExecutor` + `MemoryChannel::pair` + `SerialLinkChannel::wrap` 组成内存链路；
- 协议角色通过 `SessionOptions::role` 区分，与拨号方向无关；
- `ObjectRegistry` + `MemoryObject` 提供可读可写属性和模式零方法；
- `SyncClientService` 带 drive 回调推进虚拟时钟；
- 方法回调用 `weak_ptr` 打破引用环。

完整代码和逐段说明见[第一个程序](../getting-started/quick-start.md)。

## 传输层

### 配对运行

TCP 一对可直接在同一台机器上验证，服务端会循环接受多个客户机：

```sh
# 终端 1
dlt698_tcp_server 0.0.0.0 6980 60
# 终端 2
dlt698_tcp_client 127.0.0.1 6980 get
dlt698_tcp_client 127.0.0.1 6980 set 25
dlt698_tcp_client 127.0.0.1 6980 action 25
dlt698_tcp_client 127.0.0.1 6980 record
```

串口一对需要两个真实端口（Windows 的 `COMx` 或 POSIX 的 `/dev/ttyUSBx`）：

```sh
dlt698_rtu_server COM3 9600 60
dlt698_rtu_client COM4 9600 get
```

### 用法

```text
dlt698_tcp_server <bind-address> <port> [lifetime-seconds]
dlt698_tcp_client <host> <port> get|set|action|record [value]

dlt698_rtu_server <device> <baud> [lifetime-seconds]
dlt698_rtu_client <device> <baud> get|set|action|record [value]
```

- `lifetime-seconds` 是整个示例程序的运行时限，默认 60 秒；到期后程序正常关闭退出，与单连接的 LINK 登录超时分开。
- `set` 和 `action` 必须带 `value`，`get` 和 `record` 不接受该参数。
- 串口模式同时演示了 [SerialLinkChannel](../transport/serial.md) 的 FE 前导和 33 位间隔适配。

### 两侧的差异

| 关注点 | 服务端 | 客户端 |
| --- | --- | --- |
| 角色 | `SessionOptions::role = Role::server` | `SessionOptions::role = Role::client` |
| 预连接 | TCP 需 `require_login` + `async_link(login)`；串口不需要 | `start()` 后即进入 `preconnected` |
| 心跳 | TCP 设 `heartbeat_seconds`，串口为 0 | 不涉及 |
| 生命周期 | 循环 accept，服务多个客户机直到时限 | 执行一条命令后 RELEASE 并退出 |
| 请求分发 | `ServerService` + `ObjectRegistry` | `SyncClientService` 同步调用 |

几个容易踩的点，这些示例都显式处理了：

- **业务成功不能只看外层 `Result`**。CONNECT 被拒绝时 `connect()` 返回成功但
  `response.result` 非零，客户端示例单独检查并退出。
- **串口没有"连接"概念**。RS-485 是半双工共享总线，服务端打开串口后就一直应答，
  没有 accept 也没有 LINK 登录。
- **串口帧间隔只是估算**。`SerialLinkChannel` 没有收到真实排空回调时按字节数与
  波特率推算时间，手动方向切换的 RS-485 必须自行注入 `async_drain`。
- **`run_for` 是事件循环的时间预算**，不是单次请求超时；事务超时由 `Session` 内部管理。

### dlt698_master 与 dlt698_terminal

这一对用于验证**协议角色与拨号方向相互独立**——`dlt698_master` 永远是协议客户机，
`dlt698_terminal` 永远是协议服务器，但两边都可以主动拨号：

| 场景 | 命令 |
| --- | --- |
| 主站拨号连终端 | `dlt698_master tcp-connect 127.0.0.1 4059 get` |
| 终端监听等主站 | `dlt698_terminal tcp-listen 0.0.0.0 4059` |
| 终端反向拨号 | `dlt698_terminal tcp-connect 127.0.0.1 4059` |
| 主站监听等终端 | `dlt698_master tcp-listen 0.0.0.0 4059 get` |

```text
dlt698_master tcp-connect|tcp-listen address port get|set|action|record [value]
dlt698_master serial device baud get|set|action|record [value]

dlt698_terminal tcp-listen|tcp-connect address port [lifetime-seconds]
dlt698_terminal serial device baud [lifetime-seconds]
```

具体行为以 `usage` 输出为准。相关 API 见 [TCP 通道](../transport/tcp.md)和[串口与串行链路](../transport/serial.md)。

## 在自己的项目里使用

安装后引用最方便：

```cmake
find_package(dlt698 1.0 CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE dlt698::dlt698)
```

也可以直接从源码子目录引入，只取需要的部分：

```cmake
add_subdirectory(third-party/dlt698)
target_link_libraries(your_app PRIVATE dlt698::dlt698)
```

所有示例均链接 `dlt698::dlt698`；不需要 TCP/串口时可在构建库时关闭传输功能。详见[构建选项](../getting-started/build-options.md)。

## 测试向量

标准测试向量位于 `tests/`，`tests/vectors/README.md` 说明来源和用途。这些 hex 文件既用于 CTest，也可以在写自己的解码测试时直接引用。
