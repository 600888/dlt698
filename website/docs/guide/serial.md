---
title: 串口连接
description: 串口名、波特率、校验位、停止位、流控，以及 RS-485 方向切换和帧间隔的配置。
---

# 串口连接

## 最简形式

```cpp
#include <dlt698/app.hpp>
#include <iostream>

int main() {
    using namespace dlt698;
    app::ClientOptions options;
    options.protocol.server.bytes = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    app::Client client(options);
    // Windows 用 COM 名，POSIX 用设备路径。
    auto connected = client.open_serial("COM3", 9600);
    if (!connected) {
        std::cerr << connected.error().context << '\n';
        return 1;
    }
    auto value = client.get({0x200F, 2, 0});
    // ... 检查 value
    return client.disconnect() ? 0 : 1;
}
```

服务端对应 `server.start_serial("COM4", 9600)`。串口一端接采集终端，另一端接你的程序。

## 字格式

`open_serial(path, baud)` 用的是默认 8E1。需要非默认参数时用完整重载：

```cpp
transport::SerialOptions serial;
serial.baud_rate = 2400;
serial.data_bits = 8;
serial.parity = transport::SerialParity::even;      // none / odd / even
serial.stop_bits = transport::SerialStopBits::one;  // one / one_point_five / two
serial.flow_control = transport::SerialFlowControl::none;  // none / software / hardware

transport::SerialLinkOptions link;
link.baud_rate = 2400;
link.bits_per_character = 11;  // 1 起始 + 8 数据 + 1 校验 + 1 停止
link.set_transmit = [](bool sending) { /* 切 RS-485 方向 */ return Result<void>{}; };
link.async_drain = [](IChannel::WriteHandler done) { /* 真实排空后回调 done */ };

auto connected = client.open_serial("COM3", serial, link,
                                    app::ConnectionProfile::local_public);
```

`bits_per_character` 含起始、数据、校验和停止位。它影响帧间隔计算，必须和实际字格式一致：8E1 是 11 位，8N1 是 10 位，8E2 是 12 位。填错会导致帧间隔估算偏差。

**`one_point_five` 停止位会被拒绝**，返回配置错误。

## 帧间隔和 FE

DL/T 698.45 的串行链路有两个要求：每帧前加四个 FE（前导字节 `0xFE`），相邻帧之间保留至少 33 位时间的间隔。`open_serial` 和 `start_serial` 内部的 `SerialLinkChannel` 会自动处理，不需要自己加。

## RS-485 方向切换

半双工 RS-485 需要在发送和接收之间切换方向。两种模式：

**自动方向适配器（推荐用于 USB 转 RS-485）** —— 硬件自己控制方向，程序不需要配置：

```cpp
transport::SerialLinkOptions link;  // 不设置 set_transmit
```

此时 `SerialLinkChannel` 只按字节数和波特率估算发送时间。**这个估算不保证硬件时序**，USB 转串口芯片的内部缓冲和操作系统的排队都会引入延迟。现场可靠性要求高时，需要用带自动方向控制的适配器并实测。

**手动方向切换** —— 程序控制 DE 脚，必须提供真实排空回调：

```cpp
transport::SerialLinkOptions link;
link.set_transmit = [&](bool sending) -> Result<void> {
    // sending=true 切到发送，false 切到接收。幂等。
    return Result<void>{};
};
link.async_drain = [&](transport::IChannel::WriteHandler done) {
    // 必须等最后一个停止位真正发出去（通常是 DE 脚实际落下）再回调，
    // 不能只是"调用了 write"或"数据拷进了驱动缓冲区"。
    drain_hardware(done);
};
```

:::danger 手动方向切换缺 `async_drain` 会在构造时失败
配置了 `set_transmit` 却没有 `async_drain`，`SerialLinkChannel::wrap` 抛 `std::invalid_argument`。这不是可以忽略的警告——没有真实排空就切回接收，最后一个字节会被丢掉，表现为间歇性的帧错误和 CRC 失败。
:::

**为什么必须真实排空**：`async_write` 成功只表示字节全部交给了驱动，不代表物理线路已经发完。如果不等最后停止位发出就切方向，对端收到的帧被截断，表现为 FCS 校验失败。这类故障时好时坏，很难定位。

## 关联模式

串口默认 `local_public`：不发 LINK，直接 CONNECT。本地表计一般不需要远程登录。

```cpp
// 需要 LINK 登录的串口设备
client.open_serial("COM3", 9600, app::ConnectionProfile::remote_public);
// 已确认预设关联
client.open_serial("COM3", 9600, app::ConnectionProfile::local_preset);
```

判断依据和 TCP 一样，见[TCP 连接的三种关联模式](./tcp.md#三种关联模式怎么选)。本地 RS-485 按标准通常具有预连接通道，很多表计既不发 LINK 也不需要 CONNECT。

## 通道预算

```cpp
transport::ChannelOptions channel;
channel.read_chunk_bytes = 4096;          // 单次读取缓冲，1～1 MiB
channel.max_pending_write_bytes = 1024 * 1024;
channel.max_pending_writes = 128;
serial.channel = channel;
```

`read_chunk_bytes` 是接收缓冲大小，和协议帧长度无关。一帧可能被拆成多次读取，也可能一次读取包含多帧，流解析由库处理。

写入预算是背压保护。发送速度快于对端响应时，写队列满会返回 `resource_limit`。

## 现场排查

**打不开串口。** 确认端口号正确、设备未被其他程序占用、当前用户有权限、串口确实存在（Windows 上设备管理器确认 COM 号；Linux 上 `/dev/ttyUSB*` 路径和拨号用户）。

**能打开但收不到任何字节。** 先确认波特率和字格式匹配。RS-485 的 A/B（D+/D-）接反也很常见，表现为完全无响应。

**收到数据但校验失败。** 优先怀疑方向切换时序（没有真实排空）、字格式不符（校验位错）、或 `bits_per_character` 与实际不一致。

**偶发 CRC 失败、长报文更容易失败。** 典型的方向切换时序问题或线路干扰。缩短线路、降低波特率、加屏蔽，或者改成带自动方向控制的适配器。

**`request_timeout` 要放宽。** 串口比 TCP 慢得多。2400 波特率下，一个 300 字节的记录响应需要好几秒。

```cpp
options.protocol.request_timeout = std::chrono::milliseconds{15000};
```

**操作系统同步打开串口的耗时不受 `transport_timeout` 控制。** `open_serial` 是同步调用，打开端口的耗时取决于驱动和设备，不归这个 5 秒时限管。

## 下一步

- 三种关联模式的完整说明：[TCP 连接](./tcp.md)
- 底层串口接口与串行链路细节：[串口与串行链路](../transport/serial.md)
- 抓包和手动验证：[调试助手使用](./packet-debug.md)