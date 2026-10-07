---
title: TCP 连接
description: IP、端口和地址配置，以及 remote_public / local_public / local_preset 三种关联模式的选择依据。
---

# TCP 连接

## 一个完整的 TCP 客户端

```cpp
#include <dlt698/app.hpp>
#include <iostream>

int main() {
    using namespace dlt698;
    app::ClientOptions options;
    // SA：服务器地址，按线序填写，低有效字节在前。
    // 123456789012 写成 {0x12, 0x90, 0x78, 0x56, 0x34, 0x12}。
    options.protocol.server.bytes = {0x12, 0x90, 0x78, 0x56, 0x34, 0x12};
    options.protocol.server.logical = 0;  // 逻辑地址，范围 0～3
    options.protocol.client_address = 0;  // CA，客户端地址，两端必须一致

    app::Client client(options);
    // 主机名由 DNS 解析；host 可以是 IP 或域名。
    auto connected = client.connect_tcp("192.168.1.100", 6980);
    if (!connected) {
        std::cerr << connected.error().context << '\n';
        return 1;
    }
    auto value = client.get({0x200F, 2, 0});
    // ... 检查 value
    return client.disconnect() ? 0 : 1;
}
```

三个入口参数：主机、端口、关联模式。地址和能力不在入口参数里，通过 `options.protocol` 配置。

## 监听端配置

```cpp
app::ServerOptions options;
options.protocol.server.bytes = {0x12, 0x90, 0x78, 0x56, 0x34, 0x12};
options.protocol.server.logical = 0;
options.protocol.client_address = 0;
options.max_connections = 16;      // TCP 活动连接上限，默认 16
options.heartbeat_seconds = 5;     // remote_public 登录后的心跳周期，0 关闭

app::Server server(options);
auto started = server.start_tcp("0.0.0.0", 6980);
```

`start_tcp` 的第一个参数是**绑定地址**，必须是数字形式（IPv4/IPv6），不执行域名解析。`"0.0.0.0"` 监听所有网卡，`"127.0.0.1"` 只接受本机连接。端口传 0 时由系统分配，之后用 `server.local_port()` 查询实际端口。

## 地址配置：SA、逻辑地址、CA

这三个字段是会话路由的匹配条件，**两端必须完全一致，包括地址长度**。

| 字段 | 含义 | 取值 |
| --- | --- | --- |
| `server.type` | 地址类型 | `single`（单地址，默认）、`wildcard`、`group`、`broadcast` |
| `server.bytes` | SA 地址字节 | 1～16 字节，按线序，低有效字节在前 |
| `server.logical` | 逻辑地址 | 0～3 |
| `client_address` | CA 客户端地址 | 0～255，两端一致 |

**地址按线序保存，不要按主机序重排。** 这是最容易搞错的地方：地址 `123456789012` 的字节顺序是 `12 90 78 56 34 12`，因为低有效字节在前。照抄厂家资料里的数字时必须转换。

默认 SA 只有一个字节 `{0}`。**如果设备是六字节地址，必须显式改成六字节**，即使全零也要写全：

```cpp
options.protocol.server.bytes = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};  // 对
// options.protocol.server.bytes = {0x00};                            // 错：长度不匹配
```

长度不一致时帧在分发前被拒绝，表现为超时或连接被关闭，不会有一条明确的"地址长度错误"提示。

## 三种关联模式怎么选

这是 TCP 和串口连接都要面对的问题。DL/T 698.45 的连接建立分两步：协议服务器发起 LINK 登录，协议客户端发起 CONNECT 建立应用连接。不同设备的选择不同。

| `ConnectionProfile` | 协议流程 | 什么时候用 |
| --- | --- | --- |
| `remote_public` | 服务器 LINK 登录 → 客户端确认 → 客户端 CONNECT → GET | 采集终端、集中器、网关等远程设备，需要服务端管理登录 |
| `local_public` | 客户端 CONNECT → GET | 本地表计对等连接，不使用 LINK 但需要公共应用关联 |
| `local_preset` | 直接 GET | 已确认两端预设关联，或手动调试 |

`connect_tcp` 默认是 `remote_public`，`open_serial` 默认是 `local_public`。

### 选择依据

**对端会主动发 LINK 请求，就用 `remote_public`。** 现象是 TCP 连上后先收到一个 `68 ... 81 ... 01 ... 16` 的帧。不回复它，约 5 秒后连接被关闭。用调试助手连接默认 `dlt698_server` 就会看到这个现象——它是 `remote_public` 服务端。

**对端不发 LINK 但要求 CONNECT，用 `local_public`。** 现象是连上后可以直接发 CONNECT，拿到 CONNECT 响应后才允许 GET。只改成 `local_public` 仍然需要 CONNECT，不能直接 GET。

