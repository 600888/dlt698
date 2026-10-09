---
title: 托管服务器与设备数据
description: 直接设置数据并启动 TCP 或串口，自动管理连接和运行线程。
---

# 托管服务器与设备数据

`app::Server` 是普通应用的服务器入口：直接设置数据，启动 TCP 或串口，库会驱动事件循环、接入连接、处理协议登录及请求。`service::Device` 保存数据，连接断开和服务器停止都不会清空它。

需要开启 `DLT698_BUILD_TRANSPORT`，包含 `<dlt698/app.hpp>` 并链接 `dlt698::dlt698`。Device 同样由统一库提供，关闭 transport 后仍可用。

## 最小程序

```cpp
#include <dlt698/app.hpp>
#include <iostream>

int main() {
    dlt698::app::Server server;
    auto published = server.set({0x200F, 2, 0}, dlt698::model::UInt16{5000});
    if (!published) {
        std::cerr << published.error().context << '\n';
        return 1;
    }
    auto started = server.start_tcp("0.0.0.0", 6980);
    if (!started) {
        std::cerr << started.error().context << '\n';
        return 1;
    }
    std::cin.get();
    auto stopped = server.stop();
    return stopped ? 0 : 1;
}
```

这里设置的是电网频率 OAD `20 0F 02 00` 的协议原始值，`5000` 按标准倍率表示 `50.00 Hz`。`std::cin.get()` 只用于等待应用退出，I/O 在库的工作线程上运行。

先设置后启动、先启动后首次设置都支持。运行期间可从业务线程继续调用 `set`；客户端的下一次查询读取新值，已经开始的 GET 分块继续使用原查询快照。

## TCP 和串口场景

:::tip 调试助手连接后 5 秒断开
默认 TCP 模式主动发送 LINK 登录并等待应答，普通调试助手通常不会自动确认；默认 5000 毫秒事务超时后服务器关闭该连接。手动直接 GET 请显式选择 `local_preset`，仅关闭心跳不能取消初始登录。报文解析、完整地址配置、可编译程序和排错说明见 [TCP 手动报文调试](../transport/tcp-debug.md)。
:::

| 场景 | 启动方式 | 协议流程 |
| --- | --- | --- |
| 远程 TCP（默认） | `start_tcp(address, port)` | 每个接入连接自动发起 LINK 登录，等待应答默认 5 秒；登录完成后默认 5 秒心跳，随后应答公共 CONNECT |
| 本地 TCP | `start_tcp(address, port, ConnectionProfile::local_public)` | 不做远程登录，仍需 CONNECT |
| 显式本地预设 | `start_tcp(address, port, ConnectionProfile::local_preset)` | 跳过 LINK 与 CONNECT，调用方负责两端配置一致 |
| 本地串口（默认） | `start_serial("COM3", 9600)` | 9600/8E1，自动套串行链路，无远程登录和心跳，仍需 CONNECT |

这些是入口的默认场景策略，实际部署可选其他场景。TCP 接入方向和协议角色独立；本批高层入口提供协议服务器监听，其他拨号方向仍可使用[分层接口](session.md)。

成功启动 TCP 表示监听就绪，不代表已有客户端或关联完成。端口零由系统分配，使用 `local_port()` 查询。协议地址沿用底层示例默认 `{0}` 与 CA=0；真实接入应设置 `ServerOptions::protocol.server/client_address`。地址按线序保存，不能自动从通信地址属性推断。

TCP 持续接入独立会话，默认最多 16 个活动连接，全部共享设备数据。超限关闭新增连接；某个连接失败或关闭后释放名额，其他连接继续工作。Windows 高层入口使用独占地址绑定，已有监听占用端口时返回错误。

串口完整重载接受 `SerialOptions` 与 `SerialLinkOptions`。帧前四个 FE 与 33 位间隔由适配器处理，速率和字符位数根据实际字格式派生。当前整数时序模型不能准确表达 1.5 停止位，高层入口对此返回 `unsupported_service`。手动 RS-485 方向切换必须同时提供真实 `async_drain`；自动方向模式只有估算时序，真实硬件尚需现场验证。

## 数据发布与共享

