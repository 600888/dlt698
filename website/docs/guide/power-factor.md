---
title: 功率因数读取
description: 总及三相功率因数、数组布局、符号含义与单位换算。
---

# 功率因数读取

## 能读取什么

功率因数是**瞬时量**，表示有功与视在之比。

| 业务含义 | OI | 类型 | 倍率 | 单位 |
| --- | --- | --- | --- | --- |
| 功率因数 | `200A` | `Int16` | -3 | 无 |

与功率对象一样，数组是 **总量 + 三相**，长度 = `wiring == three_phase ? 4 : 2`。

```cpp
standard::phase_oad(standard::oi::power_factor, standard::Phase::total, layout); // 200A/2/1
standard::phase_oad(standard::oi::power_factor, standard::Phase::a, layout);     // 200A/2/2
```

## 数据地址表

| 属性 | 名称 | 类型 | 倍率 | 单位 | 读写 |
| --- | --- | --- | --- | --- | --- |
| 1 | 逻辑名 | `OctetString` | — | — | 只读 |
| 2 | 数值 | Array\<`Int16`\> | -3 | 无量纲 | 只读 |
| 3 | 换算及单位 | `ScalerUnit` | — | — | 只读 |

:::warning 功率因数是 `Int16`，符号有明确含义
线上值 `894` 是 **0.894**（倍率 -3），`1000` 就是 1.000。符号含义：

| 符号 | 业务含义 |
| --- | --- |
| 正值 | 感性（滞后），电流滞后于电压 |
| 负值 | 容性（超前），电流超前于电压 |

**没有符号的功率因数在工程上是无意义的**，只取绝对值会丢失容性/感性判断。用 `Int16` 解析，不要用 `UInt16`。
:::

**单位枚举是 255（无单位）**，`unit_symbol` 会返回空字符串，这是正确行为，不是缺陷。输出时自己补 `PF` 或留空。

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

    auto oad = standard::make_oad(oi::power_factor, 2, 0, layout);
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
        std::cerr << "meter does not provide power factor, DAR=" << unsigned(*dar) << '\n';
        client.disconnect();
        return 1;
    }

    const auto& data = std::get<model::Data>(value.value());
    auto numbers = standard::engineering_values(oad.value(), data, layout);
    if (!numbers) {
        std::cerr << numbers.error().context << '\n';
        client.disconnect();
        return 1;
    }
    static const char* kLabels[] = {"total", "A", "B", "C"};
    for (std::size_t i = 0; i < numbers.value().size(); ++i)
        std::cout << "power factor " << kLabels[i] << " = "
                  << standard::decimal_text(numbers.value()[i]) << '\n';
    // 输出：
    // power factor total = 0.894
    // power factor A = 0.880
    // power factor B = -0.880
    // power factor C = 0.891

    client.disconnect();
    return 0;
}
```

## 常用变体

### 只读总功率因数

```cpp
auto oad = standard::phase_oad(oi::power_factor, standard::Phase::total, layout); // 200A/2/1
auto value = client.get(oad.value());
```

### 判断容性还是感性

`ScaledNumber::raw` 是 `variant<int64_t, uint64_t>`，功率因数落在有符号分支：

```cpp
auto numbers = standard::engineering_values(oad.value(), data, layout);
if (numbers && !numbers.value().empty()) {
    const auto& pf = numbers.value()[0];
    // decimal_text 保留符号，直接看文本首字符最省事
    const std::string text = standard::decimal_text(pf);
    const bool capacitive = !text.empty() && text.front() == '-';
    std::cout << (capacitive ? "capacitive" : "inductive") << '\n';
}
```

### 与功率交叉校验

```cpp
// 有功 2010.0 W、视在 6500.0 VA → 2010/6500 ≈ 0.309
// 设备读出的 200A 才是权威值
```

两者偏差超过几个百分点时，先确认功率的单位和倍率（W 还是 kW），再看设备是否配置了错误的互感器变比。

## 请求与响应报文

以下示例固定：SA 六个 `00` 字节、逻辑地址 0、CA=0、PIID=0、控制域请求 `43`／响应 `C3`。

### 读取总功率因数

请求（`200A/2/1`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 20 0A 02 01 00 93 7B 16
```

响应（`Int16` 894，即 0.894）：

```text
68 1C 00 C3 05 00 00 00 00 00 00 00 BC FF 85 01 00 20 0A 02 01 01 10 03 7E 00 00 39 B5 16
```

| 字段 | 字节 | 含义 |
| --- | --- | --- |
| `85` | | 服务：GET-Response |
| `01` | | GET-Normal |
| `00` | | PIID-ACD=0 |
| `20 0A 02 01` | | 回显 OAD：功率因数，属性 2，索引 1（总量） |
| `01` | | 选择子：Data |
| `10` | | 数据类型：`Int16` |
| `03 7E` | | 原始值 894 |
| `00` / `00` | | FollowReport / TimeTag 不存在 |

`03 7E` = 894，乘以 10^-3 得 **0.894**。数据类型 `10` 是 `Int16`，与功率的 `05`（`Int32`）不同。

### 读取功率因数数组

请求（`200A/2/0`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 20 0A 02 00 00 4B 62 16
```

响应（四项 `Int16`）：

```text
68 27 00 C3 05 00 00 00 00 00 00 00 6A 2E 85 01 00 20 0A 02 00 01 01 04 10 03 7E 10 03 70 10 FC 90 10 03 7B 00 00 82 96 16
```

解析：`01`（Data）→ `01`（数组）→ `04`（4 项）：

| 索引 | 原始字节 | 原始值 | 工程值 | 性质 |
| --- | --- | --- | --- | --- |
| 1 总量 | `03 7E` | 894 | 0.894 | 感性 |
| 2 A 相 | `03 70` | 880 | 0.880 | 感性 |
| 3 B 相 | `FC 90` | -880 | -0.880 | 容性 |
| 4 C 相 | `03 7B` | 891 | 0.891 | 感性 |

B 相的 `FC 90` 是**补码表示的 -880**（`0x10000 - 0x0390 = 0xFC90`），这正是符号位生效的地方。`decimal_text` 会输出 `-0.880`。

### 读取 A 相功率因数

请求（`200A/2/2`，索引 2 = A 相）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 20 0A 02 02 00 FB 51 16
```

## 相关配置

| 配置 | 位置 | 说明 |
| --- | --- | --- |
| `DeviceLayout::wiring` | 库本地配置 | 三相为 4 项，单相为 2 项 |
| 互感器变比 | **电表内部配置** | 不在 698 点表里，是设备出厂参数；变比错会导致功率和功率因数都不对 |

## 常见失败

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| 负功率因数显示为正 | 按 `UInt16` 解析 | `Int16` 补码，必须用有符号类型 |
| 值显示为 894 而非 0.894 | 忘了倍率 -3 | 走 `engineering_values` 或自己乘 10^-3 |
| `unit_symbol` 返回空 | 单位枚举是 255（无单位） | 功率因数无量纲，属正常 |
| 数组只有 2 项 | 单相表 | `Wiring::single_phase` 下长度是 2 |
| 与 P/S 算出的值差几个百分点 | 互感器变比或采样不同步 | 以设备读数为准，检查设备配置 |