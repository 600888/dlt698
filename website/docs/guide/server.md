---
title: 搭建服务端
description: 用 app::Server 模拟设备或搭建接入服务；数据发布、生命周期、并发限制与回调陷阱。
---

# 搭建服务端

## 能做什么

`app::Server` 是**被读的一方**：它监听 TCP 或串口，接受 `app::Client` 的连接，用真实协议语义对外提供数据。

两种典型用途：

| 用途 | 怎么做 |
| --- | --- |
| **联调 / 测试** | 起一个模拟设备，让客户端程序在没有真表的情况下跑通 |
| **网关 / 汇聚** | 起一个服务，把真实设备的数据转发给多个客户端 |

服务端由三部分组成：

| 部分 | 职责 |
| --- | --- |
| `service::Device` | 存放要对外发布的数据 |
| `service::ObjectSchema` | 声明可读属性和可执行方法 |
| `app::Server` | 协议关联、连接管理、线程生命周期 |

## 最小完整示例

一个提供三相电压、电流、频率的模拟设备：

```cpp
#include <dlt698/app.hpp>
#include <iostream>
#include <memory>

int main() {
    using namespace dlt698;
    namespace oi = standard::oi;

    const standard::DeviceLayout layout{standard::Wiring::three_phase, 4, 21};

    // 1) 共享设备：数据独立于连接生命周期，服务器停止也不清空
    auto device = std::make_shared<service::Device>(
        service::DeviceOptions{layout, {}, 256, 4096, 16 * 1024 * 1024});

    // 2) 发布数据：索引 0 的整体属性，数组长度由 layout 决定
    auto report = [](const char* what, Result<void> outcome) {
        if (!outcome) std::cerr << what << ": " << outcome.error().context << '\n';
    };

    report("voltage", device->set(model::Oad{oi::voltage, 2, 0},
                                  model::Data{model::Array{{model::Data{model::UInt16{2413}},
                                                            model::Data{model::UInt16{2414}},
                                                            model::Data{model::UInt16{2415}}}}}));
    report("current", device->set(model::Oad{oi::current, 2, 0},
                                  model::Data{model::Array{{model::Data{model::Int32{5000}},
                                                            model::Data{model::Int32{4980}},
                                                            model::Data{model::Int32{5010}}}}}));
    report("frequency", device->set(model::Oad{oi::frequency, 2, 0},
                                    model::Data{model::UInt16{5000}}));

    // 3) 起服务
    app::ServerOptions options;
    options.protocol.server.bytes = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    options.diagnostic = [](std::uint64_t id, const Error& error) {
        std::cerr << "[conn " << id << "] " << error.context << '\n';
    };

    app::Server server(device, options);
    auto started = server.start_tcp("127.0.0.1", 6980, app::ConnectionProfile::local_public);
    if (!started) {
        std::cerr << "start failed: " << started.error().context << '\n';
        return 1;
    }

    std::cout << "listening on port " << server.local_port()
              << ", state=" << (server.state() == app::ServerState::running ? "running" : "?")
              << '\n';

    // 4) 保持运行
    std::cout << "press enter to stop\n";
    std::cin.get();

    auto stopped = server.stop();
    if (!stopped) std::cerr << "stop: " << stopped.error().context << '\n';
    return 0;
}
```

把它和[客户端](./first-read.md)配对跑，就能完成一次完整的读取。

## 常用变体

### 端口传 0 让系统分配

```cpp
auto started = server.start_tcp("127.0.0.1", 0, app::ConnectionProfile::local_public);
if (started) std::cout << "port = " << server.local_port() << '\n';
```

`local_port()` 在 `start_tcp` 成功后返回**实际**监听端口；串口或停止后返回 0。测试代码里用这个避免固定端口冲突。

### 关联模式

`start_tcp` 的第三个参数决定服务端接受什么样的客户端：

