---
title: Session 会话
description: SessionOptions、事务行为表、状态机和生命周期约定。
---

# Session 会话

头文件：`<dlt698/session/session.hpp>`，命名空间 `dlt698::session`。

## 创建

```cpp
class Session {
  public:
    Session(std::shared_ptr<transport::IChannel> channel,
            std::shared_ptr<IExecutor> executor,
            SessionOptions options = {});
    ~Session();

    void start();
    State state() const noexcept;
    void close();
    void cancel();
    bool in_executor_thread() const noexcept;
};
```

三个参数都是必需的：

- `channel`：**已连接**的字节通道。`Session` 不负责建连。
- `executor`：所有会话操作和定时器共用的**串行**执行器。
- `options`：角色、精确地址、能力和超时配置。

构造时抛 `std::invalid_argument`：通道或执行器为空、地址不是单地址、限制配置非法。

`Session` 对象必须由应用持有——排队任务**不会**永久保活它。析构或 `close()` 后仍需驱动执行器，才能收到 `closed` 状态的完成回调。

## 状态机

```cpp
enum class Role { client, server };

enum class State {
    disconnected, preconnected, associating, associated, releasing, closed
};
```

```text
disconnected ──start()──▶ preconnected ──async_connect()──▶ associating ──▶ associated
                                        │                                          │
                                        └──preset_association───────────────────────┘
                                                                            │
                                              async_release() ──▶ releasing ──▶ disconnected
```

`state()` 读取**原子发布**的状态：刚投递但尚未执行的操作不会立即反映出来。

## SessionOptions

```cpp
struct SessionOptions {
    Role role = Role::client;
    protocol::link::ServerAddress server;
    std::uint8_t client_address = 0;
    Limits limits;

    protocol::apdu::AssociationParameters parameters{
        0x0010, {0xf3, 0x8c, 0x08}, {}, 1024, 1024, 1, 1024, 100};
    protocol::apdu::FactoryVersion factory;

    std::chrono::milliseconds request_timeout{5000};
    std::chrono::milliseconds id_reuse_delay{120000};

    bool require_login = false;
    bool preset_association = false;
    bool clock_trusted = false;
    std::uint16_t heartbeat_seconds = 0;
    std::optional<model::Ti> request_time_tag;

    std::chrono::milliseconds fragment_timeout{1000};
    unsigned fragment_retries = 2;
    bool prefer_get_blocks = true;

    std::function<model::DateTime()> calendar_clock;
};
```

| 字段 | 说明 |
| --- | --- |
| `role` | 协议角色，与 TCP 拨号方向独立 |
| `server` | 只接受**精确单地址**，会校验 SA、CA、DIR、PRM 和功能码 |
| `request_timeout` | 默认 5 秒 |
| `id_reuse_delay` | 默认 120 秒，须配置为覆盖对端最大响应寿命且不短于请求超时 |
| `require_login` | 要求先由协议服务器调用 `async_link(login, ...)` 完成预连接 |
| `preset_association` | 显式跳过 CONNECT，按本地配置进入应用连接 |
| `heartbeat_seconds` | 服务器登录成功后自动心跳周期，零为关闭；失败关闭通道 |
| `request_time_tag` | 客户机请求自动添加时间标签，响应须原样回传 |
| `fragment_timeout` / `fragment_retries` | 单片确认超时与重发次数 |
| `prefer_get_blocks` | 超长 GET 优先按完整属性/记录行应用分块 |
| `calendar_clock` | 空时使用 UTC；注入的时钟在执行器中调用 |

:::warning `preset_association` 不是"已认证"
`preset_association = true` 显式跳过 CONNECT，直接按本地配置进入应用连接。它**不表示远端经过任何认证**，两端配置的一致性完全由调用方负责。适用于本地虚拟通道或已由外部机制完成认证的场景。
:::

`calendar_clock` 抛异常会以 `invalid_value` 关闭会话，不会发送伪造时间。

## 协议能力

`Session` 当前只开放**公共认证**（NullSecurity）。其他认证机制仅支持报文编解码，连接请求会被拒绝。

默认声明应用连接 + GET/SET/ACTION 普通及列表能力：默认协议位图前两字节为 `E1 8C`，功能位图为零，窗口固定为 1。`SessionOptions::parameters` 可以缩减能力，协商时取**交集**。

## 客户机操作

