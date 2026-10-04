---
title: SET 与 ACTION
description: 普通与列表形式、DAR 与可选 Data 的区分。
---

# SET 与 ACTION

头文件：`<dlt698/protocol/apdu/mutation.hpp>`，命名空间 `dlt698::protocol::apdu`。

## SET

```cpp
struct SetAttribute {
    model::Oad attribute;
    model::Data value;
};

struct SetResult {
    model::Oad attribute;
    std::uint8_t dar = 0;
};

struct SetRequest {
    std::uint8_t piid = 0;
    bool list = false;
    std::vector<SetAttribute> attributes;
    std::optional<TimeTag> time_tag;
};

struct SetResponse {
    std::uint8_t piid_acd = 0;
    bool list = false;
    std::vector<SetResult> attributes;
    std::optional<TimeTag> time_tag;
};
```

- **Normal 恰含一项**，NormalList 非空。
- `value` 必须是精确类型 `Data`。库不替你把整数转成合适的协议类型。
- 响应直接保存 DAR，按请求顺序一一对应。

```cpp
dlt698::protocol::apdu::SetRequest request{
    /*piid=*/1, /*list=*/false, {{ {0x2000, 2, 0}, dlt698::model::UInt16{25} }},
    /*time_tag=*/std::nullopt};
auto bytes = dlt698::protocol::apdu::encode_mutation(request);
```

## ACTION

```cpp
struct ActionMethod {
    model::Omd method;
    model::Data parameter;
};

struct ActionResult {
    model::Omd method;
    std::uint8_t dar = 0;
    std::optional<model::Data> data;   ///< 存在 NULL Data 与没有返回数据是不同结果
};

struct ActionRequest {
    std::uint8_t piid = 0;
    bool list = false;
    std::vector<ActionMethod> methods;
    std::optional<TimeTag> time_tag;
};

struct ActionResponse {
    std::uint8_t piid_acd = 0;
    bool list = false;
    std::vector<ActionResult> methods;
    std::optional<TimeTag> time_tag;
};
```

:::warning `ActionResult::data` 的三态
`std::optional<model::Data>` 区分三种情况，不要用 `has_value()` 之外的方式判断：

| 情况 | 表现 |
| --- | --- |
| 方法无返回数据 | `data == std::nullopt` |
| 方法返回 NULL Data | `data.has_value()` 且内含 `model::Null{}` |
| 方法返回具体值 | `data.has_value()` 且内含具体类型 |

把后两种混为一谈会丢失"远端确实返回了一个 NULL"这个信息。
:::

ACTION 调用完整 OMD，包含 `mode` 字节。`Session` 会检查模式字节是否与请求一致。

## 编解码入口

```cpp
using MutationApdu = std::variant<SetRequest, SetResponse, ActionRequest, ActionResponse>;

Result<MutationApdu> decode_mutation(ByteView bytes, const Limits& limits = {});
Result<Bytes> encode_mutation(const MutationApdu& message, const Limits& limits = {});
```

- 普通形式必须恰好一项，列表形式必须非空，否则返回错误。
- 编码响应时**始终不附加** FollowReport。
- then-get 形式和带非空 FollowReport 的消息返回 `unsupported_service`。

## DAR 的常见取值

| DAR | 含义 |
| --- | --- |
| 0 | 成功 |
| 1 | 拒绝访问 |
| 2 | 对象不存在 |
| 3 | 拒绝访问（对象目录/权限层） |
| 4 | 未定义 |
| 5 | 不是记录型属性 |
| 7 | 类型不匹配 |
| 8 | 索引越界 |
| 255 | provider 抛出异常 |

各接口实际可能返回的取值见[协议覆盖范围](../appendix/coverage.md)。

## 重试语义

:::danger 库不会自动重试
SET 和 ACTION 有副作用。超时或取消**只说明本地没有得到确定响应，不能证明远端没有执行**。因此：

- 库不自动重试，也不自动回滚部分成功。
- 列表操作按顺序独立执行，部分成功不回滚。
- `async_release()` 也不保证远端未执行已发出的 SET/ACTION。

需要幂等保证或事务语义，得自己在应用层实现。
:::
