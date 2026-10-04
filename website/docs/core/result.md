---
title: Result 与错误
description: Result<T>、Result<void>、Error 结构与 ErrorCode 分类。
---

# Result 与错误

头文件：`<dlt698/common/result.hpp>`，命名空间 `dlt698`。

## Result<T>

`Result<T>` 是 `[[nodiscard]]` 的，成功值或错误二选一。

```cpp
template <class T>
class [[nodiscard]] Result {
  public:
    Result(T value);            ///< 创建成功结果
    Result(Error error);        ///< 创建失败结果

    explicit operator bool() const noexcept;   ///< 是否成功
    T& value() &;                            ///< 可修改的成功值
    const T& value() const&;                 ///< 只读的成功值
    T&& value() &&;                          ///< 从临时结果移出
    const Error& error() const;              ///< 失败诊断
};
```

用法：

```cpp
auto encoded = dlt698::codec::encode_data(voltage);
if (encoded) {
    auto decoded = dlt698::codec::decode_data(encoded.value());
    // decoded.value().as<dlt698::model::UInt16>().value == 2413
} else {
    log(encoded.error().code, encoded.error().offset, encoded.error().context);
}
```

:::warning 先判断再取值
`value()` 和 `error()` 在状态不匹配时分别抛 `std::bad_variant_access`。必须先判断 `operator bool()` 或使用 `if (result)`。
:::

`value()` 有三个重载，分别对应左值、const 左值和右值对象。在临时 `Result` 上直接调用 `value()` 会得到右值引用，可以把它移动走。

## Result<void>

无返回值的操作使用 `Result<void>`，默认构造表示成功。

```cpp
template <>
class [[nodiscard]] Result<void> {
  public:
    Result() = default;          ///< 成功
    Result(Error error);         ///< 失败
    explicit operator bool() const noexcept;
    const Error& error() const;  ///< 失败时抛 std::bad_optional_access
};
```

`channel->close()`、`session->async_release()` 一类操作的结果就是这样：判断 `operator bool()` 即可，成功时没有值可取。

## Error

```cpp
struct Error {
    ErrorCode code;                                ///< 错误分类
    std::size_t offset = 0;                        ///< 出错位置；二进制编解码为字节偏移
    std::string context;                           ///< 字段名称或传输层错误描述
    std::optional<std::uint8_t> remote_code = {};  ///< 远端原码
};
```

`context` 保存字段名（如 `"attribute"`、`"output limit"`）或传输层描述（如 `"connect"`），便于直接写日志。

`remote_code` 用于保存远端协议原码，不做转换：ERROR-Response 的 `type`、GET 的逐项 DAR、CONNECT 的拒绝结果都放在这里。

## ErrorCode

| 分类 | 取值 | 含义 |
| --- | --- | --- |
| 输入不足 | `need_more_data` | 剩余字节不够读取当前字段 |
| 长度非法 | `invalid_length` | 长度域形式非法、超限或宽度越界 |
| 值非法 | `invalid_value` | 字段值不合法，如非法十六进制字符 |
| 标签不支持 | `unsupported_tag` | Data 类型标签未实现或为保留值 |
| 服务不支持 | `unsupported_service` | APDU 分支未实现，如 then-get、FollowReport |
| 校验失败 | `checksum_header` / `checksum_frame` | HCS / FCS 校验不通过 |
| 资源超限 | `resource_limit` | 超出 `Limits`、队列预算或 PIID 耗尽 |
| 尾随数据 | `trailing_data` | 完整输入仍有未消费字节 |
| 传输状态 | `closed` / `io_error` / `timeout` / `cancelled` / `busy` | 通道关闭、I/O 失败、超时、取消、已有在途操作 |
| 会话状态 | `not_associated` / `association_failed` | 未处于应用连接、连接协商失败 |
| 匹配失败 | `address_mismatch` / `direction_mismatch` | 地址或方向不匹配 |
| 远端异常 | `remote_error` | 远端返回异常响应，原码在 `remote_code` |

完整列表见[错误码参考](../appendix/error-codes.md)。

## 业务成功不能只看外层 Result

这是接入时最容易出错的一点：

- **CONNECT 被拒绝**不算传输失败。`Result<ConnectResponse>` 可能是成功的，但 `response.result` 非零。
- **GET 部分成功**不算失败。`Result<GetResponse>` 成功，但每个 `AttributeResult::result` 可能是 DAR。
- **SET/ACTION 逐项结果**同理，`SetResult::dar`、`ActionResult::dar` 才是业务结论。

```cpp
auto connect = client.connect();
if (!connect) return /* 本地错误 */;
if (connect.value().result != 0) return /* 远端拒绝，原码在 result */;
```
