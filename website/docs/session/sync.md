---
title: 同步客户机
description: SyncClientService 的驱动约定、超时归属和死锁防护。
---

# 同步客户机

头文件：`<dlt698/service/sync.hpp>`，命名空间 `dlt698::service`。

## 基本用法

```cpp
class SyncClientService {
  public:
    using Drive = std::function<void(std::chrono::milliseconds)>;

    explicit SyncClientService(std::shared_ptr<session::Session> session, Drive drive = {});
    ~SyncClientService();

    Result<protocol::apdu::ConnectResponse> connect();
    Result<ObjectValue> get(model::Oad attribute);
    Result<protocol::apdu::GetResponse> get_list(std::vector<model::Oad> attributes);
    Result<protocol::apdu::RecordResult> get_record(protocol::apdu::GetRecord record);
    Result<protocol::apdu::GetRecordResponse> get_record_list(std::vector<protocol::apdu::GetRecord> records);

    Result<std::uint8_t> set(model::Oad attribute, model::Data value);
    Result<protocol::apdu::SetResponse> set_list(std::vector<protocol::apdu::SetAttribute> attributes);

    Result<ActionValue> action(model::Omd method, model::Data parameter);
    Result<protocol::apdu::ActionResponse> action_list(std::vector<protocol::apdu::ActionMethod> methods);

    Result<void> release();
};
```

同步适配在统一的异步核心上等待，**不创建线程**。

## 两种驱动方式

`Drive` 是 `std::function<void(std::chrono::milliseconds)>`，两种模式二选一。

### 显式驱动

传入 drive 回调，调用线程反复执行它来推进事件循环：

```cpp
auto runtime = std::make_shared<dlt698::transport::IoRuntime>();
auto executor = runtime->executor();
auto channel = dlt698::transport::TcpChannel::connect(
    runtime, "127.0.0.1", 4059,
    [&](dlt698::Result<void> result) { /* 连接成功后创建并启动 Session */ });
/* ... */
auto session = std::make_shared<dlt698::session::Session>(channel, executor);
session->start();

dlt698::service::SyncClientService client(
    session, [runtime](auto elapsed) { runtime->run_for(elapsed); });

auto connect = client.connect();
```

或者在测试里用虚拟时钟：

```cpp
auto executor = std::make_shared<dlt698::ManualExecutor>();
dlt698::service::SyncClientService client(
    session, [executor](auto elapsed) { executor->advance(elapsed); });
```

:::warning 每次回调不得无限阻塞
drive 回调会被反复调用。如果它自己长时间阻塞，事务超时就不会触发，请求会一直等下去。`run_for` / `advance` 传一个有限时长是正确的做法。
:::

显式驱动的优势是**虚拟时钟也能产生超时**——`ManualExecutor::advance` 推进虚拟时间，`Session` 的 `request_timeout` 照样生效。

### 外部线程驱动

不传 drive，此时**必须**由其他线程持续驱动 `IoRuntime::run()`：

```cpp
std::thread runner([runtime] { runtime->run(); });

auto session = std::make_shared<dlt698::session::Session>(channel, executor);
session->start();
dlt698::service::SyncClientService client(session);   // 无 drive

auto connect = client.connect();
```

这是命令行长驻进程的典型模式。线程结束时机见[TCP 通道](../transport/tcp.md)的退出顺序说明。

## 死锁防护

同步等待最危险的错误是在事件循环回调里再同步等待——那会让执行器永远等不到自己的响应。

库对此有两道防护：

| 场景 | 返回 |
| --- | --- |
| 同一适配器上的第二个并发调用 | `busy` |
| 在事件循环回调中调用同步方法 | `busy` |

`Session::in_executor_thread()` 可以让你自己提前判断：

```cpp
if (session->in_executor_thread()) {
    // 当前就在执行器回调里，不能同步等待
    return;
}
```

## 错误归属

```cpp
auto value = client.get({0x2000, 2, 0});
if (!value) {
    // 本地错误：超时、关闭、busy、通道错误……
    handle_local(value.error());
} else if (auto* dar = std::get_if<std::uint8_t>(&value.value())) {
    // 远端业务拒绝：DAR
    handle_dar(unsigned(*dar));
} else {
    // 成功：精确类型 Data
    handle_value(std::get<dlt698::model::Data>(value.value()));
}
```

- **超时由 `Session` 管理**，不是同步适配器。适配器只是等待 `Session` 的回调。
- **应用必须保持运行时进展**。不传 drive 且没有外部线程时，同步调用会一直等下去。
- **销毁适配器前先结束所有同步调用**。`~SyncClientService` 释放等待状态，仍在进行的调用会拿到不确定的结果。

## drive 抛异常

drive 抛异常时返回 `io_error` 并投递取消。后续的完成状态由共享对象持有，**不会引用已经返回的栈帧**。

## 不重试

`set`、`action` 及其列表版本**不自动重试**。超时不能证明远端未执行，重复调用有副作用。需要幂等保证请自己在应用层处理。

## 完整示例

`cpp/examples/memory_mutation.cpp` 演示了完整的同步读写/方法与串行链路适配：内存通道两端都经过 FE 和 33 位间隔处理，完成同步 CONNECT → SET → ACTION → GET → RELEASE，输出 `SET DAR=0 ACTION DAR=0 GET UInt16=42`。代码见[第一个程序](../getting-started/quick-start.md)。