```cpp
void async_connect(ConnectHandler handler);
void async_get(std::vector<model::Oad> attributes, bool list, GetHandler handler);
void async_set(std::vector<protocol::apdu::SetAttribute> attributes, bool list, SetHandler handler);
void async_action(std::vector<protocol::apdu::ActionMethod> methods, bool list, ActionHandler handler);
void async_get_record(std::vector<protocol::apdu::GetRecord> records, bool list, RecordHandler handler);
void async_release(ReleaseHandler handler);
```

`async_get` 在已关联状态下发起 Normal 或 NormalList，会检查响应分支、数量以及每个 OAD 的**原顺序**，并自动收齐应用层分块。

`async_set` 设置属性与精确 `Data`，响应保持逐项 DAR。

`async_action` 调用完整 OMD 与参数 `Data`，检查模式字节、列表顺序和响应分支。

`async_release` 先取消在途事务，再释放应用连接。成功后物理通道可以再次 CONNECT。

## 服务器操作

```cpp
void async_link(protocol::apdu::LinkRequestType type, std::uint16_t heartbeat_seconds,
                LinkHandler handler);

void set_request_handler(RequestHandler handler);
void set_record_handler(RecordRequestHandler handler);
void set_set_handler(SetRequestHandler handler);
void set_action_handler(ActionRequestHandler handler);
void set_diagnostic_handler(DiagnosticHandler handler);
```

**先注册处理器，再 `start()`。** 顺序反了会漏掉早期请求。

服务器处理器是**同步调用**的，签名返回响应而不是 `void`：

```cpp
using RequestHandler =
    std::function<protocol::apdu::GetResponse(const protocol::apdu::GetRequest&)>;
using SetRequestHandler =
    std::function<protocol::apdu::SetResponse(const protocol::apdu::SetRequest&)>;
using ActionRequestHandler =
    std::function<protocol::apdu::ActionResponse(const protocol::apdu::ActionRequest&)>;
```

未注册 GET 处理器时返回 `DAR=4`；未注册 SET/ACTION 处理器时**逐项**返回拒绝 `DAR=3`。

`async_link` 支持登录、单次心跳和退出登录。`heartbeat_seconds` 只是线上声明，自动周期由 `SessionOptions::heartbeat_seconds` 控制。

`set_diagnostic_handler` 注册的诊断回调**不会抢占**正确的在途事务，用于接收损坏帧、不匹配地址/方向、迟到响应等信息。

## 事务行为表

| 操作/情况 | 当前行为 |
| --- | --- |
| `async_get(attributes, list, handler)` | 已关联客户机发起 Normal/NormalList；检查响应分支、数量及每个 OAD 的原顺序 |
| `async_set(attributes, list, handler)` | 设置属性与精确 Data，响应保持逐项 DAR；不自动回滚或重放 |
| `async_action(methods, list, handler)` | 调用完整 OMD 与参数 Data，检查模式字节、列表顺序与响应分支 |
| 第二个在途请求 | 返回 `busy`，不进入请求队列 |
| 错地址、方向、服务、PIID 或 OAD/重复响应 | 进入诊断回调，不抢占正确事务 |
| 请求超时或 `cancel()` | 结束事务并**关闭物理通道**，隔离迟到响应 |
| 成功完成或被释放打断 | PIID 进入 `id_reuse_delay` 隔离期；64 个序号暂时不可用时返回 `resource_limit` |
| `async_release()` | 先取消在途事务，再释放应用连接；成功后物理通道可再次 CONNECT |
| 协商空闲时限到期 | 退出应用连接；服务器发送 RELEASE-Notification |
| `close()`/析构 | 关闭通道及计时器，未完成请求以 `closed` 完成；仍须驱动执行器交付回调 |

:::danger 超时和取消会关闭物理通道
线上没有 generation 字段，无法把迟到响应和后续事务区分开。库的选择是直接关闭通道——这是"宁可断开也不串扰"的取舍。你的应用需要能承受连接中断。
:::

## 生命周期与线程

- `Session` 对象需要由应用持有，排队任务不会永久保活它。
- 正常退出时保留执行器并继续驱动，直到挂起回调完成，然后才结束运行线程和运行时。
- 公共请求和完成回调都在会话执行器内处理，用户回调抛出的异常会被隔离。
- 服务器的同步处理器和 provider **不得**阻塞等待同一执行器。
- `in_executor_thread()` 返回 `true` 时，禁止同步等待同一环境。

线上没有 generation 字段，所以 PIID 隔离靠时间窗：`id_reuse_delay` 之后才复用序号，64 个序号同时被占用时返回 `resource_limit`。
