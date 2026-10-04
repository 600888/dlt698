---
title: 连接管理 APDU
description: LINK、CONNECT、RELEASE、ERROR 的值模型与线格式。
---

# 连接管理 APDU

头文件：`<dlt698/protocol/apdu/connection.hpp>`，命名空间 `dlt698::protocol::apdu`。

## LINK

LINK 是**由协议服务器发起**的预连接操作，与 TCP 的连接发起方向无关。

```cpp
enum class LinkRequestType : std::uint8_t { login = 0, heartbeat = 1, logout = 2 };

struct LinkRequest {
    std::uint8_t piid_acd = 0;
    LinkRequestType type = LinkRequestType::login;
    std::uint16_t heartbeat_seconds = 180;
    model::DateTime requested_at;
};

struct LinkResponse {
    std::uint8_t piid = 0;
    std::uint8_t result = 0;    ///< bit7 为时钟可信标志，低三位为结果（0 至 3）
    model::DateTime requested_at;
    model::DateTime received_at;
    model::DateTime responded_at;
};
```

`LinkResponse::result` 的 bit7 是时钟可信标志，低三位是结果码。两个都要检查：结果码非零表示登录被拒绝，时钟不可信会影响后续时间标签的可用性。

LINK 编解码**不附加** Client/Server 尾部字节。

## CONNECT

### 协商参数

```cpp
struct AssociationParameters {
    std::uint16_t version = 0x0010;
    std::array<std::uint8_t, 8> protocol{};      ///< 一致性位图，最高位对应序号零
    std::array<std::uint8_t, 16> function{};
    std::uint16_t send_frame_bytes = 1024;
    std::uint16_t receive_frame_bytes = 1024;
    std::uint8_t receive_window = 1;
    std::uint16_t apdu_bytes = 1024;
    std::uint32_t timeout_seconds = 100;
};
```

:::warning 尺寸语义
`send_frame_bytes`/`receive_frame_bytes` 是长度域 `L` 的上限，**不包含** `0x68`/`0x16`。`apdu_bytes` **不包含**链路封装。
:::

一致性位图的位序是**最高位对应序号零**——这是最容易搞错的地方。

### 厂商字段

```cpp
struct FactoryVersion {
    std::array<std::uint8_t, 4> manufacturer{};
    std::array<std::uint8_t, 4> software_version{};
    std::array<std::uint8_t, 6> software_date{};
    std::array<std::uint8_t, 4> hardware_version{};
    std::array<std::uint8_t, 6> hardware_date{};
    std::array<std::uint8_t, 8> extension{};   ///< 允许零填充
};
```

固定长度字段，不含 A-XDR 长度前缀，也不带 Data 标签。

### 认证机制

```cpp
struct NullSecurity {};
struct PasswordSecurity { std::string password; };
struct SymmetrySecurity { Bytes ciphertext; Bytes signature; };
struct SignatureSecurity { Bytes ciphertext; Bytes signature; };

using ConnectMechanism =
    std::variant<NullSecurity, PasswordSecurity, SymmetrySecurity, SignatureSecurity>;

struct SecurityData {
    Bytes random;
    Bytes signature;
};

struct ConnectRequest {
    std::uint8_t piid = 0;
    AssociationParameters parameters;
    ConnectMechanism mechanism = NullSecurity{};
    std::optional<TimeTag> time_tag;
};

struct ConnectResponse {
    std::uint8_t piid_acd = 0;
    FactoryVersion factory;
    AssociationParameters parameters;
    std::uint8_t result = 0;              ///< 0 至 5 或 255，保留远端原码
    std::optional<SecurityData> security;
    std::optional<TimeTag> time_tag;
};
```

:::warning 编解码不等于认证
codec 支持四种认证机制的**线格式**，包括响应中的认证附加数据，但**不执行任何密码验证或认证运算**。

`Session` 只发起和接受**公共（NullSecurity）连接**；其他认证机制的请求会被拒绝。若需要真实认证，得自己在应用层实现。
:::

`ConnectResponse::result` 保留远端原码（0 至 5，或 255）。外层 `Result` 成功不代表连接成功，必须检查这个字段。

## RELEASE

```cpp
struct ReleaseRequest { std::uint8_t piid = 0; std::optional<TimeTag> time_tag; };

struct ReleaseResponse {
    std::uint8_t piid_acd = 0;
    std::uint8_t result = 0;      ///< 标准仅定义成功（0）
    std::optional<TimeTag> time_tag;
};

struct ReleaseNotification {
    std::uint8_t piid_acd = 0;
    model::DateTimeS established_at;
    model::DateTimeS current_time;
    std::optional<TimeTag> time_tag;
};
```

`ReleaseNotification` 由服务器在协商空闲时限到期时发送，通知对端应用连接已失效。客户机侧看到它时应当结束本地状态。

## ERROR

```cpp
struct ErrorResponse {
    bool server = true;      ///< true 编码为 EE，false 编码为 6E
    std::uint8_t piid = 0;
    std::uint8_t type = 2;   ///< 1=无法解析，2=服务不支持，255=其他
    std::optional<TimeTag> time_tag;
};
```

解码到 `ErrorResponse` 时，完整输入 codec 会把它转成 `Result` 的失败状态，错误码 `remote_error`，原始 `type` 保存在 `Error::remote_code`。

## 时间标签

```cpp
struct TimeTag {
    model::DateTimeS sent_at;    ///< 发送时间
    model::Ti allowed_delay;     ///< 允许的传输延时
};
```

`TimeTag` 可按原字段编解码，日期时间不做日历校验和时区附加。延时与有效期的**语义**（是否超期、是否丢弃）目前未实现——`Session` 会在请求侧配置 `request_time_tag` 自动添加，但不会因超期而拒绝。

## 编解码入口

```cpp
using ConnectionApdu =
    std::variant<LinkRequest, LinkResponse, ConnectRequest, ConnectResponse, ReleaseRequest,
                 ReleaseResponse, ReleaseNotification, ErrorResponse>;

Result<ConnectionApdu> decode_connection(ByteView bytes, const Limits& limits = {});
Result<Bytes> encode_connection(const ConnectionApdu& message, const Limits& limits = {});
```

非空 FollowReport 暂返回 `unsupported_service`。日历字段按原始字节保存，不做格式转换。
