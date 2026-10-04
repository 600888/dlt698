---
title: 通道与内存通道
description: IChannel 接口、ChannelOptions 与 MemoryChannel。
---

# 通道与内存通道

## IChannel

头文件：`<dlt698/transport/channel.hpp>`，命名空间 `dlt698::transport`。

```cpp
class IChannel {
  public:
    using ReadHandler = std::function<void(Result<Bytes>)>;   ///< 成功值拥有缓冲区
    using WriteHandler = std::function<void(Result<void>)>;

    virtual ~IChannel() = default;

    virtual void async_read(ReadHandler handler) = 0;
    virtual void async_write(Bytes bytes, WriteHandler handler) = 0;
    virtual void close() = 0;
};
```

## 通用语义

这些约定对所有通道实现成立（`TcpChannel`、`SerialChannel`、`MemoryChannel`、`SerialLinkChannel`）：

| 行为 | 说明 |
| --- | --- |
| 单在途读 | 每个通道最多一个 `async_read`，第二个返回 `busy` |
| 读块边界任意 | 一次读可能是半帧，也可能包含多帧，须自行流解析 |
| 写保序全量 | 写操作按顺序执行，写完全部字节才完成 |
| 预算检查在投递前 | 字节和条数预算包含已投递但尚未进入 strand 的操作 |
| 关闭幂等 | `close()` 可重复调用，挂起操作以错误完成 |
| 关闭后仍需驱动 | 异步实现需要继续驱动运行时，才能处理关闭和完成回调 |

:::warning 写完成 ≠ 远端处理完成
`async_write` 成功表示全部字节已经交给底层驱动或对端缓存，**不代表**远端已经处理。内存通道写入成功表示整块已进入对端缓存；串口写完成表示全部交给驱动，尚不能证明物理线路排空。
:::

## ChannelOptions

```cpp
struct ChannelOptions {
    std::size_t read_chunk_bytes = 4096;                ///< 单次读取缓冲区大小
    std::size_t max_pending_write_bytes = 1024 * 1024;  ///< 所有未完成写操作的总字节上限
    std::size_t max_pending_writes = 128;               ///< 所有未完成写操作的条数上限
};
```

`read_chunk_bytes` 范围是 1 至 1 MiB。两个写入上限都必须**非零**。

条数预算里**空写也占一条**。

## MemoryChannel

头文件：`<dlt698/transport/memory.hpp>`。

```cpp
struct MemoryOptions {
    std::size_t read_chunk_bytes = 4096;
    std::size_t max_buffer_bytes = 1024 * 1024;   ///< 每端缓存上限
    std::size_t max_pending_writes = 128;
};

class MemoryChannel final : public IChannel {
  public:
    static std::pair<std::shared_ptr<MemoryChannel>, std::shared_ptr<MemoryChannel>>
    pair(std::shared_ptr<IExecutor> executor, MemoryOptions options = {});
};
```

`pair` 返回一对已连接的通道，**共享同一个执行器**，可以分别绑定协议客户机和服务器。执行器为空或配置非法（各项非零）时抛 `std::invalid_argument`。

```cpp
auto executor = std::make_shared<dlt698::ManualExecutor>();
auto [client_side, server_side] = dlt698::transport::MemoryChannel::pair(executor);

auto client = std::make_shared<dlt698::session::Session>(client_side, executor);
auto server = std::make_shared<dlt698::session::Session>(server_side, executor, server_options);
client->start();
server->start();
```

### 预算语义

`MemoryOptions` 限制读取分块、每端缓存与排队写字节、排队写条数。**预算不足返回 `resource_limit`，不执行部分写入**——不会出现"写了一半"的中间状态。

### 销毁顺序

`~MemoryChannel()` 关闭本端并唤醒双方尚未完成的读取。关闭、销毁与异步完成**仍需驱动执行器**，否则挂起的回调不会执行。

## 自己实现通道

如果要接入其他传输方式（比如 UDP 封装、进程管道），实现 `IChannel` 即可：

- `async_read` 必须保证最多一个在途读，第二次返回 `busy`。
- `async_write` 必须保序，并接管传入的 `Bytes`。
- 预算不足时返回 `resource_limit`，不要部分写入。
- 回调抛出的异常应被隔离并记录。
- `close()` 幂等，且挂起操作要以错误完成。
- 应用须持有通道至操作完成——内部异步操作不会永久保活通道。

只要同时提供一个 `IExecutor` 实现，整个会话层就能直接复用。
