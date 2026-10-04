---
title: 核心类型
description: Result 错误模型、字节缓冲、精确类型 Data 与 A-XDR 编解码。
comments: false
---

# 核心类型

`dlt698::core` 提供不依赖任何传输手段的编解码能力：不需要 socket、不需要执行器，也不需要线程。

## 这一部分的页面

| 页面 | 内容 |
| --- | --- |
| [Result 与错误](./result.md) | `Result<T>`、`Result<void>`、`Error`、`ErrorCode` |
| [字节与缓冲](./bytes.md) | `Bytes`、`ByteView`、`Limits`、`Reader`、`Writer`、十六进制转换 |
| [Data 精确类型](./data.md) | `Data`、`DataType`、描述符类型、精确类型不混用规则 |
| [Data 编解码](./codec.md) | `encode_data`/`decode_data`、组合式 `read_data`/`write_data`、OAD 与长度域 |

## 一句话概括设计取向

- **不用异常表达可预期的失败。** 完整输入的编解码接口返回 `Result<T>`；只有 Reader/Writer 组合式接口会抛 `DecodeFailure`，由完整输入接口负责转换。
- **不用泛型值表达协议数据。** `Data` 是 `std::variant`，每个协议标签对应一个独立的 C++ 类型，不会把 UInt16、Enum 和字节串混成同一个值。
- **不隐藏所有权。** `ByteView` 借用内存且禁止从临时容器构造；异步写接管 `Bytes`；读取完成返回拥有内存的 `Bytes`。

## 包含关系

```text
<dlt698/dlt698.hpp>          聚合头文件：字节工具、Data、APDU 与链路帧接口
  <dlt698/common/result.hpp>    Result / Error / ErrorCode
  <dlt698/common/bytes.hpp>     Bytes / ByteView / Limits / Reader / Writer
  <dlt698/model/data.hpp>       Data / DataType / 描述符
  <dlt698/codec/data_codec.hpp> A-XDR 长度域与 Data 编解码
  <dlt698/protocol/apdu/*.hpp>   连接、GET、SET/ACTION、记录 APDU
  <dlt698/protocol/link/*.hpp   链路帧、流解析、分帧
```

只用编解码时包含 `<dlt698/dlt698.hpp>` 即可，不需要 Asio 头文件。
