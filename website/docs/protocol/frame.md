---
title: 链路帧与流解析
description: Frame、crc16、encode_frame/decode_frame 与 FrameStreamDecoder。
---

# 链路帧与流解析

头文件：`<dlt698/protocol/link/frame.hpp>`，命名空间 `dlt698::protocol::link`。

## 数据模型

```cpp
enum class AddressType : std::uint8_t { single = 0, wildcard = 1, group = 2, broadcast = 3 };

struct ServerAddress {
    AddressType type = AddressType::single;
    std::uint8_t logical = 0;    ///< 须为 0 至 3
    Bytes bytes = {0};           ///< 1 至 16 字节，按线序保存，低有效字节在前
};

struct Frame {
    std::uint8_t control = 0x43;
    ServerAddress server;
    std::uint8_t client = 0;
    Bytes payload;               ///< 未扰码的链路用户数据
};
```

`AddressType` 对应地址描述字节的高两位。**地址字节按线序保存**——低有效字节在前，不要按主机序重新排列。

`payload` 是**未扰码**的链路用户数据。SC 扰码由 `encode_frame`/`decode_frame` 负责，`Frame` 内部始终保持明文。它可能包含链路分帧头，本层不负责重组。

## 校验与编解码

```cpp
std::uint16_t crc16(ByteView bytes) noexcept;

Result<Bytes> encode_frame(const Frame& frame, const Limits& limits = {});
Result<Frame> decode_frame(ByteView bytes, const Limits& limits = {});
```

`crc16` 使用反射多项式 `0x8408`，初值和最终异或值都是 `0xFFFF`。线上以**低字节在前**发送。

`encode_frame` 输出包含起止符、HCS 和 FCS 的完整帧。控制字 SC 位启用时会对用户数据执行加 `0x33` 扰码。

`decode_frame` 只接受**恰好一个**从 `0x68` 到 `0x16` 的完整帧：不接受 FE 前导，也不接受尾随多帧。校验失败分别返回 `checksum_header`（HCS）或 `checksum_frame`（FCS）。

```cpp
dlt698::protocol::apdu::GetRequest request{1, false, {{0x4001, 2, 0}}, {}};
auto apdu = dlt698::protocol::apdu::encode_apdu(dlt698::protocol::apdu::Apdu{request});
if (apdu) {
    dlt698::protocol::link::Frame frame;
    frame.server.bytes = {0x07, 0x09, 0x19, 0x05, 0x16, 0x20};
    frame.payload = std::move(apdu).value();
    auto wire = dlt698::protocol::link::encode_frame(frame);
}
```

长度域和控制域按 2017 版严格检查，功能码支持 1 和 3。

## FrameStreamDecoder

真实链路上的字节块和协议帧边界没有关系：一次读取可能只有半帧，也可能粘了多帧。`FrameStreamDecoder` 负责从任意字节流里增量提取帧。

```cpp
using StreamEvent = std::variant<Frame, Error>;

class FrameStreamDecoder {
  public:
    explicit FrameStreamDecoder(Limits limits = {});
    FrameStreamDecoder(FrameStreamDecoder&&) noexcept;
    FrameStreamDecoder& operator=(FrameStreamDecoder&&) noexcept;

    std::vector<StreamEvent> feed(ByteView bytes);   ///< 输入并返回当前可确定的事件
    void reset() noexcept;                           ///< 丢弃未完成的帧和噪声
    std::size_t buffered_size() const noexcept;
};
```

```cpp
dlt698::protocol::link::FrameStreamDecoder decoder;
channel->async_read([&](dlt698::Result<dlt698::Bytes> result) {
    if (!result) return handle(result.error());
    for (auto& event : decoder.feed(result.value())) {
        if (auto* frame = std::get_if<dlt698::protocol::link::Frame>(&event))
            on_frame(*frame);
        else
            log(std::get<dlt698::Error>(event));
    }
});
```

行为要点：

- **半帧会被保留**，只在收到完整帧时才输出。仅收到半帧时返回空列表，可以继续喂数据。
- **粘帧会被拆开**，一次 `feed` 可能返回多个事件。
- **噪声被跳过**，坏帧产生错误事件后逐字节寻找下一个 `0x68`。
- 构造要求帧长度在 12 至 16385 字节之间，且 `max_stream_bytes` 不小于 `max_frame_bytes`，否则抛 `std::invalid_argument`。

:::warning 非线程安全，且半帧超时由你负责
`FrameStreamDecoder` 是单线程状态对象，必须在会话执行器或 strand 中使用。如果不通过 `Session` 使用，合法但不完整的帧需要你自己设置超时并调用 `reset()`——库不会因为超时自动丢弃它。
:::

## 与其他层的关系

- **FE 前导不在这一层。** 串行链路的四个 FE 由 [SerialLinkChannel](../transport/serial.md) 添加，原始读取保留 FE 交给 `FrameStreamDecoder` 处理。
- **链路分帧不在这一层。** `Frame::payload` 可能以分帧格式域开头，需要自己用 [LinkReassembler](./fragment.md) 处理。
- **会话已经封装了这一切。** 正常使用不需要直接操作 `FrameStreamDecoder`，`Session` 内部已完成接收泵、解析和分帧重组。
