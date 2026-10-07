---
title: TCP 通道
description: IoRuntime、TcpChannel 与 TcpListener。
---

# TCP 通道

头文件：`<dlt698/transport/tcp.hpp>`，命名空间 `dlt698::transport`。仅在 `DLT698_BUILD_TRANSPORT=ON` 时提供。

:::tip 使用网络或串口调试助手连接 TCP
默认示例会主动发送 LINK 登录，未收到合法应答约 5 秒后关闭该连接。TCP 已连接不代表应用关联完成；手动直接 GET 应显式配置 `local_preset`。完整报文、可编译程序及排错步骤见 [TCP 手动报文调试](./tcp-debug.md)。
:::

## IoRuntime

```cpp
class IoRuntime {
  public:
    IoRuntime();
    ~IoRuntime();

    void run();                                              ///< 持续处理 I/O 事件
    void run_for(std::chrono::milliseconds duration);        ///< 处理事件直到时限到达
    void stop();                                             ///< 请求事件循环停止
    void restart();                                          ///< 清除停止状态

    std::shared_ptr<IExecutor> executor();
};
```

:::danger 没有隐藏线程
`IoRuntime` **不创建任何内部工作线程**。应用负责调用 `run()` 或 `run_for()`，并管理线程结束。

`~IoRuntime()` 停止运行时，调用方**必须事先结束所有外部运行线程**。
:::

- `run()` 在当前线程处理 I/O 事件直到运行时停止。工作守卫让空闲运行时继续等待。
- `run_for(duration)` 的 `duration` 是**本次驱动事件循环的时间预算**，不是单个操作的超时。正在执行的用户回调不会被时限中断。
- `stop()` **不关闭通道**。未处理的完成回调需要 `restart()` 后继续驱动才能执行。
- `restart()` 的前置条件是所有 `run`/`run_for` 调用均已返回。

`executor()` 返回独立 strand 上的会话执行器和真实单调计时器，须由**同一 runtime** 驱动，**可以**从不同线程投递。执行器会保持运行时内部上下文存活。

## TcpChannel

```cpp
class TcpChannel final : public IChannel {
  public:
    using ConnectHandler = std::function<void(Result<void>)>;

    static std::shared_ptr<TcpChannel> connect(
        std::shared_ptr<IoRuntime> runtime, std::string host, std::uint16_t port,
        ConnectHandler handler, ChannelOptions options = {});
};
```

```cpp
#include <dlt698/transport/tcp.hpp>

auto runtime = std::make_shared<dlt698::transport::IoRuntime>();
auto executor = runtime->executor();
auto channel = dlt698::transport::TcpChannel::connect(
    runtime, "127.0.0.1", 4059,
    [](dlt698::Result<void> result) { /* 连接成功后创建并启动 Session */ });
```

`connect` 立即返回通道对象，此时连接可能尚未完成。`host` 可以是 IP 地址或待解析的主机名。运行时为空或通道配置非法时抛 `std::invalid_argument`。

连接成功后再创建 `Session` 并 `start()`。

## TcpListener

```cpp
class TcpListener {
  public:
    using AcceptHandler = std::function<void(Result<std::shared_ptr<TcpChannel>>)>;

    static Result<std::shared_ptr<TcpListener>> listen(
        std::shared_ptr<IoRuntime> runtime, const std::string& bind_address,
        std::uint16_t port, ChannelOptions options = {});

    std::uint16_t local_port() const noexcept;
    void async_accept(AcceptHandler handler);
    void close();
};
```

- `listen` **同步**绑定本地地址并开始监听。`bind_address` 必须是数字地址，**不执行主机名解析**。
- `port` 为零表示由系统分配，可用 `local_port()` 查询实际端口。
- 失败返回错误（运行时为空、地址解析失败、socket 操作失败）；运行时非空但通道配置非法时抛 `std::invalid_argument`。
- `async_accept` 最多一个在途接受操作，第二个返回 `busy`。
- `close()` **不关闭**此前已接受的通道，应用须继续驱动运行时以处理完成回调。

## 协议角色与拨号方向独立

这是最容易搞混的一点。`TcpListener` 接受 TCP 连接**不限定**协议角色，谁是客户机由 `SessionOptions::role` 决定：

| 场景 | 拨号方 | `role` 配置 |
| --- | --- | --- |
| 集中器主动连终端 | 集中器 | 集中器 `client`，终端 `server` |
| 终端反向连接集中器 | 终端 | 终端 `client`，集中器 `server` |

两种拨号方向都测试了 LINK、CONNECT、GET/列表 GET、同步 SET/ACTION、RELEASE。

## 预算与超时

- 一个通道只允许**一个在途读**，第二个返回 `busy`。
- 写操作保序并写完全部字节后完成。字节与条数预算涵盖已投递但尚未进入 strand 的写操作。
- **底层通道不会自动重试、重放，也不会设置 DNS/建连/读写超时。**
- `Session` 的 `request_timeout` **不等同于**建连超时——它只覆盖事务等待。

## 退出顺序

彻底退出时按这个顺序：

1. 关闭所有通道和监听器。
2. 驱动运行时，处理所有挂起的完成回调。
3. `stop()` 停止事件循环。
4. 结束调用 `run()` 的外部线程。
5. 销毁 `IoRuntime`。

停止运行时不会关闭通道，未处理的回调须 `restart()`/`run` 才可能继续完成。销毁 runtime 前必须结束调用 `run` 的外部线程。

回调抛出的异常会被隔离，应由应用记录。**不要在回调中阻塞等待同一 runtime 的结果**——那会死锁。

## 多线程泵送

多个线程可以同时泵送同一个 runtime：

- 每个通道/监听器及会话执行器**分别**串行处理自身状态（strand 保证）。
- **不同实例的回调可以并发**。如果多个回调共享可变状态，那个状态要自己加锁。

回调在运行时线程执行，应用须持有通道至操作完成——内部异步操作不会永久保活通道。
