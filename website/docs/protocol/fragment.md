---
title: 链路分帧
description: Fragment、LinkFragmenter 与 LinkReassembler。
---

# 链路分帧

头文件：`<dlt698/protocol/link/fragment.hpp>`，命名空间 `dlt698::protocol::link`。

链路分帧用于 APDU 超过帧尺寸上限时的拆分与重组。它独立于 APDU 服务——分帧格式域和 APDU 内容互不知情。

## 数据模型

```cpp
enum class FragmentType : std::uint8_t { first = 0, last = 1, acknowledgement = 2, middle = 3 };

struct Fragment {
    FragmentType type = FragmentType::first;
    std::uint16_t sequence = 0;   ///< 0 至 4095
    Bytes data;
};
```

```cpp
Result<Fragment> decode_fragment(ByteView payload);
Result<Bytes> encode_fragment(const Fragment& fragment);
```

- `decode_fragment` 的输入必须是**已解除 SC 扰码**的链路用户数据。格式域为小端两字节。
- `encode_fragment` 要求序号在 0 至 4095 之间，数据帧不得为空，**确认帧不得包含数据**。
- 保留位错误、类型错误或长度错误都会返回明确的错误，不会静默跳过。

## 发送端

```cpp
class LinkFragmenter {
  public:
    LinkFragmenter(Bytes apdu, std::size_t fragment_bytes, std::size_t limit);

    Fragment current() const;                          ///< 重复调用不前进，可用于超时重发
    Result<void> acknowledge(std::uint16_t sequence);  ///< 用最近正确序号推进一次
};
```

- `apdu` 是至少分为两片的完整 APDU，函数接管其内存。
- `fragment_bytes` 是每片数据字节上限，不含两字节格式域，必须大于零。
- `limit` 是 APDU 拥有内存的上限。
- 参数非法或 APDU 超限时构造抛 `std::invalid_argument`。

`current()` 幂等——超时重发时重复调用它得到同一片，不会误推进。`acknowledge()` 只接受**最近正确的序号**，过期确认会返回错误而不是回退状态。

## 接收端

```cpp
struct Reassembly {
    std::optional<std::uint16_t> acknowledge;   ///< 需要回复确认的序号
    std::optional<Bytes> apdu;                  ///< 重组完成的完整 APDU
    bool duplicate = false;                     ///< 该片段是重复的
};

class LinkReassembler {
  public:
    explicit LinkReassembler(std::size_t limit);

    Result<Reassembly> accept(const Fragment& fragment);
    void reset();
    bool active() const noexcept;
};
```

`accept` 的行为：

- **按序接收。** 起始片段必须是序号零。
- **重复不重复提交。** 重复片段会置 `duplicate` 标志，但不会再次交付 APDU。
- **乱序不污染缓冲。** 序号不连续时返回错误，已有的重组缓冲保持原样。
- **末片直接交付应用**，按标准图 16/17 不等待链路确认。

`limit` 是完整 APDU 的字节上限，必须大于零。`LinkReassembler` **不创建定时器**，地址、方向和超时检查由调用方负责；未完成的缓冲需要调用 `reset()` 回收。

:::warning 自行使用时责任在调用方
`LinkReassembler` 只管字节重组。对端地址、SA/CA/DIR/PRM 是否匹配、重组超时、连接是否仍然有效——这些都要你自己判断。正常使用 `Session` 时这些检查已经内置，不需要直接用这个类。
:::

## 与 Session 的关系

`Session` 已经把链路分帧集成到事务里：`SessionOptions::fragment_timeout`（默认 1000ms，单片确认及未完成重组的超时）和 `fragment_retries`（默认 2，仅重发未获确认的相同链路片段）控制行为，`prefer_get_blocks`（默认 true）决定超长 GET 是否优先走应用层分块。

应用层分块（GET Next）与链路分帧是两套独立机制，详见 [GET 与记录查询](./get.md)。
