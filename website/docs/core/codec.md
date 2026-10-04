---
title: Data 编解码
description: encode_data/decode_data、read_data/write_data、A-XDR 长度域与 OAD。
---

# Data 编解码

头文件：`<dlt698/codec/data_codec.hpp>`，命名空间 `dlt698::codec`。

## 两套接口

| 形式 | 接口 | 失败方式 |
| --- | --- | --- |
| 完整输入 | `decode_data` / `encode_data` | 返回 `Result<T>` |
| 组合解析 | `read_data` / `write_data`、长度域与 OAD | 抛 `DecodeFailure` |

需要解析一段字节里的多个字段（例如先读 OAD 再读 Data）时用组合形式，它们能保留 `Reader` 游标；只是编解码单个完整结构时用 `decode_data`/`encode_data`，错误已经转成 `Result`。

## 完整输入接口

```cpp
Result<model::Data> decode_data(ByteView input, const Limits& limits = {});
Result<Bytes> encode_data(const model::Data& value, const Limits& limits = {});
```

```cpp
#include <dlt698/dlt698.hpp>

dlt698::model::Data voltage = dlt698::model::UInt16{2413};
auto encoded = dlt698::codec::encode_data(voltage);
if (encoded) {
    auto decoded = dlt698::codec::decode_data(encoded.value());
    // decoded.value().as<dlt698::model::UInt16>().value == 2413
}
```

`decode_data` 内部把 `DecodeFailure` 转成 `Result` 错误，并保证：

- **拒绝尾随字节。** 输入必须只包含一个 Data，多余字节返回 `trailing_data`。
- **检查资源上限。** 字节数、节点总数（`max_elements`，含根节点）、嵌套深度（`max_depth`，根为 0）都会检查。
- **不猜测未知标签。** 保留或未实现的标签返回 `unsupported_tag`。

## 组合式接口

```cpp
model::Data read_data(Reader& reader, const Limits& limits, std::size_t depth = 0);
void write_data(Writer& writer, const model::Data& value, const Limits& limits,
                std::size_t depth = 0);
```

- `read_data` 从当前位置读一个 Data，**保留其后的字节**供继续解析。
- `write_data` 向现有输出追加一个带标签的 Data；输出字节上限由 `Writer` 创建时决定。
- 两者都会抛 `DecodeFailure`。
- `depth` 是当前根节点的起始嵌套深度，通常为零。

:::warning 两个容易忽略的点
- **节点预算在每次调用时重新计数。** `read_data`/`write_data` 的 `max_elements` 只约束本次调用读写的这一棵树，不是整条消息的累计值。需要整条消息的总预算时，自己在更高层限制数量。
- **`write_data` 失败时不回滚。** 已经追加的字节会留在 `Writer` 里。失败后请丢弃整个 `Writer`，不要试图继续写。
:::

典型用法：

```cpp
dlt698::Limits limits;
dlt698::Writer writer(limits.max_data_bytes);

dlt698::codec::write_oad(writer, oad);
dlt698::codec::write_data(writer, value, limits);
dlt698::Bytes bytes = writer.take();
```

解析侧：

```cpp
dlt698::Reader reader(dlt698::ByteView(bytes));
try {
    dlt698::model::Oad oad = dlt698::codec::read_oad(reader);
    dlt698::model::Data value = dlt698::codec::read_data(reader, limits);
    reader.finish();   // 确认没有多余字节
} catch (const dlt698::DecodeFailure& failure) {
    log(failure.error.code, failure.error.offset, failure.error.context);
}
```

## A-XDR 长度域

```cpp
std::size_t read_length(Reader& reader, std::size_t limit);
void write_length(Writer& writer, std::size_t length);
```

读取要求采用**最短编码形式**。不定长形式、非最小编码、长度值超过 `limit` 都会抛 `DecodeFailure`（对应 `invalid_length` 或 `invalid_value`）。写入同样使用最短形式。

## OAD

```cpp
model::Oad read_oad(Reader& reader);              ///< 读四字节，不读 Data 类型标签
void write_oad(Writer& writer, const model::Oad& value);
```

这两个函数只处理四字节 OAD 内容（OI 两字节 + 属性字节 + 索引字节），**不附加 Data 类型标签**。写入时属性字节的特征位原样保留。

## 校验往返

编解码是互逆的，可以在测试里直接断言：

```cpp
auto encoded = dlt698::codec::encode_data(input);
assert(encoded);
auto decoded = dlt698::codec::decode_data(encoded.value());
assert(decoded);
assert(decoded.value() == input);
```

`float32` 的 `NaN` 走原始位往返，比较时不要用相等判断。日期时间保持原始字段和通配值（`FF`/`FFFF`），不做日历校验，也不附加本机时区。
