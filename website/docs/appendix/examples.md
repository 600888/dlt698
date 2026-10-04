---
title: 示例程序
description: 仓库附带示例的用途、构建与运行方式。
---

# 示例程序

示例源码位于 `cpp/examples/`，构建后输出到 `bin/`（Visual Studio 多配置构建为 `bin/Release/`）。

构建示例需要 `-DDLT698_BUILD_EXAMPLES=ON`（默认开启）。

## 概览

| 程序 | 用途 | 需要设备 |
| --- | --- | --- |
| `dlt698_decode` | 解一帧给定十六进制数据 | 否 |
| `dlt698_memory_get` | 内存通道上的 CONNECT → GET → RELEASE | 否 |
| `dlt698_memory_mutation` | 内存链路上的同步 CONNECT → SET → ACTION → GET → RELEASE | 否 |
| `dlt698_master` | 协议客户机：TCP/串口下的 get、set、action、record | 视模式而定 |
| `dlt698_terminal` | 协议服务器：监听或拨号，接受主站请求 | 视模式而定 |

`dlt698_master` 和 `dlt698_terminal` 只在 `DLT698_BUILD_TRANSPORT=ON` 时构建，由同一份 `peer.cpp` 编译，通过 `DLT698_TERMINAL` 宏区分角色。

## dlt698_decode

解一帧固定测试数据，观察链路层和 APDU 的解析结果。

```sh
dlt698_decode "68 17 00 43 05 07 09 19 05 16 20 00 15 60 05 01 01 40 01 02 00 00 C6 07 16"
```

这帧来自标准附录 D.3.1 的 GET 请求固定向量。适合用来验证：

- 本地构建的编解码与标准向量一致；
- 链路层字段（地址、长度、控制字、HCS/FCS）解析正确；
- APDU 层的 GET Normal 请求结构符合预期。

## dlt698_memory_get

内存通道上的完整异步示例：注册对象、启动两端、CONNECT、读取数据与未知对象、RELEASE、关闭。

运行后可以观察精确 Data 字节和 `DAR=4`（未定义对象）的返回。

```sh
dlt698_memory_get
```

对应的 API 用法见：

- [对象目录与 Provider](../session/object.md)
- [Client/ServerService](../session/service.md)

## dlt698_memory_mutation

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

## dlt698_master 与 dlt698_terminal

一对协议对端程序，用于验证两种拨号方向和真实 socket 路径。

**协议角色与拨号方向独立**——`dlt698_master` 永远是协议客户机，`dlt698_terminal` 永远是协议服务器，但两边都可以主动拨号：

| 场景 | 命令 |
| --- | --- |
| 主站拨号连终端 | `dlt698_master tcp-connect 127.0.0.1 4059 get` |
| 终端监听等主站 | `dlt698_terminal tcp-listen 0.0.0.0 4059` |
| 终端反向拨号 | `dlt698_terminal tcp-connect 127.0.0.1 4059` |
| 主站监听等终端 | `dlt698_master tcp-listen 0.0.0.0 4059 get` |

### 用法

```text
dlt698_master tcp-connect|tcp-listen address port get|set|action|record [value]
dlt698_master serial device baud get|set|action|record [value]

dlt698_terminal tcp-listen|tcp-connect address port [lifetime-seconds]
dlt698_terminal serial device baud [lifetime-seconds]
```

- `address`：`tcp-connect` 填远端地址，`tcp-listen` 填本地绑定地址。
- `lifetime-seconds`：应用连接空闲时限，默认 60 秒。
- `serial` 模式同时演示了 [SerialLinkChannel](../transport/serial.md) 的 FE 前导和 33 位间隔适配。

具体行为以 `--help` 和 `usage` 输出为准。相关 API 见 [TCP 通道](../transport/tcp.md)和[串口与串行链路](../transport/serial.md)。

## 在自己的项目里使用

安装后引用最方便：

```cmake
find_package(dlt698 0.1 CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE dlt698::dlt698)
```

也可以直接从源码子目录引入，只取需要的部分：

```cmake
add_subdirectory(third-party/dlt698)
target_link_libraries(your_app PRIVATE dlt698::service)
```

按需链接 `dlt698::core`、`dlt698::session`、`dlt698::service`、`dlt698::transport` 可以显著缩短编译时间。详见[构建选项](../getting-started/build-options.md)。

## 测试向量

标准测试向量位于 `tests/`，`tests/vectors/README.md` 说明来源和用途。这些 hex 文件既用于 CTest，也可以在写自己的解码测试时直接引用。
