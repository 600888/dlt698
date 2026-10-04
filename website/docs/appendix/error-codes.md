---
title: 错误码参考
description: ErrorCode 全部取值、含义与常见触发场景。
---

# 错误码参考

头文件：`<dlt698/common/result.hpp>`。

## ErrorCode 完整列表

```cpp
enum class ErrorCode {
    need_more_data,
    invalid_length,
    invalid_value,
    unsupported_tag,
    unsupported_service,
    checksum_header,
    checksum_frame,
    resource_limit,
    trailing_data,
    closed,
    io_error,
    timeout,
    cancelled,
    busy,
    address_mismatch,
    direction_mismatch,
    not_associated,
    association_failed,
    remote_error
};
```

## 编解码类

| 取值 | 含义 | 典型触发 |
| --- | --- | --- |
| `need_more_data` | 输入不足 | 半帧、组合解析时剩余字节不够读当前字段 |
| `invalid_length` | 长度非法 | A-XDR 长度域非最短形式、不定长、超过 limit；`be()` 宽度超过 8；十六进制奇数位 |
| `invalid_value` | 值非法 | 非法十六进制字符；`calendar_clock` 抛异常；链路地址非法 |
| `unsupported_tag` | Data 标签未实现 | 保留或未知标签；库不会猜长度跳过 |
| `unsupported_service` | APDU 分支未实现 | then-get、非空 FollowReport、SECURITY、REPORT、PROXY |
| `checksum_header` | HCS 校验失败 | 帧头校验不通过 |
| `checksum_frame` | FCS 校验失败 | 帧尾校验不通过 |
| `resource_limit` | 超出资源上限 | `Limits` 超限、队列预算不足、PIID 64 个序号全部隔离 |
| `trailing_data` | 存在尾随字节 | 完整输入 codec 解码后仍有剩余 |

## 传输类

| 取值 | 含义 |
| --- | --- |
| `closed` | 通道已关闭，或 `Session` 已 `close()`/析构 |
| `io_error` | 底层 I/O 失败，或同步适配的 drive 函数抛异常 |
| `timeout` | 请求超过 `request_timeout`（默认 5 秒） |
| `cancelled` | 被 `cancel()` 或 `async_release()` 取消 |
| `busy` | 通道已有在途读、已有在途事务，或同步适配器重入 |

## 会话类

| 取值 | 含义 |
| --- | --- |
| `address_mismatch` | SA/CA 不匹配，或响应 OAD 与请求不符 |
| `direction_mismatch` | DIR 不匹配（客户机↔服务器方向反了） |
| `not_associated` | 会话未处于应用连接状态就发起了请求 |
| `association_failed` | CONNECT 协商失败 |
| `remote_error` | 远端返回异常响应，原码在 `Error::remote_code` |

## Error 结构

```cpp
struct Error {
    ErrorCode code;
    std::size_t offset = 0;                        ///< 出错位置
    std::string context;                           ///< 字段名或传输层描述
    std::optional<std::uint8_t> remote_code = {};  ///< 远端原码
};
```

### offset 的单位

`offset` 的单位由接口约定：

| 接口 | 单位 |
| --- | --- |
| 二进制编解码 | 字节偏移 |
| `from_hex` | 文本中的**字符**偏移 |
| 传输层 | 通常为零或有意义值，取决于实现 |

没有对应位置时通常为零。

### context 的内容

`context` 保存字段名称或传输层描述，可直接写日志：

- 编解码：字段名，如 `"attribute"`、`"output limit"`、`"trailing bytes"`
- 传输：操作名，如 `"connect"`

### remote_code 的来源

`remote_code` 保存远端协议原码，库不做转换：

| 来源 | 位置 |
| --- | --- |
| ERROR-Response 的 `type` | 解码到 `ErrorResponse` 时 |
| GET 逐项 DAR | `AttributeResult::result` 的 `uint8_t` 分支 |
| CONNECT 拒绝结果 | `ConnectResponse::result` |
| 记录查询 DAR | `RecordResult::result` 的 `uint8_t` 分支 |

## DAR 与 ErrorCode 的区别

这两个是完全不同的概念，不要混用：

| | ErrorCode | DAR |
| --- | --- | --- |
| 归属 | 库内部 | 协议线格式 |
| 含义 | 本地解析/传输失败原因 | 远端拒绝服务的原因 |
| 出现位置 | `Result::error().code` | 响应 payload 中 |
| 是否可重试 | 视情况 | SET/ACTION 重试有副作用 |

**关键**：`Result` 成功但响应里带 DAR，才是业务失败。详见 [Result 与错误](../core/result.md#业务成功不能只看外层-result)。

## 记录日志的建议

```cpp
void log_error(const dlt698::Error& error) {
    log("code=%d offset=%zu context=%s remote=%d",
        int(error.code), error.offset, error.context.c_str(),
        error.remote_code ? int(*error.remote_code) : -1);
}
```

把 `offset` 和 `context` 一起打出来，才能定位到具体字段。`remote_code` 存在时一并记录，便于和远端日志对照。
