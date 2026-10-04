---
title: 会话与服务
description: Session 事务管理、对象目录与 Provider、Client/ServerService 和同步客户机。
comments: false
---

# 会话与服务

`dlt698::session` 和 `dlt698::service` 把协议层组装成可用的通信能力：连接管理、事务匹配、对象读写与方法分发。

## 分层关系

```text
service::SyncClientService      同步等待适配（不创建线程）
        │
service::ClientService         GET/SET/ACTION 客户机
service::ServerService         服务器侧对象分发
        │
session::Session               连接、事务匹配、超时、取消、释放
        │
transport::IChannel           字节流（内存 / TCP / 串口）
```

## 这一部分的页面

| 页面 | 内容 |
| --- | --- |
| [Session 会话](./session.md) | `SessionOptions`、事务语义、状态机、生命周期约定 |
| [对象目录与 Provider](./object.md) | `ObjectSchema`、`ObjectRegistry`、`IObjectProvider`、`MemoryObject` |
| [Client/ServerService](./service.md) | 客户机服务与服务器侧接入 |
| [同步客户机](./sync.md) | `SyncClientService` 的驱动约定与死锁防护 |

## 三个必须记住的约定

**协议角色与拨号方向独立。** `SessionOptions::role` 决定谁是协议客户机，谁是协议服务器；TCP 由谁主动连接是传输层的事。集中器主动拨号连终端时，拨号方仍是协议客户机。

**单在途事务。** 一个 `Session` 同时只允许一个请求。第二个请求直接返回 `busy`，不进入队列。需要并发就创建多个 `Session`。

**回调在执行器内串行执行。** 所有处理器和完成回调都在 `IExecutor` 上运行。处理器内部**绝不能阻塞等待同一执行器**——那会让会话死锁。需要同步语义就用[同步客户机](./sync.md)，它会检测这种场景并返回 `busy`。
