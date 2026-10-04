---
title: Client/ServerService
description: 客户机服务的单项与列表接口，以及服务器侧的对象分发接入。
---

# Client/ServerService

头文件：`<dlt698/service/service.hpp>`，命名空间 `dlt698::service`。

## ClientService

```cpp
class ClientService {
  public:
    explicit ClientService(std::shared_ptr<session::Session> session);

    void async_get(model::Oad attribute, std::function<void(Result<ObjectValue>)> handler);
    void async_set(model::Oad attribute, model::Data value,
                   std::function<void(Result<std::uint8_t>)> handler);
    void async_action(model::Omd method, model::Data parameter,
                      std::function<void(Result<ActionValue>)> handler);

    void async_get_list(std::vector<model::Oad> attributes, session::Session::GetHandler handler);
    void async_set_list(std::vector<protocol::apdu::SetAttribute> attributes,
                        session::Session::SetHandler handler);
    void async_action_list(std::vector<protocol::apdu::ActionMethod> methods,
                           session::Session::ActionHandler handler);

    void async_get_record(protocol::apdu::GetRecord record,
                          std::function<void(Result<protocol::apdu::RecordResult>)> handler);
    void async_get_record_list(std::vector<protocol::apdu::GetRecord> records,
                               session::Session::RecordHandler handler);
};
```

构造时抛 `std::invalid_argument`：会话为空。**会话的连接和生命周期由应用管理**——`ClientService` 不会替你 `start()` 或 `close()`。

### 单项 vs 列表

单项接口返回 `ObjectValue`（`ObjectValue` 是 DAR 或 `Data` 的 variant）、`uint8_t` DAR 或 `ActionValue`，业务代码更直接。列表接口返回完整响应，逐项结果和原顺序都由你处理。

```cpp
dlt698::service::ClientService client(session);

client.async_get({0x4001, 2, 0}, [](dlt698::Result<dlt698::service::ObjectValue> result) {
    if (!result) return handle_local_error(result.error());
    if (auto* dar = std::get_if<std::uint8_t>(&result.value()))
        return handle_dar(unsigned(*dar));
    handle_value(std::get<dlt698::model::Data>(result.value()));
});

client.async_set({0x4001, 2, 0}, dlt698::model::UInt16{2413},
                 [](dlt698::Result<std::uint8_t> result) {
                     if (!result) return handle_local_error(result.error());
                     if (result.value() != 0) return handle_dar(result.value());
                     handle_ok();
                 });
```

### 列表的部分成功

:::warning 列表不回滚
`async_set_list` 和 `async_action_list` 按顺序**独立执行**每一项，部分成功**不会**回滚。响应里逐项的 DAR 才是结论，不要只看外层 `Result`。
:::

超时或取消只说明本地没有得到确定响应，**不能证明远端没有执行**。因此这两个接口都不会自动重试。

### 记录查询

`async_get_record` 自动收齐应用层分块，回调拿到的是完整结果。`async_get_record_list` 保持逐项 DAR 和原顺序。

## ServerService

```cpp
class ServerService {
  public:
    ServerService(std::shared_ptr<session::Session> session,
                  std::shared_ptr<ObjectRegistry> objects);
};
```

构造时把对象目录接入服务器会话的 GET/SET/ACTION 分发。两个参数都不能为空，否则抛 `std::invalid_argument`。

```cpp
auto objects = std::make_shared<dlt698::service::ObjectRegistry>();
/* 注册对象…… */

auto server_session = std::make_shared<dlt698::session::Session>(channel, executor, options);
dlt698::service::ServerService server(server_session, objects);
server_session->start();
```

:::warning 处理器不捕获自身地址
`ServerService` 内部会把自己的处理器注册到会话上。捕获 `ServerService` 的 `this` 或其 `shared_ptr` 会形成引用环，导致会话和目录都无法释放。实现自己的包装层时注意别把 `this` 传进 lambda。
:::

`ServerService` 持有目录的**共享所有权**，不捕获自身地址。目录和 provider 会话共享，因此 provider 的并发读取约定由你保证。

## 错误映射

| 情况 | `Result` 状态 | 详情 |
| --- | --- | --- |
| 本地会话错误 | 失败 | `error().code` 是 `timeout`/`closed`/`busy`/`not_associated` 等 |
| 远端业务拒绝 | **成功** | DAR 在 `ObjectValue` 的 `uint8_t`、`SetHandler` 的逐项结果或 `ActionValue::dar` |
| CONNECT 被拒绝 | **成功** | `ConnectResponse::result` 非零 |
| provider 抛异常 | 成功 | 目录转成 `DAR=255` |

## 选择同步还是异步

| 需求 | 选择 |
| --- | --- |
| 有自己的事件循环、想避免阻塞 | `ClientService` |
| 命令行工具、脚本化调用、测试 | [SyncClientService](./sync.md) |
| 设备侧提供数据 | `ServerService` + `ObjectRegistry` |

混用也可以——`SyncClientService` 和 `ClientService` 可以绑定同一个会话，只要注意[单在途事务](./session.md#事务行为表)的限制。
