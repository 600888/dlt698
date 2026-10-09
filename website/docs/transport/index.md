---
title: 传输层
description: 执行器模型、通道接口、TCP 与串口。
comments: false
---

# 传输层

统一库 `dlt698::dlt698` 的传输接口提供字节流通道。TCP 和原始串口依赖 standalone Asio，但**只有传输模块**用它——消费方不需要 Asio 头文件。

## 这一部分的页面

| 页面 | 内容 |
| --- | --- |
| [执行器](./executor.md) | `IExecutor`、`ITimer`、`ManualExecutor` 虚拟时钟 |
| [通道与内存通道](./channel.md) | `IChannel` 接口、`ChannelOptions`、`MemoryChannel` |
| [TCP 通道](./tcp.md) | `IoRuntime`、`TcpChannel`、`TcpListener` |
| [TCP 手动报文调试](./tcp-debug.md) | LINK 登录、5 秒断开、连接模式与可直接发送的 GET |
| [串口与串行链路](./serial.md) | `SerialChannel`、`SerialLinkChannel`、RS-485 方向控制 |

## 组成部分

| 类型 | 归属 | 说明 |
| --- | --- | --- |
| `IExecutor` / `ITimer` | core | 串行执行器与单调计时器 |
| `ManualExecutor` | core | 手动驱动的虚拟时钟，仅单线程 |
| `IChannel` | core | 异步字节流接口 |
| `MemoryChannel` | core | 有界内存通道，模拟用 |
| `SerialLinkChannel` | core | 698 串行链路时序适配 |
| `IoRuntime` | transport | 由应用驱动的 Asio 事件循环 |
| `TcpChannel` / `TcpListener` | transport | TCP 连接与监听 |
| `SerialChannel` | transport | 原始串口字节流 |

:::tip core 与 transport 的区别
`MemoryChannel` 和 `SerialLinkChannel` 在 `core` 里，**不依赖 Asio**，用 `DLT698_BUILD_TRANSPORT=OFF` 也能用。只有 `IoRuntime`、`TcpChannel`、`TcpListener`、`SerialChannel` 需要传输构建。
:::

## 没有隐藏线程

这是整个传输层最重要的设计约定：

- `IoRuntime` **不创建内部工作线程**，由应用调用 `run()` / `run_for()` 驱动。
- `ManualExecutor` 同样不创建线程，时间由 `advance()` 推进。
- `SyncClientService` 不创建线程，只在调用线程上等待。
- `IExecutor::post`/`schedule` 必须**延后**执行任务，不能在投递函数内直接调用用户任务。

因此"谁驱动运行时"始终是应用的责任。这也是为什么模拟（`ManualExecutor` + `MemoryChannel`）和生产（`IoRuntime` + `TcpChannel`）能共用同一套会话代码。
