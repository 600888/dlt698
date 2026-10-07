---
title: 谐波与波形失真度
description: 电压与电流谐波含有量、总含有量与指定次数；波形失真度；最高谐波次数与数组长度配置。
---

# 谐波与波形失真度

## 能读取什么

四个对象分成两组：

| 业务含义 | OI | 说明 |
| --- | --- | --- |
| 电压谐波含有量 | `200D` | A/B/C 三相，属性 2/3/4 |
| 电流谐波含有量 | `200E` | A/B/C 三相，属性 2/3/4 |
| 电压波形失真度 | `200B` | 各相 THD，属性 2 是三相数组 |
| 电流波形失真度 | `200C` | 各相 THD，属性 2 是三相数组 |

**谐波和失真度是两回事**：

- 谐波含有量：每个次数的**含量**（%）
- 波形失真度（THD）：全部次数合成的**总失真度**（%），由各次含量算出

设备一般两者都提供，但**不一定同时提供**。

## 数据地址表

### 谐波含有量对象

谐波对象是**唯一一类相别放在属性号上的对象**：

| 属性 | 名称 | 相别 | 索引含义 |
| --- | --- | --- | --- |
| 1 | 逻辑名 | — | — |
| 2 | A 相总及 2～n 次含有量 | A | 0 整体，1 总含有量，k = k 次 |
| 3 | B 相总及 2～n 次含有量 | B | 同上 |
| 4 | C 相总及 2～n 次含有量 | C | 同上 |
| 5 | 最高谐波次数 | — | 索引固定 0 |
| 6 | 换算及单位 | — | 索引固定 0 |

```text
属性号 = 相别：  2 → A 相    3 → B 相    4 → C 相
索引   = 次数：  0 → 整个数组
                1 → 总含有量
                2 → 2 次谐波
                ...
                n → n 次谐波
```

**没有 1 次（基波）项**。基波通常就是额定频率，不作为谐波统计。

### 属性类型与倍率

| 业务含义 | 类型 | 倍率 | 单位 | 长度 |
| --- | --- | --- | --- | --- |
| 电压/电流谐波含有量 | `Int16` | -2 | % | `harmonic_order` |
| 电压/电流波形失真度 | `Int16` | -2 | % | 3（三相）或 1（单相） |

:::warning 谐波数组长度由 `DeviceLayout::harmonic_order` 决定
数组长度 = `harmonic_order`，**不是** `harmonic_order - 1`。结构是「1 项总含有量 + (harmonic_order - 1) 项各次谐波」：

| `harmonic_order` | 数组长度 | 索引范围 |
| --- | --- | --- |
| 21（默认） | 21 | 1（总）+ 2～21 次 |
| 15 | 15 | 1（总）+ 2～15 次 |
| 2 | 2 | 1（总）+ 2 次 |

**必须先读设备属性 5（最高谐波次数）再构造布局**，否则长度校验一定失败。
:::

