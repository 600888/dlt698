---
title: 状态字读取
description: 电能表运行状态字 1～7、跟随上报状态字与模式字；位串不解释业务含义。
---

# 状态字读取

## 能读取什么

两个对象：

| 业务含义 | OI | 类型 | 说明 |
| --- | --- | --- | --- |
| 电能表运行状态字 | `2014` | Array\<`BitString(16)`\> | 固定 7 项，对应附录 F.1～F.7 |
| 跟随上报状态字 | `2015` | `BitString(32)` | 单个 32 位位串 |

:::warning 库不解释状态字的位含义
状态字是**位串**，不是数值。库里：

- 校验位宽（16 位 / 32 位）和数组长度（7 项）
- **保留原始字节，不做位到故障代码的翻译**
- 不清除保留位，不自动识别故障业务
- **不参与 `engineering_values`** —— 位串没有数值倍率

各状态的**业务含义**（如"失压"、"逆相序"、"报警"）须由应用按附录 F 自行定义位掩码。这是刻意的取舍：不同厂家对保留位的用法不同，库不替你猜。
:::

## 数据地址表

### 运行状态字 `2014`

| 属性 | 名称 | 类型 | 长度 | 说明 |
| --- | --- | --- | --- | --- |
| 1 | 逻辑名 | `OctetString` | — | 通常不读 |
| 2 | 运行状态字 1～7 | Array\<`BitString`\> | 固定 7 项 × 16 位 | 索引 0 整体，1～7 = 状态字 1～7 |
| 3 | 换算及单位 | `ScalerUnit` | — | 占位，无实际倍率 |

7 项对应标准附录 F.1～F.7，**长度固定 7，与 `DeviceLayout` 无关**。

### 跟随上报状态字 `2015`

| 属性 | 名称 | 类型 | 长度 | 读写 |
| --- | --- | --- | --- | --- |
| 1 | 逻辑名 | `OctetString` | — | 只读 |
| 2 | 跟随上报状态字 | `BitString` | 32 位 | 只读 |
| 3 | 换算及单位 | `ScalerUnit` | — | 占位 |
| 4 | 跟随上报模式字 | `BitString` | 32 位 | **标准可写** |

:::tip 属性 4 可写 ≠ 你能写
`AttributeDefinition::writable = true` 表示**标准定义了写入能力**，实际写入还要过两关：

1. 客户端侧：`Client` 的运行 schema 必须显式开放该属性
2. 设备侧：设备必须允许写入

见[写参数与执行方法](./write-action.md)。
:::

## 最小完整示例

读取运行状态字并按位掩码判断：

```cpp
#include <dlt698/app.hpp>
#include <dlt698/standard/catalog.hpp>
#include <iostream>

int main() {
    using namespace dlt698;
    namespace oi = standard::oi;

    const standard::DeviceLayout layout{standard::Wiring::three_phase, 4, 21};

    app::ClientOptions options;
    options.protocol.server.bytes = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    app::Client client(options);
    auto connected = client.connect_tcp("127.0.0.1", 6980);
    if (!connected) {
        std::cerr << connected.error().context << '\n';
        return 1;
    }

    auto oad = standard::make_oad(oi::operating_status, 2, 0, layout);
    if (!oad) {
        std::cerr << oad.error().context << '\n';
        client.disconnect();
        return 1;
    }

    auto value = client.get(oad.value());
    if (!value) {
        std::cerr << value.error().context << '\n';
        client.disconnect();
        return 1;
    }
    if (const auto dar = std::get_if<std::uint8_t>(&value.value())) {
        std::cerr << "meter does not provide status words, DAR=" << unsigned(*dar) << '\n';
        client.disconnect();
        return 1;
    }

    const auto& data = std::get<model::Data>(value.value());
    const auto& words = data.as<model::Array>().value;

    // 业务含义由应用定义，下面是示例掩码，不是标准定义。
    // 位串字节序按协议：value[0] 是高字节。
    for (std::size_t i = 0; i < words.size(); ++i) {
        const auto& word = words[i].as<model::BitString>();
        const std::uint16_t raw =
            std::uint16_t(word.value[0]) << 8 | std::uint16_t(word.value[1]);
        std::cout << "status word " << (i + 1) << " = 0x" << std::hex << raw << std::dec;
        if ((raw & 0x0001) != 0) std::cout << " [bit0]";
        if ((raw & 0x1000) != 0) std::cout << " [bit12]";
        std::cout << '\n';
    }
    // 输出：
    // status word 1 = 0x4104 [bit12]
    // status word 2 = 0x8004 [bit0]

    client.disconnect();
    return 0;
}
```

:::warning 位序需要按设备文档核对
上面示例用「高字节在前」拼成 16 位整数，这是常见约定。**但位串内部的位序（是 bit0 在前还是在后）取决于设备实现和附录 F 的定义**，库不做转换。正式产品应对照该型号的说明书确认，并把掩码定义集中管理。
:::

## 常用变体

### 只读一个状态字

```cpp
auto word1 = standard::make_oad(oi::operating_status, 2, 1, layout); // 2014/2/1
auto word3 = standard::make_oad(oi::operating_status, 2, 3, layout); // 2014/2/3

auto result = client.get_list({word1.value(), word3.value()});
```

索引 1～7 对应状态字 1～7，索引 0 是整个数组。**不要假设索引越界安全**，库会按 7 项校验。

