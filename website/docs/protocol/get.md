---
title: GET 与记录查询
description: Normal/NormalList、Record/RecordList 与 GET Next 分块。
---

# GET 与记录查询

头文件：`<dlt698/protocol/apdu/get.hpp>`、`<dlt698/protocol/apdu/get_block.hpp>`，命名空间 `dlt698::protocol::apdu`。

## 属性读取

```cpp
struct GetRequest {
    std::uint8_t piid = 0;   ///< 优先级位及低六位调用标识，第 6 位保留且须为零
    bool list = false;
    std::vector<model::Oad> attributes;
    std::optional<TimeTag> time_tag;
};

struct AttributeResult {
    model::Oad attribute;
    std::variant<std::uint8_t, model::Data> result;   ///< uint8_t 为 DAR，Data 为属性值
};

struct GetResponse {
    std::uint8_t piid_acd = 0;
    bool list = false;
    std::vector<AttributeResult> attributes;
    std::optional<TimeTag> time_tag;
};
```

约束：

- **Normal 恰含一个属性**，NormalList 必须非空。`list=false` 传多个属性会返回错误。
- **DAR 与 Data 分开保存。** `AttributeResult::result` 是 variant，判断类型才知道是失败还是值。
- **按原顺序保留。** 响应顺序与请求 OAD 顺序一一对应，`Session` 会检查这一点。
- **不支持 FollowReport。** 解码带该字段的响应返回 `unsupported_service`；编码响应时始终写入"FollowReport 不存在"的标记。

读取单个属性：

```cpp
for (const auto& item : response.attributes) {
    if (auto* dar = std::get_if<std::uint8_t>(&item.result))
        log("OI", item.attribute.oi, "DAR", unsigned(*dar));
    else
        log("OI", item.attribute.oi, to_hex(*std::get_if<model::Data>(&item.result)));
}
```

## 记录查询

```cpp
struct GetRecord {
    model::Oad attribute;
    model::Rsd rows;        ///< 行选择器
    model::Rcsd columns;     ///< 列选择器，空 RCSD 为全选
};

struct GetRecordRequest {
    std::uint8_t piid = 0;
    bool list = false;
    std::vector<GetRecord> records;
    std::optional<TimeTag> time_tag;
};

using RecordRow = std::vector<model::Data>;

struct RecordResult {
    model::Oad attribute;
    model::Rcsd columns;
    std::variant<std::uint8_t, std::vector<RecordRow>> result;
};

struct GetRecordResponse {
    std::uint8_t piid_acd = 0;
    bool list = false;
    std::vector<RecordResult> records;
    std::optional<TimeTag> time_tag;
};
```

每条查询保存完整的 `Rsd`/`Rcsd`。响应必须给出表头——即使 RCSD 为空表示全选，`RecordResult::columns` 里也要列出实际列。选择器的业务语义由 provider 解释，库不做假设。

行数据用 `RecordRow`（一组 `Data`）表示，每行的列数必须与 `columns` 一致，且每列类型与表头匹配。

## GET Next 分块

```cpp
struct GetNextRequest {
    std::uint8_t piid = 0;
    std::uint16_t block = 0;    ///< 最近正确接收的块号，保持原请求 PIID/优先级
    std::optional<TimeTag> time_tag;
};

struct GetNextResponse {
    std::uint8_t piid_acd = 0;
    bool last = false;
    std::uint16_t block = 0;
    std::variant<std::uint8_t, std::vector<AttributeResult>, std::vector<RecordResult>> result;
    std::optional<TimeTag> time_tag;
};
```

### 服务端切分

```cpp
using GetSnapshot = std::variant<GetResponse, GetRecordResponse>;

class GetBlockTransfer {
  public:
    static Result<std::vector<GetNextResponse>> split(GetSnapshot snapshot,
                                                      std::size_t target_bytes,
                                                      const Limits& limits);

    GetBlockTransfer(std::uint8_t piid, bool records, Limits limits,
                     bool merge_record_rows = true);
    Result<std::optional<GetSnapshot>> accept(const GetNextResponse& block);
};
```

`split` 的语义：

- **完整属性或记录行是最小单位。** 记录表按行拆分，各块保留 OAD/RCSD；客户机合并相同表头的连续记录行。重复记录 OAD 的查询保持整个记录结果边界，Session 自动禁用合并；独立收集器须显式传 `merge_record_rows=false`。
- `target_bytes` 是期望值而非硬约束。不可切分的单元超过 `target_bytes` 时仍由链路分帧传输，但必须满足 `limits`。
- 最多 65536 块，16 位块号不循环。
- `snapshot` 是首次查询结果，后续取块**不再次读取 provider**。快照总量、超时和会话数量由调用方另外限制。

`accept` 的语义：

- 只接受**从零开始的连续块**，跳号、重复、分支变化都返回错误。
- 非末块返回空 `optional`，末块返回完整快照。
- 失败时 DAR 原码在 `Error::remote_code`。
- 不检查 TimeTag 和地址——`Session` 检查后再调用；也不会自动重发请求。

### 客户端收集

正常使用不需要直接用 `GetBlockTransfer`。`Session::async_get` 和 `async_get_record` 会自动收齐所有数据块，回调拿到的是完整结果。

```cpp
session->async_get_record(records, /*list=*/false,
                          [&](dlt698::Result<dlt698::protocol::apdu::GetRecordResponse> result) {
                              if (result) handle_rows(result.value().records);
                          });
```

如果注册了记录处理器，`Session` 会缓存首次结果直到分块完成或超时，后续分页**不会再次调用 provider**。

## 编解码入口

```cpp
using GetApdu = std::variant<GetRequest, GetResponse, GetRecordRequest, GetRecordResponse,
                             GetNextRequest, GetNextResponse>;

Result<GetApdu> decode_get(ByteView bytes, const Limits& limits = {});
Result<Bytes> encode_get(const GetApdu& apdu, const Limits& limits = {});
```

`decode_get` 还会检查 PIID 保留位、属性结果选择器和可选时间标签的合法性。其他服务、变体或带 FollowReport 的响应返回 `unsupported_service`。

## 与链路分帧的区别

两套机制独立：

| | GET Next 应用层分块 | 链路分帧 |
| --- | --- | --- |
| 拆分单位 | 完整属性/记录行（重复 OAD 保留整个结果） | 任意字节片 |
| 触发条件 | 结果超过 APDU 尺寸 | APDU 超过帧尺寸 |
| 谁负责 | `GetBlockTransfer` | `LinkFragmenter`/`LinkReassembler` |
| 是否自动 | `Session` 自动完成 | `Session` 自动完成 |

`SessionOptions::prefer_get_blocks`（默认 true）决定超长 GET 优先走应用层分块；即使走应用层分块，单个块超过帧尺寸时仍会用链路分帧传输。