**对端既不发 LINK 也不需要 CONNECT，用 `local_preset`。** 现象是连上后可以直接发 GET。适合手工调试和固定部署。

### 常见错误

**客户端选 `remote_public` 但对端不发 LINK。** 会返回 `LINK login timeout`。这时应该改用 `local_public` 或 `local_preset`：

```cpp
auto connected = client.connect_tcp("192.168.1.100", 6980,
                                    app::ConnectionProfile::local_preset);
```

**把心跳周期设为 0 以为能跳过登录。** 不能。`remote_public` 仍会主动发登录请求，心跳周期只是登录之后的间隔。

**只延长超时。** `request_timeout` 延长只是推迟断开，不会让未完成的登录变成已完成。

**两端模式不一致。** 关联策略和 SA/CA 都要匹配，否则会出现 LINK 响应无人处理、CONNECT 被拒或 GET 返回 DAR。

### 标准依据与本库实现

按 DL/T 698.45—2017 第 6.1.1.2 节，本地 RS-485 和红外通道默认具有预连接通道；第 6.1.1.3.1、6.1.1.3.3 节规定预连接通道有最低权限的预建立应用连接窗口。

本库把场景做成显式配置。`local_preset` 使用配置的能力和尺寸，**并未完整实现标准预建立窗口的权限切换**，因此不能据此认为所有对象都能直接访问；需要更高权限时仍要建立公共应用连接。TCP 透传本地表计时，按对端实际支持的场景选择，不能仅凭传输是 TCP 就认定对端一定发 LINK。

## 超时配置

四个时限各有作用域，容易混淆：

| 参数 | 默认值 | 作用 |
| --- | --- | --- |
| `ClientOptions::transport_timeout` | 5 秒 | DNS 解析加 TCP 建连的合计时限 |
| `ClientOptions::login_timeout` | 5 秒 | 客户端等待远程服务器 LINK 的阶段时限 |
| `SessionOptions::request_timeout` | 5 秒 | 单个协议事务：CONNECT、各类请求、RELEASE |
| `SessionOptions::parameters.timeout_seconds` | 100 秒 | 应用关联的空闲超时，写在线上参与协商 |
| `ServerOptions::heartbeat_seconds` | 5 秒 | 服务端登录后的自动心跳周期，0 关闭 |

```cpp
app::ClientOptions options;
options.transport_timeout = std::chrono::milliseconds{3000};
options.login_timeout = std::chrono::milliseconds{10000};  // 对端登录慢时放宽
options.protocol.request_timeout = std::chrono::milliseconds{8000};  // 大记录分块多时放宽
```

**超时或 `cancel()` 会关闭物理通道。** 线上协议没有 generation 字段，无法区分迟到的响应和下一个请求的响应，本库的取舍是直接断开而不是继续复用连接。所以超时之后要重新连接，不能在同一个 `Client` 上假设连接还可用。重新 `connect_tcp` 是允许的。

**大记录查询要放宽 `request_timeout`。** 记录响应超过协商的 APDU 长度时会自动分块，每块一次往返，总耗时可能远超单块时间。

## 断开与重连

```cpp
auto released = client.disconnect();  // 空闲时先尝试 RELEASE，再关闭并回收线程
client.request_disconnect();          // 异步取消，不发送 RELEASE，可在回调内调用
auto state = client.state();          // connected / disconnected / connecting / disconnecting
```

`disconnect()` 会等待传输、取消回调和线程全部收尾。重复调用成功，之后可以重新连接。正常退出用 `disconnect()`；在诊断回调里用 `request_disconnect()`，因为回调内同步调用会返回 `busy`。

## 典型现场问题

**连接后立刻收到登录帧然后约 5 秒断开。** 服务端是 `remote_public`，对端没回 LINK。要么用配套客户端，要么把服务端改成 `local_preset`，见[调试助手使用](./packet-debug.md)。

**连接超时。** 依次确认：IP 和端口是否可达（先 telnet）、SA/CA 是否两端一致、地址长度是否一致、设备是否需要先发 LINK。更多见[常见问题排查](./troubleshooting.md)。

**收到 GET 响应但全是 DAR。** 传输和关联都通了，是对象或属性不被支持。用[协议覆盖范围](../appendix/coverage.md)核对，并确认属性号没写错（属性 1 是逻辑名，属性 2 才是数据）。

## 下一步

- 串口连接：[串口连接](./serial.md)
- 手动发报文验证：[调试助手使用](./packet-debug.md)
- 底层接口（自己管理监听、通道、运行时）：[TCP 通道](../transport/tcp.md)