| 模式 | 服务端期望 | 典型场景 |
| --- | --- | --- |
| `ConnectionProfile::local_public` | 客户端直接发 CONNECT | 局域网直连，最常用 |
| `ConnectionProfile::remote_public` | 客户端先 LINK 登录，再 CONNECT | 跨公网 |
| `ConnectionProfile::local_preset` | 跳过 CONNECT，直接读 | 已由网关完成关联 |

详见[TCP 连接](./tcp.md)。

:::tip 服务端和客户端的模式要配对
服务端用 `local_public` 时客户端也要用 `local_public`（单方面配对会失败）。角色由 `SessionOptions::role` 决定，拨号方向由传输层决定，两者相互独立 —— 详见[常见概念](../core/concepts.md)。
:::

### 定期更新数据

服务端跑起来后可以在业务线程更新数据：

```cpp
std::thread updater([device] {
    std::uint16_t freq = 5000;
    while (true) {
        device->set(model::Oad{0x200F, 2, 0}, model::Data{model::UInt16{freq}});
        freq = (freq == 5000) ? 4998 : 5000;   // 在 50.00 / 49.98 Hz 之间摆动
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
});
```

`Device::set` 是**线程安全**的，可以在运行中由业务线程调用。单次更新**原子发布**，但**不承诺不同属性之间的事务一致性** —— 别指望"改完电压后电流一定同步"。

### 更新数组的一个元素

```cpp
// 只改 A 相电流，保留 B/C 相
device->set_element(model::Oad{oi::current, 2, 1}, model::Data{model::Int32{5100}});
```

`set_element` 用**非零索引**（从 1 开始），只替换一个一级元素，不需要重发整个数组。整体属性用 `set`。

### 声明自定义对象

设备有厂家私有对象时，用 `define` 声明：

```cpp
service::ObjectSchema schema;
schema.oi = 0xF001;                 // 未收录的 OI
schema.name = "vendor_private";
schema.attributes = {
    {2, model::DataType::uint32, true, false, false},   // number=2, type, readable, writable, record
};
auto defined = device->define(schema);
if (!defined) std::cerr << "define: " << defined.error().context << '\n';

// 声明后即可发布
device->set(model::Oad{0xF001, 2, 0}, model::Data{model::UInt32{12345}});
```

约束：

- `define` 只支持**未收录**的 OI，用标准 OI 会失败
- 只支持普通**只读**属性，不支持方法或记录声明
- **类型声明后不能替换** —— 要改类型必须重建 `Device`
- 不发布未设置的属性 —— `define` 之后不 `set`，读到的是 DAR=4

:::warning 自定义对象不能远端写入
`define` 声明的属性 `writable` 恒为 false，且 `Device::set` 是**本地配置方法**，与协议 SET 服务无关。

