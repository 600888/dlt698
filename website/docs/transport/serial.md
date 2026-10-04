---
title: 串口与串行链路
description: SerialChannel、SerialLinkChannel 与 RS-485 方向控制。
---

# 串口与串行链路

698 串行链路有两层：原始串口字节流（`SerialChannel`）和链路时序适配（`SerialLinkChannel`）。两者都必须经过。

## SerialChannel

头文件：`<dlt698/transport/serial.hpp>`。仅在 `DLT698_BUILD_TRANSPORT=ON` 时提供。

```cpp
enum class SerialParity { none, odd, even };
enum class SerialStopBits { one, one_point_five, two };
enum class SerialFlowControl { none, software, hardware };

struct SerialOptions {
    unsigned baud_rate = 9600;
    unsigned data_bits = 8;
    SerialParity parity = SerialParity::even;
    SerialStopBits stop_bits = SerialStopBits::one;
    SerialFlowControl flow_control = SerialFlowControl::none;
    ChannelOptions channel;
};

class SerialChannel final : public IChannel {
  public:
    static Result<std::shared_ptr<SerialChannel>> open(
        std::shared_ptr<IoRuntime> runtime, const std::string& device,
        SerialOptions options = {});
};
```

- `open` **同步**打开并配置串口，后续读写由运行时驱动。
- `device` 是 Windows COM 名称或 POSIX 设备路径，例如 `COM3`、`/dev/ttyUSB0`。Windows COMn 自动使用设备命名空间兼容高编号。
- 默认 9600/8E1。
- 失败返回错误，失败时会自动释放已打开的端口。

`SerialChannel` **不添加 FE 前导**，也不切换 RS-485 方向。写完成表示全部交给驱动，**不等价于**物理线路已经排空。

## SerialLinkChannel

头文件：`<dlt698/transport/serial_link.hpp>`。

```cpp
struct SerialLinkOptions {
    unsigned baud_rate = 9600;
    unsigned bits_per_character = 11;   ///< 含起始、数据、校验、停止位
    std::size_t max_pending_write_bytes = 1024 * 1024;
    std::size_t max_pending_writes = 128;
    std::function<Result<void>(bool)> set_transmit;                        ///< 幂等设置方向
    std::function<void(IChannel::WriteHandler)> async_drain;              ///< 驱动确认排空
};

class SerialLinkChannel final : public IChannel {
  public:
    static std::shared_ptr<SerialLinkChannel> wrap(
        std::shared_ptr<IChannel> raw, std::shared_ptr<IExecutor> executor,
        SerialLinkOptions options = {});
};
```

`SerialLinkChannel` **不依赖 Asio**，在 core 里，`DLT698_BUILD_TRANSPORT=OFF` 时也能用——可以直接包 `MemoryChannel` 做确定性测试。

### 它做什么

```text
发送：encode_frame 的完整帧 → 前置四个 FE → 等待发送间隔 → 写入 raw
接收：raw 的原始字节（含 FE）→ 交给 FrameStreamDecoder
```

- **只接受恰好一个含校验的完整帧。** `async_write` 不接受半帧或多帧拼接。
- 发送时添加**四个 FE**，然后按真实排空或软件估算的输出时间，保留**至少 33 位**的字符间隔。
- **最近一次接收之后**才开始发送的，也要延后 33 位。
- 队列预算**包括 FE**。
- 读取保留 FE，不剥离，由 `FrameStreamDecoder` 处理。

### 参数一致性

:::warning bits_per_character 必须与端口一致
`SerialLinkOptions::bits_per_character` 是含起始、数据、校验、停止位的总位数，**必须与实际串口配置一致**。9600/8E1 对应 11 位。

非整数停止位（如 1.5）按**向上取整**配置位数。
:::

波特率和每字符位数共同决定 33 位间隔的实际时间。配置错误会导致时序不对，表现为 sporadic 校验错误或丢帧。

## RS-485 方向控制

```cpp
std::function<Result<void>(bool)> set_transmit;   // true 发送，false 接收
std::function<void(IChannel::WriteHandler)> async_drain;
```

`SerialLinkOptions` 提供两个可选的驱动 hook：

- **`set_transmit(bool)`** 幂等设置方向。`true` 进入发送，`false` 回到接收。
- **`async_drain(handler)`** 由**驱动**确认最后停止位已经发出，再调用完成回调。可以从其他线程完成。

:::danger 手动方向切换必须有真实排空
配置了 `set_transmit` 却没有 `async_drain` 时，`wrap` 抛 `std::invalid_argument`。

只在发送估算时间而不接真实排空接口，会导致最后几个字节还在移位寄存器里就切回接收，末字节被破坏。**没有真实排空接口时禁止手动方向配置。**
:::

`async_drain` 的回调可以在其他线程调用，说明驱动是异步完成排空确认的。

### 有排空 vs 只估算

| 情况 | 时序保证 |
| --- | --- |
| 提供 `async_drain` | 由驱动确认最后停止位发出后才完成，时序可靠 |
| 只有 `set_transmit` + 估算 | 适合**自动方向**适配器，由字节数和波特率估算输出时间 |

:::warning 估算不能保证硬件时序
只用估算时，**不能保证** USB 缓冲、硬件流控或特定适配器的时序正确。

带流控或需要严格排空的设备应接入 `async_drain`。
:::

关闭时 `SerialLinkChannel` 会取消定时器和队列，并尽力恢复接收方向。驱动排空失败时**不重放**数据。

## 完整接线

```cpp
#include <dlt698/transport/serial.hpp>
#include <dlt698/transport/serial_link.hpp>

auto runtime = std::make_shared<dlt698::transport::IoRuntime>();
auto executor = runtime->executor();

auto raw = dlt698::transport::SerialChannel::open(runtime, "COM3");
std::shared_ptr<dlt698::session::Session> session;
if (raw) {
    dlt698::transport::SerialLinkOptions link;
    link.baud_rate = 9600;
    link.bits_per_character = 11;

    auto channel = dlt698::transport::SerialLinkChannel::wrap(raw.value(), executor, link);
    session = std::make_shared<dlt698::session::Session>(channel, executor);
    session->start();
}
// 应用保留 session，继续发起连接并驱动 runtime。
```

:::warning 应用必须持有 session
示例中的 `session` 是 `shared_ptr`，必须由应用持有到最后。排队任务不会永久保活 `Session`；序列化适配器和原始通道都是共享所有权，销毁适配器会同时关闭原始通道。
:::

## 在内存链路上验证

`SerialLinkChannel` 可以直接包 `MemoryChannel`，不需要真实硬件就能验证 FE 前导和 33 位间隔：

```cpp
auto executor = std::make_shared<dlt698::ManualExecutor>();
auto raw = dlt698::transport::MemoryChannel::pair(executor);
auto master_link = dlt698::transport::SerialLinkChannel::wrap(raw.first, executor);
auto terminal_link = dlt698::transport::SerialLinkChannel::wrap(raw.second, executor);
```

完整示例见[第一个程序](../getting-started/quick-start.md)。

## 验证状态

:::caution 硬件互操作未验证
当前证据为虚拟时间/内存链路模拟，以及本机串口参数配置和打开失败路径。**真实串口收发与 RS-485 硬件互操作尚未验证。**

现场部署前请自行完成硬件时序测量。相关测试状态见[协议覆盖范围](../appendix/coverage.md)。
:::
