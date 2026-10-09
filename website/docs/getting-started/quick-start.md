---
title: 第一个程序
description: 用托管服务器直接发布数据，也可通过内存示例了解分层接口。
---

# 第一个程序

普通服务端使用 `<dlt698/app.hpp>` 和 `dlt698::dlt698`：创建服务器、设置数据、启动即可，库管理监听、连接、会话和运行线程。

```cpp
#include <dlt698/app.hpp>
#include <iostream>

int main() {
    dlt698::app::Server server;
    auto value = server.set({0x200F, 2, 0}, dlt698::model::UInt16{5000});
    if (!value) {
        std::cerr << value.error().context << '\n';
        return 1;
    }
    auto started = server.start_tcp("0.0.0.0", 6980);
    if (!started) {
        std::cerr << started.error().context << '\n';
        return 1;
    }
    std::cin.get();
    return server.stop() ? 0 : 1;
}
```

`200F/属性2/索引0` 是频率原始值，`5000` 表示 `50.00 Hz`。等待 Enter 只用于让应用保持运行，不需要手动驱动事件循环。串口把启动行换成 `server.start_serial("COM3", 9600)`；运行中继续 `server.set(...)` 即可更新或首次发布其他属性。

```cmake
find_package(dlt698 1.0 CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE dlt698::dlt698)
```

构建后运行 `dlt698_server`，或 `dlt698_server COM3`。默认 TCP 会自动 LINK 登录并等待客户端 CONNECT；本地和预设关联、共享设备、布局/预算及退出约定见[托管服务器与设备数据](../session/server.md)。

:::tip 手动发送 GET 前先选择关联模式
使用网络/串口调试助手的 TCP 模式连接默认示例，会收到 LINK 登录请求；未回复时约 5 秒后断开。直接手动 GET 请按 [TCP 手动报文调试](../transport/tcp-debug.md)配置 `local_preset` 和匹配的 SA/CA；只建立 TCP 或只关闭心跳都不能完成协议关联。
:::

## 客户端：连接后直接读取

```cpp
dlt698::app::Client client;
auto connected = client.connect_tcp("127.0.0.1", 6980);
// 先检查 connected，再读取；value 保留 Data/DAR，仍需检查业务结果。
auto value = client.get({0x200F, 2, 0});
```

客户端也不需要手动驱动 I/O。完整错误处理示例为 `cpp/examples/app/client.cpp`；在另一终端运行 `dlt698_client`，读取频率后自动断开。串口使用 `client.open_serial("COM4", 9600)`，完整契约见[托管客户端](../session/client.md)。

## 分层接口：内存中的完整会话

这个程序不需要网络和真实设备：两端都跑在内存通道上，客户机用[同步客户机](../session/sync.md)在同一个线程里驱动虚拟时钟。完整源码见 `cpp/examples/service/memory_mutation.cpp`。

```cpp
#include <dlt698/service/sync.hpp>
#include <dlt698/transport/memory.hpp>
#include <dlt698/transport/serial_link.hpp>
#include <iostream>

int main() {
    using namespace dlt698;
    auto executor = std::make_shared<ManualExecutor>();
    auto raw = transport::MemoryChannel::pair(executor);
    // 在内存中同样经过 FE 和串行间隔适配，真实设备可把 raw 替换为 SerialChannel。
    auto master_link = transport::SerialLinkChannel::wrap(raw.first, executor);
    auto terminal_link = transport::SerialLinkChannel::wrap(raw.second, executor);

    session::SessionOptions options;
    options.role = session::Role::server;
    auto master = std::make_shared<session::Session>(master_link, executor);
    auto terminal = std::make_shared<session::Session>(terminal_link, executor, options);

    auto objects = std::make_shared<service::ObjectRegistry>();
    auto object = std::make_shared<service::MemoryObject>();
    object->set(2, model::UInt16{10});
    object->bind_method(1,
                        [weak = std::weak_ptr<service::MemoryObject>(object)](
                            const auto&, const model::Data& parameter) -> service::ActionValue {
                            weak.lock()->set(2, parameter);
                            return {0, parameter};
                        });
    if (!objects->register_object({0x2000,
                                   "可写值及模拟方法",
                                   {{2, model::DataType::uint16, true, true}},
                                   {{1, model::DataType::uint16, model::DataType::uint16, true}}},
                                  object))
        return 1;

    service::ServerService server(terminal, objects);
    service::SyncClientService client(master,
                                      [executor](auto elapsed) { executor->advance(elapsed); });
    master->start();
    terminal->start();

    auto connect = client.connect();
    if (!connect || connect.value().result) return 2;

    auto set = client.set({0x2000, 2, 0}, model::UInt16{25});
    if (!set || set.value()) return 3;

    auto action = client.action({0x2000, 1, 0}, model::UInt16{42});
    if (!action || action.value().dar) return 4;

    auto value = client.get({0x2000, 2, 0});
    if (!value || !std::holds_alternative<model::Data>(value.value())) return 5;

    std::cout << "SET DAR=" << unsigned(set.value())
              << " ACTION DAR=" << unsigned(action.value().dar)
              << " GET UInt16=" << std::get<model::Data>(value.value()).as<model::UInt16>().value
              << '\n';

    auto released = client.release();
    master->close();
    terminal->close();
    executor->run_ready();
    return released ? 0 : 6;
}
```

构建后运行 `dlt698_memory_mutation`，输出：

```text
SET DAR=0 ACTION DAR=0 GET UInt16=42
```

## 逐段说明

**执行器与通道。** `ManualExecutor` 是虚拟时钟执行器，只允许单线程使用。`MemoryChannel::pair` 返回一对已连接的通道，共享同一个执行器。真实设备上把 `raw.first` 换成 [TCP 通道](../transport/tcp.md)或[串口通道](../transport/serial.md)即可，其余代码不变。

**协议角色与拨号方向无关。** 这里只给 `terminal` 设置了 `Role::server`。谁主动发起 TCP 连接是传输层的事，与协议角色独立配置。

**对象目录。** `register_object` 的第一个参数是 schema：属性编号 2、类型 `uint16`、可读可写；方法编号 1，参数和返回类型都是 `uint16`。属性 `writable` 默认 `false`，必须显式打开。详见[对象目录与 Provider](../session/object.md)。

**方法回调不能捕获 shared_ptr。** 目录持有 provider，provider 又被方法回调捕获会形成引用环。示例用 `std::weak_ptr` 打破环，并在调用时 `lock()`。

**同步适配需要驱动函数。** 第二个参数是 drive 回调，库会在每次等待时反复调用它来推进虚拟时钟，这样超时也能正常触发。不传 drive 时必须有其他线程持续驱动 `IoRuntime::run()`。在执行器回调里调用同步接口会直接返回 `busy`，避免死锁。

**`ObjectValue` 是 variant。** `get` 返回的要么是 DAR（`uint8_t`），要么是精确类型 `Data`，必须先 `std::holds_alternative` 判断再取值。

## 下一步

- 需要读真实设备：看[串口与串行链路](../transport/serial.md)和 [TCP 通道](../transport/tcp.md)。
- 需要处理分块、记录或 FollowReport：看 [GET 与记录查询](../protocol/get.md)。
- 想理解报错来源：看 [Result 与错误](../core/result.md)和[错误码参考](../appendix/error-codes.md)。