`ServerOptions::traffic` 可观察每个连接的收发，回调额外接收与 `diagnostic` 相同的非零连接 ID；ID 在一次运行内唯一，重启重新编号。回调在工作线程借用事件与字节，不得阻塞，异常被隔离。完整示例、发送结果和生命周期约定见[报文调试](../guide/packet-debug.md#在应用中监控收发)。

默认标准路径使用现有目录校验精确类型、数组布局、长度和资源预算，未设置的属性不生成零值。三相电压整体是 `Array<UInt16>`，不能直接用标量代替；元素更新使用 `device()->set_element(...)`，元素索引从 1 开始。

`set` 是本地更新，发布的属性对远端保持只读；它不表示授予协议 SET 权限。运行中给同一 OI 设置属性 2、3 会自动扩充定义，失败不会改变已发布目录或旧值。使用 `Device::define` 可先声明未收录的厂家 OI 的普通只读属性，随后使用同一个 `set`；本批不支持覆盖标准定义、改类型、方法、记录或远端可写声明。

```cpp
auto device = std::make_shared<dlt698::service::Device>();
dlt698::app::Server tcp(device);
dlt698::app::Server serial(device);
```

一个服务器实例只能启用一种传输，重复启动返回 `busy`。需要同时提供 TCP/串口时使用多个服务器共享设备；停止一个服务器不影响另一个。数据保存在内存，跨连接和同实例重启保留，不提供磁盘持久化。

`DeviceOptions` 支持布局及总量预算，默认三相、4 费率、最高 21 次谐波；最大 256 个对象、4096 个属性及 16 MiB 已发布值的编码总量。单值仍受 `Limits` 的深度、节点、字节预算约束。自定义声明预占对象和属性名额，替换值按净字节变化计算；预算衡量当前存储，已交付给读取方的旧快照由读取方持有。

需要真实采集 provider、远端写入、ACTION 或记录后端时，继续使用[对象目录](object.md)和[服务分发](service.md)。[托管客户端](client.md)已提供直接连接和读写；高层 provider 接入、外部运行时和自动重拨将在后续阶段交付。

## 关闭、状态和诊断

`stop()` 幂等地停止监听、关闭全部会话和通道，并等候工作线程自然排空取消回调后退出；析构也会清理。实现不依赖固定睡眠，停止后可重新启动，数据保留。

`ServerState` 为 `stopped/starting/running/stopping`，`running` 描述传输入口而非协议关联。`connections()` 发布当前活动会话数，关闭观察器执行后释放名额。

`ServerOptions::diagnostic` 可接收连接标识和错误，标识零表示监听器或接入资源错误。正常运行期间回调在工作线程执行；异常被隔离，回调不能无限阻塞。回调内调用阻塞的 `stop()` 返回 `busy`，可用 `request_stop()` 请求异步停止，再由业务线程等待。回调内释放最后一个公开句柄由受管理的线程回收机制处理，不 self-join，也不 detach 工作线程。

外部线程不得在析构对象的同时继续调用其成员。普通接口保持现有 Result/DAR 语义，不因入口变简单而隐藏类型错误或远端原码。

## 示例与验证

`cpp/examples/app/server.cpp` 构建为 `dlt698_server`：无参数监听 `0.0.0.0:6980`，传串口名则开启本地串口，按 Enter 停止。它提供频率 `200F`，与新 `dlt698_client` 配对，见[托管客户端](client.md)。旧 `dlt698_tcp_client` 固定查询旧模拟目录的 `2000`，配对使用时仍运行原 `dlt698_tcp_server`。

```sh
dlt698_server
dlt698_server COM3
ctest --test-dir build -C Release -R '^(device|app)$' --output-on-failure
```

`app` 和 `transport_tcp` 均默认参与 CTest，带 `network` 标签。软件覆盖类型/预算/并发、多个 TCP 会话、默认 LINK、预设关联、同 OI 扩充、共享设备、端口占用、停止重启及回调内销毁。transport 统一启用 Asio 线程支持，修复了 MinGW 静态链接中因包含顺序不同而混用有线程/无线程类型的崩溃。串口打开失败和 hooks 配置错误已有测试；成功的真实串口、RS-485 物理时序和独立设备互操作仍待设备验收。
