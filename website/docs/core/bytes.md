---
title: 字节与缓冲
description: Bytes、ByteView、Limits、Reader、Writer 与十六进制转换。
---

# 字节与缓冲

头文件：`<dlt698/common/bytes.hpp>`，命名空间 `dlt698`。

## Bytes 与 ByteView

```cpp
using Bytes = std::vector<std::uint8_t>;
```

`ByteView` 是借用连续内存的只读视图，**不拥有**底层缓冲区。

```cpp
class ByteView {
  public:
    ByteView() = default;
    ByteView(const std::uint8_t* data, std::size_t size);  ///< 非空视图首地址不可为空
    ByteView(const Bytes& data) noexcept;                    ///< 借用容器全部内容
    ByteView(Bytes&&) = delete;                              ///< 禁止借用临时
    ByteView(const Bytes&&) = delete;                        ///< 禁止借用 const 临时

    const std::uint8_t* data() const noexcept;
    std::size_t size() const noexcept;
    bool empty() const noexcept;
    std::uint8_t operator[](std::size_t i) const;            ///< 不做边界检查
    ByteView subview(std::size_t offset, std::size_t size) const;
};
```

:::warning 生命周期约定
- `ByteView` 使用期间底层内存必须保持有效，且不能因容器扩容而移动。
- 禁止从临时 `Bytes` 构造，从编译期就杜绝悬空视图。
- `operator[]` 不检查边界，越界是未定义行为；先 `require` 或用 `subview`。
- `subview` 越界抛 `std::out_of_range`。

codec 在调用结束前完成读取，返回的都是拥有内存的值；异步写会接管传入的 `Bytes`；异步读取完成时回调收到的是拥有内存的 `Bytes`。
:::

## Limits

所有编解码入口的最后一个参数都是 `const Limits&`，默认构造即可。

```cpp
struct Limits {
    std::size_t max_data_bytes = 1024 * 1024;   ///< 完整 Data/APDU 的输入输出字节上限
    std::size_t max_elements = 65536;           ///< 单个 Data 树节点总数（含根），也是 GET 属性数上限
    std::size_t max_depth = 32;                 ///< Data 嵌套深度上限，根节点深度为 0
    std::size_t max_frame_bytes = 16385;        ///< 14 位长度字段加起止符的完整帧上限
    std::size_t max_stream_bytes = 32770;       ///< 流解析缓存上限，须能容纳最大帧
};
```

`max_elements` 统计的是**节点总数**而不是叶子数，包含根节点。`FrameStreamDecoder` 要求帧长度在 12 至 16385 字节之间，且流缓存上限不得小于帧上限，否则构造抛 `std::invalid_argument`。

## DecodeFailure

```cpp
class DecodeFailure final {
  public:
    explicit DecodeFailure(Error error);
    Error error;
};
```

这是 Reader/Writer 组合式接口的异常类型。**完整输入的 codec 负责把它转换成 `Result` 错误**，调用方通常不需要直接捕获它。

## Reader

在借用视图上推进游标，读取失败抛 `DecodeFailure`。

```cpp
class Reader {
  public:
    explicit Reader(ByteView input);

    std::size_t position() const noexcept;   ///< 相对输入起点的字节偏移
    std::size_t remaining() const noexcept;

    void require(std::size_t n, const char* field) const;      ///< 不推进游标
    std::uint8_t u8(const char* field = "byte");
    std::uint64_t be(std::size_t n, const char* field);        ///< 大端，n 为 0..8
    Bytes bytes(std::size_t n, const char* field);             ///< 复制，不借用
    void finish() const;                                        ///< 剩余非零则抛 trailing_data
};
```

`be(n, field)` 中 `n > 8` 抛 `invalid_length`；`n == 0` 返回 0。输入不足统一抛 `need_more_data`，错误偏移是当前游标位置。

## Writer

拥有输出缓冲区的写入器，**始终**检查上限。

```cpp
class Writer {
  public:
    explicit Writer(std::size_t limit);

    void u8(std::uint8_t value);
    void be(std::uint64_t value, std::size_t n);   ///< 取低 n 字节大端输出
    void bytes(ByteView value);
    Bytes take();                                   ///< 移出缓冲区，通常是编码最后一步
    std::size_t size() const noexcept;
};
```

`reserve(n)` 先检查再分配，超限抛 `resource_limit`，不会写入部分内容。`be` 会截断高于 `n` 字节的位。

## 十六进制转换

```cpp
Result<Bytes> from_hex(std::string_view text);
std::string to_hex(ByteView bytes);
```

- `from_hex` 接受大小写混排的十六进制字符，字节之间允许空白，**不接受 `0x` 前缀**。非法字符或半字节之间的空白返回 `invalid_value`，奇数位返回 `invalid_length`。失败时 `Error::offset` 是文本中的**字符偏移**。
- `to_hex` 输出大写、字节之间单个空格分隔，空输入返回空字符串。

```cpp
auto bytes = dlt698::from_hex("68 17 00 43");
// bytes.value() == {0x68, 0x17, 0x00, 0x43}
std::string text = dlt698::to_hex(bytes.value());  // "68 17 00 43"
```
