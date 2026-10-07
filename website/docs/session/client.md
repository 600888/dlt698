---
title: 托管客户端
description: 直接连接、读取和写入，自动管理运行线程及协议关联。
---

# 托管客户端

包含 `<dlt698/app.hpp>`，链接 `dlt698::app`，开启 `DLT698_BUILD_TRANSPORT`。核心调用只需创建、连接、读取三步：

```cpp
dlt698::app::Client client;
auto connected = client.connect_tcp("127.0.0.1", 6980);
auto value = client.get({0x200F, 2, 0});
```

实际程序先检查 `connected`，再读取。连接成功表示传输和必要的 LINK/CONNECT 已完成；库拥有运行线程，不需要调用方创建 Session 或手动 `run_for()`。

`cpp/examples/app/client.cpp` 是完整可编译示例，构建为 `dlt698_client`，与 `dlt698_server` 配对。先在一个终端运行服务器，再在另一个终端运行客户端：

```sh
dlt698_server
dlt698_client
```

客户端读取标准频率 `200F/2/0`，输出 `Frequency=5000 (unit: 0.01 Hz)`，随后断开。服务器继续接入，按 Enter 停止。旧 `dlt698_tcp_client` 访问旧模拟目录 `2000`，仍与原 `dlt698_tcp_server` 配对。

## 串口与关联场景

串口将连接行替换为 `client.open_serial("COM4", 9600)`；服务器使用另一个相连的串口 `server.start_serial("COM3", 9600)`。默认 8E1，库自动安装四 FE 和 33 位间隔适配。完整重载接受 `SerialOptions`、`SerialLinkOptions`，规则与服务器相同：速率和位数由实际字格式派生，拒绝 1.5 停止位；手动 RS-485 方向切换必须同时提供真实 `async_drain`。

| 场景 | 客户端行为 | 默认入口 |
| --- | --- | --- |
| `remote_public` | 等待服务器发起 LINK，自动应答后发送公共 CONNECT | TCP |
| `local_public` | 直接发送公共 CONNECT | 串口 |
| `local_preset` | 使用显式本地预设关联，不发送 CONNECT | 完整重载中显式选择 |

两端必须配置相同场景，SA/CA 也必须匹配。默认 SA 字节 `{0}`、逻辑地址零和 CA 零，可通过 `ClientOptions::protocol` 指定实际地址、能力、TimeTag 与预算；客户端不猜测现场表计地址。

:::tip 连接真实电表或使用调试助手
默认 TCP 客户端会等待服务器 LINK，若实际对端采用本地公共或预设关联，应显式选择对应 profile；默认等待可能返回 `LINK login timeout`。反过来，调试助手连接默认远程服务器后不回复 LINK，会使服务器约 5 秒后关闭连接。场景选择、完整报文与直接 GET 示例见 [TCP 手动报文调试](../transport/tcp-debug.md)。
:::

## 读取、写入与错误

| 方法 | 结果 |
| --- | --- |
| `get/get_list` | Data 或原始 DAR；列表保留顺序和部分成功 |
| `get_record/get_record_list` | 完整行列快照或逐查询 DAR，自动收齐 GET Next 和链路分帧 |
| `set/set_list` | 原始 DAR 或逐项 DAR，不回滚部分成功 |
| `action/action_list` | DAR 及可选 Data，不自动重试 |

外层 `Result` 成功表示收到合法结果，还需检查 DAR。托管 Server 的本地 `set` 默认只读地发布数据，因此客户端远端 SET 返回 DAR=3；需要可写属性、方法或记录的服务器仍通过已有高级 schema/provider 接入。

```cpp
auto value = client.get({0x200F, 2, 0});
if (!value) {
    std::cerr << value.error().context << '\n';
} else if (const auto dar = std::get_if<std::uint8_t>(&value.value())) {
    std::cerr << "DAR=" << unsigned(*dar) << '\n';
} else {
    const auto& data = std::get<dlt698::model::Data>(value.value());
    // 根据 OAD 对应的精确类型使用 data，完整示例还检查 UInt16 类型。
}
```

CONNECT 拒绝返回 `association_failed`，`Error::remote_code` 保留原始认证结果码；远端 ERROR 返回 `remote_error` 并保留原码。SET/ACTION 超时或断线只能说明本地未取得完成结果，远端是否执行可能未知，库不会自动重放。

## 超时、并发和关闭

`ClientOptions::transport_timeout` 默认 5 秒，限制 DNS/TCP 建连的合计时长；`login_timeout` 默认 5 秒，限制远程 LINK 等待；`protocol.request_timeout` 默认 5 秒，限制 CONNECT、业务请求和 RELEASE。失败关闭并排空在途 I/O，之后可以显式重新连接。操作系统同步打开串口的耗时不受 DNS/TCP 超时控制。

一个连接只允许一个在途请求，并发冲突返回 `busy`，不维护无界请求队列。`state()` 的 `connected` 表示已关联，对端关闭或结束关联后最终变为 `disconnected`。

正常结束调用 `disconnect()`：空闲关联先尝试 RELEASE，然后等待传输、取消回调及线程收尾；即使 RELEASE 失败仍关闭，失败结果交给调用方。有在途请求时直接关闭并唤醒请求等待。重复断开成功，之后可以重新连接。

`request_disconnect()` 可取消正在建连或执行的请求，不阻塞、不发送 RELEASE。诊断回调在工作线程运行，回调内同步连接、读取或断开返回 `busy`，应使用 `request_disconnect()`。析构强制关闭，回调内释放最后句柄由共享的托管线程回收器处理，不 self-join、不 detach。

真实回环测试覆盖三种关联、DNS 主机名、读写/方法/分块记录、部分成功、CONNECT/ERROR 原码、登录/请求超时、取消和回调内销毁；安装调用方只链接 app 目标。真实串口、RS-485 物理时序和独立设备互操作仍待硬件验收。客户端监听、服务器拨号、自动恢复连接和外部运行时属于后续阶段。
