---
title: APDU 编解码
description: 统一 Apdu 变体与 encode_apdu/decode_apdu。
---

# APDU 编解码

头文件：`<dlt698/protocol/apdu/apdu.hpp>`，命名空间 `dlt698::protocol::apdu`。

## 统一入口

```cpp
using Apdu =
    std::variant<LinkRequest, LinkResponse, ConnectRequest, ConnectResponse, ReleaseRequest,
                 ReleaseResponse, ReleaseNotification, ErrorResponse, GetRequest, GetResponse,
                 SetRequest, SetResponse, ActionRequest, ActionResponse, GetRecordRequest,
                 GetRecordResponse, GetNextRequest, GetNextResponse>;

Result<Apdu> decode_apdu(ByteView bytes, const Limits& limits = {});
Result<Bytes> encode_apdu(const Apdu& message, const Limits& limits = {});
```

```cpp
auto apdu = dlt698::protocol::apdu::decode_apdu(bytes);
if (apdu) {
    std::visit(
        [](const auto& message) {
            using T = std::decay_t<decltype(message)>;
            if constexpr (std::is_same_v<T, dlt698::protocol::apdu::GetResponse>)
                std::cout << message.attributes.size() << " items\n";
        },
        apdu.value());
}
```

完整的 18 个变体分支：LINK 请求/响应、CONNECT 请求/响应、RELEASE 请求/响应/通知、双向 ERROR、GET/记录/GET-Next 请求响应、SET/ACTION 请求响应。

## 按服务单独编解码

`decode_apdu`/`encode_apdu` 之外，每类服务还有独立入口：

| 入口 | 覆盖分支 |
| --- | --- |
| `decode_connection` / `encode_connection` | LINK、CONNECT、RELEASE、ERROR |
| `decode_get` / `encode_get` | GET、GET Record、GET Next |
| `decode_mutation` / `encode_mutation` | SET、ACTION |

```cpp
// 只发一个 SET 请求
dlt698::protocol::apdu::SetRequest request{
    /*piid=*/1, /*list=*/false, {{oad, value}}, /*time_tag=*/std::nullopt};
auto bytes = dlt698::protocol::apdu::encode_mutation(request);
```

单独使用时更直接：入参就是具体类型，不必构造变体；错误信息也更聚焦。`decode_apdu` 适合"收什么都能解析"的通用通道。

## 通用行为

- **拒绝尾随字节。** 完整输入 codec 不接受多余字节，返回 `trailing_data`。
- **拒绝超限输入。** 受 `Limits` 的字节数和 `max_elements` 约束。
- **未实现分支明确报错。** then-get、非空 FollowReport 等返回 `unsupported_service`，不会静默通过。
- **保留远端原码。** ERROR-Response 转成 `remote_error`，原码在 `Error::remote_code`。

## 尺寸语义

`Limits::max_data_bytes` 约束的是**完整 Data/APDU** 的字节数，不包含链路封装。帧尺寸（长度域 `L` 的上限）不含起止符，APDU 尺寸不含链路封装——三个概念不要混用。