### 读跟随上报状态字

```cpp
auto oad = standard::make_oad(oi::follow_report_status, 2, 0, layout); // 2015/2/0
auto value = client.get(oad.value());
if (value && !std::get_if<std::uint8_t>(&value.value())) {
    const auto& data = std::get<model::Data>(value.value());
    const auto& bits = data.as<model::BitString>();
    std::cout << "follow report status, " << bits.bit_count << " bits\n";
    for (std::size_t i = 0; i < bits.value.size(); ++i)
        std::cout << "byte " << i << " = 0x" << std::hex << unsigned(bits.value[i]) << std::dec << '\n';
    // 输出：
    // follow report status, 32 bits
    // byte 0 = 0x0
    // byte 1 = 0x0
    // byte 2 = 0x0
    // byte 3 = 0x2
}
```

### 读跟随上报模式字

```cpp
auto oad = standard::make_oad(oi::follow_report_status, 4, 0, layout); // 2015/4/0
```

### 变化检测

轮询状态字时，按位比较前后两次的原始字节，**只上报变化的位**：

```cpp
// 上次的 7 项位串存下来
// 本次读回后逐字节比对，只对异或非零的字节生成事件
// 这样避免每次轮询都产生一条"状态未变"的无效记录
```

## 请求与响应报文

以下示例固定：SA 六个 `00` 字节、逻辑地址 0、CA=0、PIID=0、控制域请求 `43`／响应 `C3`。

### 读取运行状态字 1

请求（`2014/2/1`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 20 14 02 01 00 70 16 16
```

响应：

```text
68 37 00 C3 05 00 00 00 00 00 00 00 2F 5F 85 01 00 20 14 02 01 01 01 07 04 10 04 10 04 10 80 04 04 10 00 00 04 10 00 00 04 10 00 00 04 10 00 00 04 10 00 00 00 00 F8 79 16
```

| 字段 | 字节 | 含义 |
| --- | --- | --- |
| `85` | | 服务：GET-Response |
| `01` | | GET-Normal |
| `00` | | PIID-ACD=0 |
| `20 14 02 01` | | 回显 OAD：运行状态字，属性 2，索引 1 |
| `01` | | 选择子：Data |
| `01` | | 数据类型：`Array` |
| `07` | | 数组长度 7 |
| `04 10` | | 元素 1：`BitString`，长度 2 字节（16 位），值 `04 10` |
| `04 10` | | 元素 2：`04 10` |
| `04 10` | | 元素 3：`04 10` |
| `80 04` | | 元素 4：`80 04` |
| `04 10` | | 元素 5～7：`04 10` |
| `00` / `00` | | FollowReport / TimeTag 不存在 |

每个位串元素前**必须先写字节长度**，再写内容。注意元素 4 是 `80 04` 而其他是 `04 10` —— 这不是解析错误，位串内容本就不同。

### 读取全部 7 个运行状态字

请求（`2014/2/0`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 20 14 02 00 00 A8 0F 16
```

响应：

```text
68 37 00 C3 05 00 00 00 00 00 00 00 2F 5F 85 01 00 20 14 02 00 01 01 07 04 10 04 10 04 10 80 04 04 10 00 00 04 10 00 00 04 10 00 00 04 10 00 00 04 10 00 00 00 00 25 28 16
```

同样是 7 项，OAD 回显为索引 0。

### 读取跟随上报状态字

请求（`2015/2/0`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 20 15 02 00 00 13 13 16
```

响应（32 位位串）：

```text
68 1F 00 C3 05 00 00 00 00 00 00 00 0F 01 85 01 00 20 15 02 00 01 04 20 00 00 00 02 00 00 71 E6 16
```

| 字段 | 字节 | 含义 |
| --- | --- | --- |
| `01` | | 选择子：Data |
| `04` | | 数据类型：`BitString` |
| `20` | | 位长 **32** 位（1 字节） |
| `00 00 00 02` | | 4 字节内容：32 位 |

这里位长字节是 `20`（十六进制 32），表示 32 位。四个内容字节中最后一个是 `02`，只有 bit1 为 1。

### 读不到位串时

`2015/2/0` 返回时是**单个位串，不是数组**。若设备把跟随上报状态做成数组，解析会因类型不符失败。这是属性定义差异，属正常。

## 相关配置

| 配置 | 位置 | 说明 |
| --- | --- | --- |
| `SessionOptions::request_timeout` | 库连接参数 | 单次 GET 超时，默认 5 秒 |
| 状态字位掩码定义 | **应用侧** | 库不提供，按设备型号和附录 F 定义 |
| 跟随上报模式字写权限 | 客户端 schema + 设备 | 标准可写，实际要两边都允许 |

## 常见失败

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| DAR=4 | 设备不提供状态字 | 部分简易表只提供电量和时钟 |
| 数组长度不是 7 | 设备实现与标准不同 | 库按标准校验 7 项，不兼容时会失败 |
| `engineering_values` 返回 `unsupported_tag` | 对位串调用了它 | 位串没有倍率，直接读 `Data` |
| 状态判断和现场实际不符 | 位序或掩码不对 | 库不解释位含义，需按型号文档核对 |
| 某保留位恒为 1 | 设备用它做厂商私有标志 | 属正常，不要当成故障 |
| 属性 4 写入失败 | schema 未开放或设备拒绝 | 见[写参数与执行方法](./write-action.md) |