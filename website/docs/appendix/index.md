---
title: 附录
description: 错误码参考、协议覆盖范围与示例程序。
comments: false
---

# 附录

## 这一部分的页面

| 页面 | 内容 |
| --- | --- |
| [错误码参考](./error-codes.md) | `ErrorCode` 全部取值及其触发条件 |
| [协议覆盖范围](./coverage.md) | 已实现能力、限制、待办事项和验证状态 |
| [示例程序](./examples.md) | 仓库附带示例的用途和运行方式 |

## 联调前的必读

在真实设备上对接之前，至少确认这几点：

1. **查过覆盖范围。** [协议覆盖范围](./coverage.md)列出了哪些分支已实现、哪些会返回 `unsupported_service`。
2. **理解超时和取消会关闭通道。** 线上没有 generation 字段，库无法区分迟到响应，所以选择断开。详见 [Session 事务行为表](../session/session.md#事务行为表)。
3. **不把外层 `Result` 当业务结论。** CONNECT 被拒绝、GET 部分成功都是 `Result` 成功但业务失败。详见 [Result 与错误](../core/result.md#业务成功不能只看外层-result)。
4. **RS-485 排空要接真实驱动。** 只有时间估算时不能保证硬件时序。详见[串口与串行链路](../transport/serial.md#rs-485-方向控制)。
5. **保持执行器进展。** 没有驱动 `IoRuntime`，所有回调都不会执行。详见 [TCP 通道退出顺序](../transport/tcp.md#退出顺序)。

## 文档与源码的关系

本文档描述的是公开头文件的接口。行为细节以 `cpp/include` 下的中文 Doxygen 注释为准，两者不一致时以头文件为准，并请提交修正。