## 最小完整示例

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

    // A 相 2 次电压谐波：属性 2（A 相），索引 2（2 次）
    auto oad = standard::harmonic_oad(oi::voltage_harmonics, standard::Phase::a, 2, layout);
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
        std::cerr << "meter does not provide harmonics, DAR=" << unsigned(*dar) << '\n';
        client.disconnect();
        return 1;
    }

    const auto& data = std::get<model::Data>(value.value());
    auto numbers = standard::engineering_values(oad.value(), data, layout);
    if (numbers && !numbers.value().empty())
        std::cout << "A-phase 2nd voltage harmonic = "
                  << standard::decimal_text(numbers.value()[0]) << ' '
                  << standard::unit_symbol(numbers.value()[0].scaling.unit) << '\n';
    // 输出：A-phase 2nd voltage harmonic = 1.25 %

    client.disconnect();
    return 0;
}
```

## 常用变体

### 先确认最高谐波次数

**这是读谐波前的第一步**：

```cpp
auto oad = standard::make_oad(oi::voltage_harmonics, 5, 0, layout); // 200D/5/0
auto value = client.get(oad.value());
if (value && !std::get_if<std::uint8_t>(&value.value())) {
    const auto& data = std::get<model::Data>(value.value());
    // 属性 5 是 UInt8 计数，没有倍率。Data 用 as<T>() 按精确类型访问，
    // 类型不符会抛 std::bad_variant_access，所以先确认 type()。
    if (data.type() == model::DataType::uint8) {
        const std::size_t order = data.as<model::UInt8>().value;
        std::cout << "highest harmonic order = " << order << '\n';
        // 用它构造布局：数组长度就是 order
        const standard::DeviceLayout actual{standard::Wiring::three_phase, 4, order};
        // ...
    }
}
```

:::warning 无倍率属性不能用 `engineering_values`
`engineering_values` 只处理**有数值倍率**的属性。属性 5（最高谐波次数）、`4010` 计量元件数等是纯 `UInt8` 计数，对它们调用会返回：

```text
non-numeric standard attribute
```

这不是错误，是设计取舍。要读这类属性直接取 `Data` 里的原始整数。
:::

### 读总含有量

`order` 传 0 取总含有量（索引 1）：

```cpp
auto total = standard::harmonic_oad(oi::current_harmonics, standard::Phase::a, 0, layout);
// 200E/2/1 → Int16 480 → 4.80 %
```

### 一次读某相全部次数

索引 0 读整个数组：

```cpp
auto all = standard::make_oad(oi::voltage_harmonics, 2, 0, layout); // 200D/2/0
auto value = client.get(all.value());
if (value && !std::get_if<std::uint8_t>(&value.value())) {
    const auto& data = std::get<model::Data>(value.value());
    auto numbers = standard::engineering_values(all.value(), data, layout);
    if (numbers) {
        // 顺序：总含有量、2 次、3 次 …… n 次
        for (std::size_t i = 0; i < numbers.value().size(); ++i)
            std::cout << (i == 0 ? "total" : std::to_string(i + 1) + "th")
                      << " = " << standard::decimal_text(numbers.value()[i]) << " %\n";
    }
}
```

### 遍历三相

```cpp
for (auto phase : {standard::Phase::a, standard::Phase::b, standard::Phase::c}) {
    auto oad = standard::harmonic_oad(oi::voltage_harmonics, phase, 2, layout);
    // 属性分别是 2/3/4
}
```

### 读波形失真度

波形失真度用 `phase_oad`，相别在索引里（与谐波不同）：

```cpp
auto thd = standard::make_oad(oi::voltage_distortion, 2, 0, layout); // 200B/2/0
auto a   = standard::phase_oad(oi::voltage_distortion, standard::Phase::a, layout); // 200B/2/1
```

## 请求与响应报文

以下示例固定：SA 六个 `00` 字节、逻辑地址 0、CA=0、PIID=0、控制域请求 `43`／响应 `C3`。

> **本节响应做了简化**：真实设备属性 2 的数组长度是 `harmonic_order`（最高 21 项），这里只列出前 3 项以便阅读，实际报文长度以设备返回为准。

### 读取 A 相总电压谐波含有量

请求（`200D/2/1`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 20 0D 02 01 00 B2 2C 16
```

响应（`Int16` 320，即 3.20 %）：

```text
68 1C 00 C3 05 00 00 00 00 00 00 00 BC FF 85 01 00 20 0D 02 01 01 10 01 40 00 00 18 F3 16
```

| 字段 | 字节 | 含义 |
| --- | --- | --- |
| `85` | | 服务：GET-Response |
| `01` | | GET-Normal |
| `00` | | PIID-ACD=0 |
| `20 0D 02 01` | | 回显 OAD：电压谐波，属性 2（A 相），索引 1（总含有量） |
| `01` | | 选择子：Data |
| `10` | | 数据类型：`Int16` |
| `01 40` | | 原始值 320 |
| `00` / `00` | | FollowReport / TimeTag 不存在 |

### 读取 A 相 2 次电压谐波

