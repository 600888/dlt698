---
title: 频率与温度读取
description: 电网频率和表内温度；两个都是单值变量，属性 2 直接是数值。
---

# 频率与温度读取

## 能读取什么

频率和温度是**最简单的一类变量**：单相、单值、没有数组。

| 业务含义 | OI | 类型 | 倍率 | 单位 |
| --- | --- | --- | --- | --- |
| 电网频率 | `200F` | `UInt16` | -2 | Hz |
| 表内温度 | `2010` | `Int16` | -1 | ℃ |

两个对象的属性结构完全相同：

| 属性 | 名称 | 类型 | 读写 |
| --- | --- | --- | --- |
| 1 | 逻辑名 | `OctetString` | 只读 |
| 2 | 数值 | `UInt16` / `Int16` | 只读 |
| 3 | 换算及单位 | `ScalerUnit` | 只读 |

**索引只能是 0**，这两个对象没有数组实例，传非零索引会被 `validate_oad` 拒绝。

## 数据地址表

| 业务含义 | OI | 属性 | 索引 | 类型 | 倍率 | 单位 |
| --- | --- | --- | --- | --- | --- | --- |
| 电网频率 | `200F` | 2 | 0 | `UInt16` | -2 | Hz |
| 表内温度 | `2010` | 2 | 0 | `Int16` | -1 | ℃ |

:::warning 频率无符号，温度有符号
- **频率**是 `UInt16`，正常范围 45～65 Hz，线上 5000 = 50.00 Hz
- **温度**是 `Int16`，**可以是负值**（低温环境下），线上 -55 = -5.5 ℃

用 `UInt16` 解析温度，-5.5 ℃ 会变成 65481 ℃。
:::

## 最小完整示例

一次连接读两个值：

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

    auto freq = standard::make_oad(oi::frequency, 2, 0, layout);
    auto temp = standard::make_oad(oi::internal_temperature, 2, 0, layout);
    if (!freq || !temp) {
        std::cerr << "bad layout\n";
        client.disconnect();
        return 1;
    }

    auto response = client.get_list({freq.value(), temp.value()});
    if (!response) {
        std::cerr << response.error().context << '\n';
        client.disconnect();
        return 1;
    }

    // get_list 返回完整 GetResponse：外层成功只表示传输成功，
    // 每项仍要单独判断是 DAR 还是 Data —— 这就是"部分成功"。
    const auto& attributes = response.value().attributes;
    static const char* kNames[] = {"frequency", "temperature"};
    for (std::size_t i = 0; i < attributes.size(); ++i) {
        const auto& entry = attributes[i];
        if (const auto dar = std::get_if<std::uint8_t>(&entry.result)) {
            std::cerr << kNames[i] << " unsupported, DAR=" << unsigned(*dar) << '\n';
            continue;
        }
        // 建议按回显的 OAD 决定如何解释，而不是依赖请求顺序。
        const model::Oad& oad = entry.attribute;
        const auto& data = std::get<model::Data>(entry.result);
        auto numbers = standard::engineering_values(oad, data, layout);
        if (numbers && !numbers.value().empty())
            std::cout << (oad.oi == oi::frequency ? "frequency" : "temperature") << " = "
                      << standard::decimal_text(numbers.value()[0]) << ' '
                      << standard::unit_symbol(numbers.value()[0].scaling.unit) << '\n';
    }
    // 输出：
    // frequency = 50.00 Hz
    // temperature = 35.2 ℃

    client.disconnect();
    return 0;
}
```

## 常用变体

### 分开读取

```cpp
auto freq = standard::make_oad(oi::frequency, 2, 0, layout);
auto value = client.get(freq.value());
```

### 频率偏差判断

频率正常范围 45～65 Hz：

```cpp
const std::string text = standard::decimal_text(numbers.value()[0]);
// "50.00" 正常；"49.98" 偏低；"50.02" 偏高
```

判定门限按当地电网规程确定，库不做判断。

### 温度越限告警

温度可以设上下限：

```cpp
// 高温告警：> 85 ℃
// 低温告警：< -25 ℃
// 门限由应用决定，库不提供默认阈值
```

**表内温度是电表自身器件温度，不是环境温度或绕组温度**，与油浸式变压器测的绕组温度不是一回事。

## 请求与响应报文

以下示例固定：SA 六个 `00` 字节、逻辑地址 0、CA=0、PIID=0、控制域请求 `43`／响应 `C3`。

### 读取电网频率

请求（`200F/2/0`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 20 0F 02 00 00 1C 0C 16
```

响应（`UInt16` 5000，即 50.00 Hz）：

```text
68 1C 00 C3 05 00 00 00 00 00 00 00 BC FF 85 01 00 20 0F 02 00 01 12 13 88 00 00 30 D7 16
```

| 字段 | 字节 | 含义 |
| --- | --- | --- |
| `85` | | 服务：GET-Response |
| `01` | | GET-Normal |
| `00` | | PIID-ACD=0 |
| `20 0F 02 00` | | 回显 OAD：频率，属性 2，索引 0 |
| `01` | | 选择子：Data |
| `12` | | 数据类型：`UInt16` |
| `13 88` | | 原始值 5000 |
| `00` / `00` | | FollowReport / TimeTag 不存在 |

### 读取表内温度

请求（`2010/2/0`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 20 10 02 00 00 44 7D 16
```

响应（`Int16` 352，即 35.2 ℃）：

```text
68 1C 00 C3 05 00 00 00 00 00 00 00 BC FF 85 01 00 20 10 02 00 01 10 01 60 00 00 B0 9D 16
```

注意数据类型是 `10`（`Int16`），与频率的 `12`（`UInt16`）不同。

### 负温度

若设备报 `Int16` -55，线上是 `FF C9`：

| 原始字节 | 原始值 | 工程值 |
| --- | --- | --- |
| `FF C9` | -55 | -5.5 ℃ |

`decimal_text` 输出 `-5.5`。

### 带 PIID 的请求

同一 OAD 上多个请求靠 PIID 区分响应：

```text
PIID=1:   68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 01 20 0F 02 00 00 37 08 16
PIID=0x21: 68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 21 20 0F 02 00 00 57 8D 16
```

客户端自动管理 PIID，手工构造报文时才需要关心。

## 相关配置

| 配置 | 位置 | 说明 |
| --- | --- | --- |
| `SessionOptions::request_timeout` | 库连接参数 | 单次 GET 超时，默认 5 秒 |
| 温度/频率告警门限 | **应用侧** | 库不提供默认值 |

## 常见失败

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| DAR=4 | 设备不提供该变量 | 频率和温度都不是必备功能，低端表可能只有其中一个 |
| 负温度显示为六万多度 | 按 `UInt16` 解析温度 | 温度是 `Int16` |
| 频率显示 5000 | 忘了倍率 -2 | 线上 5000 是 50.00 Hz |
| 温度显示 352 | 忘了倍率 -1 | 线上 352 是 35.2 ℃ |
| 传非零索引被拒 | 这两个对象没有数组 | 索引固定 0，`validate_oad` 返回 `invalid_length` |
| `get_list` 返回成功但某项无值 | 部分成功 | 外层 `Result` 只表示传输成功，逐项查 DAR |