---
title: 协议层
description: 链路帧、流解析、链路分帧和各类 APDU 的编解码入口。
comments: false
---

# 协议层

协议层是纯编解码，不依赖 socket、执行器或线程。所有接口都是自由函数，输出 `Result<T>`。

## 分层

```text
链路层   Frame / FrameStreamDecoder / LinkFragmenter / LinkReassembler
            │  0x68 … 0x16，HCS/FCS，SC 扰码，FE 由传输层处理
应用层   Apdu（18 个变体）
            │  LINK / CONNECT / RELEASE / GET / SET / ACTION / ERROR / GET Record / GET Next
数据层   model::Data（精确类型）
```

## 先读这一页

第一次接触 698 的话，先看[对象模型与测点寻址](./point-model.md)。它回答"一个测点是怎么被确定的"，也就是 OI、属性标识和元素索引的三层关系，以及记录型对象为什么只能二维定位。不理解这一层，后面所有 GET/SET 的设计都悬着。

## 这一部分的页面

| 页面 | 内容 |
| --- | --- |
| [对象模型与测点寻址](./point-model.md) | OI + 属性 + 索引的三层展开，记录型对象的间接寻址，点表来源 |
| [标准固定点位](./standard-points.md) | 14 个常用 OI、OAD 构造、标准值校验、精确倍率与只读绑定 |
| [链路帧与流解析](./frame.md) | `Frame`、`crc16`、`encode_frame`/`decode_frame`、`FrameStreamDecoder` |
| [链路分帧](./fragment.md) | `Fragment`、`LinkFragmenter`、`LinkReassembler` |
| [APDU 编解码](./apdu.md) | 统一 `Apdu` 变体与 `encode_apdu`/`decode_apdu` |
| [连接管理 APDU](./connection.md) | LINK、CONNECT、RELEASE、ERROR 的值模型与线格式 |
| [GET 与记录查询](./get.md) | Normal/NormalList、Record/RecordList、GET Next 分块 |
| [SET 与 ACTION](./mutation.md) | 普通与列表形式、DAR 与可选 Data |

## 两种用法

**按服务单独编解码。** 需要构造某一种请求、或解析响应时，直接用 `encode_connection`、`encode_get`、`encode_mutation`。这是最常见的情况。

```cpp
auto bytes = dlt698::protocol::apdu::encode_mutation(SetRequest{1, false, {{oad, value}}, {}});
```

**统一变体分发。** 需要"先收字节、再按类型分发"的通用通道时，用 `encode_apdu`/`decode_apdu`。

```cpp
auto apdu = dlt698::protocol::apdu::decode_apdu(bytes);
if (apdu) {
    std::visit(handler, apdu.value());
}
```

## 尺寸约定

两个尺寸概念不要混淆：

- **帧尺寸**（`AssociationParameters::send_frame_bytes`）是长度域 `L` 的上限，**不包含** `0x68` 和 `0x16` 起止符。
- **APDU 尺寸**（`apdu_bytes`）不包含链路封装。

`Limits::max_frame_bytes` 是完整帧的字节上限（含起止符），默认 16385，对应 14 位长度字段的最大值。

## 异常分支的统一处理

未实现的 APDU 分支——then-get、非空 FollowReport、SECURITY、REPORT、PROXY——不会静默通过，而是返回 `unsupported_service`。这意味着你可以放心地解析未知对端的报文：能解出来的都是真实支持的分支，解不出来的会明确告诉你原因。