请求（`200D/2/2`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 20 0D 02 02 00 DA 06 16
```

响应（`Int16` 125，即 1.25 %）：

```text
68 1C 00 C3 05 00 00 00 00 00 00 00 BC FF 85 01 00 20 0D 02 02 01 10 00 7D 00 00 1C 3E 16
```

### 读取 A 相电压谐波数组

请求（`200D/2/0`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 20 0D 02 00 00 6A 35 16
```

响应（前 3 项 `Int16`：总 320、2 次 125、3 次 0）：

```text
68 24 00 C3 05 00 00 00 00 00 00 00 D9 D0 85 01 00 20 0D 02 00 01 01 03 10 01 40 10 00 7D 10 00 00 00 00 86 B6 16
```

解析：`01`（Data）→ `01`（数组）→ `03`（3 项）：

| 索引 | 原始字节 | 原始值 | 含义 | 工程值 |
| --- | --- | --- | --- | --- |
| 1 | `01 40` | 320 | 总含有量 | 3.20 % |
| 2 | `00 7D` | 125 | 2 次 | 1.25 % |
| 3 | `00 00` | 0 | 3 次 | 0.00 % |

### 读取 B 相 3 次电流谐波

请求（`200E/3/3`，属性 3 = B 相，索引 3 = 3 次）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 20 0E 03 03 00 13 60 16
```

响应（`Int16` 215，即 2.15 %）：

```text
68 1C 00 C3 05 00 00 00 00 00 00 00 BC FF 85 01 00 20 0E 03 03 01 10 00 D7 00 00 DC 8A 16
```

### 读取最高谐波次数

请求（`200D/5/0`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 20 0D 05 00 00 6F B9 16
```

响应（`UInt8` 21）：

```text
68 1B 00 C3 05 00 00 00 00 00 00 00 5A 5F 85 01 00 20 0D 05 00 01 11 15 00 00 29 07 16
```

`15` 十六进制 = 21 十进制，即最高 21 次谐波，**数组长度也是 21**。

### 读取电压波形失真度

请求（`200B/2/0`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 20 0B 02 00 00 F0 7E 16
```

响应（三项 `Int16`：2.15 %、1.98 %、2.07 %）：

```text
68 24 00 C3 05 00 00 00 00 00 00 00 D9 D0 85 01 00 20 0B 02 00 01 01 03 10 00 D7 10 00 C6 10 00 CF 00 00 28 87 16
```

## 相关配置

| 配置 | 位置 | 说明 |
| --- | --- | --- |
| `DeviceLayout::harmonic_order` | 库本地配置 | 数组长度 = `harmonic_order`，范围 2～255，**必须与设备属性 5 一致** |
| 谐波分析次数 `400E` | **电表内部参数** | 电表自己算谐波用到的次数，与属性 5 是两回事，详见[电表参数](./meter-parameters.md) |

## 常见失败

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| 数组长度校验失败 | `harmonic_order` 与设备不一致 | 先读 `200D/5/0`（或 `200E/5/0`）再构造布局 |
| DAR=4 | 设备不提供谐波 | 老型号表通常只有失真度没有各次谐波 |
| `harmonic_oad` 返回 `invalid_value` | `order` 传了 1 | 谐波没有基波项，`order` 只能 0 或 ≥2；也不能超过 `harmonic_order` |
| `harmonic_oad` 对 total 相返回错误 | 谐波对象没有总相 | 相别只能 A/B/C |
| `engineering_values` 报 `non-numeric standard attribute` | 读了属性 5 等无倍率属性 | 直接从 `Data` 取原始整数 |
| 把相别当索引用 `phase_oad` 读谐波 | 谐波的相别在属性上 | 谐波必须用 `harmonic_oad`，失真度才用 `phase_oad` |
| 谐波值全为 0 | 该次谐波确实为零，或设备未测量 | 对照失真度判断：THD 有值而各次为 0 说明设备不提供 |
| 报文比预期长很多 | 数组长度是 21 而非 3 | 这是正常的，示例只列了前 3 项 |