要让远端能写入，必须走[服务层高级 API](./write-action.md#服务端开放写权限)用 `ObjectSchema` 声明 `writable = true` 并挂 provider，`Device::define` 做不到。
:::

### 串口服务端

```cpp
// 简单串口：默认 8E1
auto started = server.start_serial("COM3", 9600, app::ConnectionProfile::local_public);

// 完整配置：字格式 + 帧间隔预算
transport::SerialOptions serial;
serial.baud_rate = 19200;
serial.data_bits = 8;
serial.parity = transport::SerialParity::even;
serial.stop_bits = transport::SerialStopBits::one;
serial.flow_control = transport::SerialFlowControl::none;

// 帧间隔由波特率和字长推算；bits_per_character 含起始、数据、校验、停止位
transport::SerialLinkOptions link;
link.baud_rate = 19200;
link.bits_per_character = 11;   // 1 起始 + 8 数据 + 1 校验 + 1 停止

auto started2 = server.start_serial("/dev/ttyS0", serial, link);
```

`bits_per_character` **必须与实际端口配置一致**（这里是 8E1 = 11 位）。填错会导致帧间隔计算错误，在低波特率长帧时表现为偶发粘帧或丢帧。

1.5 停止位会被拒绝。手动 RS-485 方向切换**必须提供真实的 `async_drain` 回调**，只有时间估算无法保证硬件时序。详见[串口连接](./serial.md)。

### 多个客户端共享一份数据

```cpp
auto device = std::make_shared<service::Device>(service::DeviceOptions{layout});

// 两个服务器共享同一个 device
app::Server a(device, options_a);
app::Server b(device, options_b);
a.start_tcp("127.0.0.1", 6980);
b.start_tcp("127.0.0.1", 6981);

// 任一服务器停止都不影响 device，另一个照常服务
```

连接上限由 `ServerOptions::max_connections` 控制（默认 16）。**串口只有一个会话** —— 串口不能设这个参数。

### 在诊断回调里停止服务

`diagnostic` 在服务器自己的工作线程执行。如果在回调里调 `stop()`，会等待当前线程结束 —— **死锁**。用 `request_stop()`：

```cpp
options.diagnostic = [](std::uint64_t, const Error&) {
    server.request_stop();   // 异步请求，不等待
};

// 业务线程再调 stop() 等待收尾
server.stop();
```

## 请求与响应报文

服务端对外的报文与客户端请求一一对应，本文不重复帧示例。完整的读取报文见各数据类别页：

| 想看 | 参见 |
| --- | --- |
| 读单个属性 | [第一次读取](./first-read.md) |
| 批量读 | [批量读取](./batch-read.md) |
| 写属性 | [写入与调用方法](./write-action.md) |
| 记录查询 | [日冻结](./daily-freeze.md) |

## 相关配置

| 配置 | 位置 | 说明 |
| --- | --- | --- |
| `DeviceLayout::wiring` | 设备配置 | 决定电压等数组长度（3 或 1） |
| `DeviceLayout::tariff_count` | 设备配置 | 决定电能数组长度 |
| `DeviceOptions::max_value_bytes` | 设备配置 | 所有已发布值的编码字节总量上限，默认 16 MB |
| `DeviceOptions::max_objects` | 设备配置 | 含自定义声明的对象数上限，默认 256 |
| `DeviceOptions::max_attributes` | 设备配置 | 属性数上限，默认 4096 |
| `ServerOptions::max_connections` | 服务器配置 | TCP 活动连接上限，默认 16 |
| `ServerOptions::heartbeat_seconds` | 服务器配置 | `remote_public` 登录后的心跳周期，默认 5，零关闭 |
| `ServerOptions::diagnostic` | 服务器配置 | 诊断回调，**在服务器线程执行** |

## 常见失败

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| `start_tcp` 返回 `busy` | 已在运行，或并发启停 | `stop()` 后再启动 |
| 客户端连上但读不到数据 | 没有发布该属性 | `Device::get` 未发布时返回 DAR=4 |
| 读到的数组长度不对 | `layout.wiring` 与实际不符 | 三相是 3，单相是 1 |
| `set` 返回 `invalid_value` | 类型或数组长度与定义不符 | 值必须是精确协议类型 |
| `set_element` 返回 DAR=8 | 索引越界 | 索引从 1 开始，且不能超过数组长度 |
| `define` 失败 | OI 已被标准目录收录 | 标准对象不需要 define |
| `define` 后改不了类型 | 类型声明后锁定 | 重建 `Device` |
| 多个客户端连不上 | 超过 `max_connections` | 调大连接上限 |
| 串口只能接一个客户端 | 串口只有一个会话 | 用 TCP 或加串口服务器 |
| 在诊断回调里 `stop()` 死锁 | 等待自己所在线程 | 用 `request_stop()` |
| 数据更新了但客户端读到旧值 | 不承诺跨属性一致性 | 自行做一致性控制 |
| 服务器停止后数据丢了 | 数据在 `device` 上，不在服务器上 | 复用同一个 `device` 实例 |
| `local_port()` 返回 0 | 尚未启动、已停止或走串口 | `start_tcp` 成功后查询 |