---
title: 需量读取
description: 正反向有功、组合无功、四象限无功和视在的最大需量及发生时间；当前需量；需量周期与滑差时间。
---

# 需量读取

## 能读取什么

需量是**一段时间内的最大负荷值**，与电能不同，它带一个**发生时间**：这段时间内哪一刻达到了这个最大值。

本库收录两类需量：

| 类别 | 业务含义 | 是否带发生时间 | 页面 |
| --- | --- | --- | --- |
| **最大需量** | 结算周期内的历史最大值 | 是，结构 `{数值, 发生时间}` | 本页 |
| **当前需量** | 当前需量周期内的实时值 | 否，单个数值 | 本页 |

最大需量按方向和无功类型分成 10 个家族，与[电能](./energy.md)家族一一对应。

**需量的数值精度比电能高**（倍率 -4，即 4 位小数），但**电能对象没有"扩展精度"属性**，需量固定就是这个精度。

## 数据地址表

最大需量对象与电能对象编号规律一致，索引含义也相同（0 = 整体，1 = 总量，2～n+1 = 费率）。

| 家族 | OI（总、A、B、C） | 单位 |
| --- | --- | --- |
| 正向有功最大需量 | `1010` `1011` `1012` `1013` | kW |
| 反向有功最大需量 | `1020` `1021` `1022` `1023` | kW |
| 组合无功 1 最大需量 | `1030` `1031` `1032` `1033` | kvar |
| 组合无功 2 最大需量 | `1040` `1041` `1042` `1043` | kvar |
| 第一象限最大需量 | `1050` `1051` `1052` `1053` | kvar |
| 第二象限最大需量 | `1060` `1061` `1062` `1063` | kvar |
| 第三象限最大需量 | `1070` `1071` `1072` `1073` | kvar |
| 第四象限最大需量 | `1080` `1081` `1082` `1083` | kvar |
| 正向视在最大需量 | `1090` `1091` `1092` `1093` | kVA |
| 反向视在最大需量 | `10A0` `10A1` `10A2` `10A3` | kVA |

每个最大需量对象收录的属性：

| 属性 | 名称 | 类型 | 说明 |
| --- | --- | --- | --- |
| 1 | 逻辑名 | `OctetString` | 通常不读 |
| 2 | 总及费率最大需量 | Array\<Structure\> | 元素是 `{Int32/UInt32 数值, DateTimeS 发生时间}` |
| 3 | 换算及单位 | `ScalerUnit` | 设备声明的倍率和单位 |

数组长度 = `tariff_count + 1`。属性 2、3 **只读**。

> **符号**：有功、视在、四象限需量用 `UInt32`；组合无功 1/2 需量用 `Int32`（可能为负）。不要一律按无符号解析。

当前需量是单值对象，属性 2 就是数值本身，没有发生时间：

| 业务含义 | OI | 类型 | 倍率 | 单位 |
| --- | --- | --- | --- | --- |
| 当前有功需量 | `2017` | `Int32` | -4 | kW |
| 当前无功需量 | `2018` | `Int32` | -4 | kvar |
| 当前视在需量 | `2019` | `Int32` | -4 | kVA |

影响需量计算的**电表内部参数**（不是库的连接参数，见[配置速查](./configuration.md)）：

| 业务含义 | OI | 类型 | 倍率 | 单位 |
| --- | --- | --- | --- | --- |
| 最大需量周期 | `4100` | `UInt8` | 0 | min |
| 滑差时间 | `4101` | `UInt8` | 0 | min |

## 最小完整示例

读正向有功总需量，打印数值和发生时间：

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

    // 正向有功总需量：1010/2/1
    auto oad = standard::demand_oad(oi::forward_active_maximum_demand, standard::Phase::total, 0,
                                    layout);
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
        std::cerr << "meter does not provide maximum demand, DAR=" << unsigned(*dar) << '\n';
        client.disconnect();
        return 1;
    }

    // demand_values 校验结构并同时取出数值和原始发生时间。
    const auto& data = std::get<model::Data>(value.value());
    auto readings = standard::demand_values(oad.value(), data, layout);
    if (!readings || readings.value().empty()) {
        std::cerr << "unexpected maximum demand payload\n";
        client.disconnect();
        return 1;
    }
    const auto& demand = readings.value()[0];
    std::cout << "max demand = " << standard::decimal_text(demand.number) << ' '
              << standard::unit_symbol(demand.number.scaling.unit) << '\n';
    // DateTimeS 是 std::array<std::uint8_t,7>：年(2 字节 大端) 月 日 时 分 秒 毫秒
    const auto& t = demand.occurred_at.value;
    std::cout << "occurred at " << unsigned(t[0]) * 256 + unsigned(t[1]) << '-'
              << unsigned(t[2]) << '-' << unsigned(t[3]) << ' ' << unsigned(t[4]) << ':'
              << unsigned(t[5]) << '\n';
    // 输出：
    // max demand = 1.2345 kW
    // occurred at 2026-10-5 12:30

    client.disconnect();
    return 0;
}
```

:::tip 时间不做日历转换
`demand_values` 返回的 `occurred_at` 是**协议里的原始 `DateTimeS`**，字段是 `std::array<std::uint8_t, 7>`：前两字节是大端年份，接着月、日、时、分、秒、毫秒。库里不做时区换算、不转成 `time_t`、也不校验日期合法性，要本地化请自己组装。
:::

## 常用变体

### 一次读总量和所有费率

索引 0 读整个数组，输出顺序是总量、费率 1～n：

```cpp
auto oad = standard::make_oad(standard::oi::forward_active_maximum_demand, 2, 0, layout);
auto value = client.get(oad.value());
if (value && !std::get_if<std::uint8_t>(&value.value())) {
    const auto& data = std::get<model::Data>(value.value());
    auto readings = standard::demand_values(oad.value(), data, layout);
    if (readings) {
        for (std::size_t i = 0; i < readings.value().size(); ++i)
            std::cout << (i == 0 ? "total" : "tariff " + std::to_string(i)) << " = "
                      << standard::decimal_text(readings.value()[i].number) << '\n';
    }
}
```

### 分相需量

相别在 OI 里，与电能一致：

```cpp
auto total = standard::demand_oad(oi::forward_active_maximum_demand, standard::Phase::total, 0, layout);
auto a     = standard::demand_oad(oi::forward_active_maximum_demand, standard::Phase::a, 0, layout);
auto b     = standard::demand_oad(oi::forward_active_maximum_demand, standard::Phase::b, 0, layout);
auto c     = standard::demand_oad(oi::forward_active_maximum_demand, standard::Phase::c, 0, layout);
// 1010/2/1、1011/2/1、1012/2/1、1013/2/1

auto result = client.get_list({total.value(), a.value(), b.value(), c.value()});
```

### 读取当前需量

当前需量是单值对象，没有发生时间，用 `engineering_values` 即可：

```cpp
auto oad = standard::make_oad(standard::oi::current_active_demand, 2, 0, layout);
auto value = client.get(oad.value());
if (value && !std::get_if<std::uint8_t>(&value.value())) {
    const auto& data = std::get<model::Data>(value.value());
    auto numbers = standard::engineering_values(oad.value(), data, layout);
    if (numbers && !numbers.value().empty())
        std::cout << standard::decimal_text(numbers.value()[0]) << ' '
                  << standard::unit_symbol(numbers.value()[0].scaling.unit) << '\n';
    // 线上 Int32 12345 → 1.2345 kW
}
```

同时读三个当前需量：

```cpp
auto active   = standard::make_oad(standard::oi::current_active_demand, 2, 0, layout);
auto reactive = standard::make_oad(standard::oi::current_reactive_demand, 2, 0, layout);
auto apparent = standard::make_oad(standard::oi::current_apparent_demand, 2, 0, layout);
auto result = client.get_list({active.value(), reactive.value(), apparent.value()});
```

### 确认需量周期

最大需量周期决定需量的统计窗口，**是电表自己的参数**，读它用普通 GET：

```cpp
auto oad = standard::make_oad(standard::oi::maximum_demand_period, 2, 0, layout);
auto value = client.get(oad.value());
if (value && !std::get_if<std::uint8_t>(&value.value())) {
    const auto& data = std::get<model::Data>(value.value());
    auto numbers = standard::engineering_values(oad.value(), data, layout);
    if (numbers && !numbers.value().empty())
        std::cout << standard::decimal_text(numbers.value()[0]) << ' '
                  << standard::unit_symbol(numbers.value()[0].scaling.unit) << '\n';
    // UInt8 15 → 15 min
}
```

写这个参数要走 SET，见[写参数与执行方法](./write-action.md)。

## 请求与响应报文

以下示例固定：SA 六个 `00` 字节、逻辑地址 0、CA=0、PIID=0、控制域请求 `43`／响应 `C3`。

### 读取正向有功总需量

请求（`1010/2/1`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 10 10 02 01 00 4D B0 16
```

响应：

```text
68 28 00 C3 05 00 00 00 00 00 00 00 26 32 85 01 00 10 10 02 01 01 02 02 06 00 00 30 39 1C 07 EA 0A 05 0C 1E 00 00 00 62 D3 16
```

| 字段 | 字节 | 含义 |
| --- | --- | --- |
| `85` | | 服务：GET-Response |
| `01` | | GET-Normal |
| `00` | | PIID-ACD=0 |
| `10 10 02 01` | | 回显 OAD |
| `01` | | 选择子：Data |
| `02` | | 数据类型：`Structure` |
| `02` | | 字段数：2 |
| `06 00 00 30 39` | | 字段 1：`UInt32` 12345（最大值） |
| `1C` | | 字段 2：`DateTimeS` |
| `07 EA` | | 年 2026 |
| `0A 05` | | 月 10 日 5 |
| `0C 1E` | | 时 12 分 30 |
| `00` | | 秒 0 |
| `00` / `00` | | FollowReport / TimeTag 不存在 |

`00 00 30 39` = 12345，乘以 10^-4 得 **1.2345 kW**；发生时间 **2026-10-05 12:30:00**。

### 读取整个需量数组

请求（`1010/2/0`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 10 10 02 00 00 95 A9 16
```

响应（总量 12345，费率 1～4 中列出前两项）：

```text
68 39 00 C3 05 00 00 00 00 00 00 00 F2 16 85 01 00 10 10 02 00 01 01 02 02 02 06 00 00 30 39 1C 07 EA 0A 05 0C 1E 00 02 02 06 00 00 1F 40 1C 07 EA 0A 05 0E 00 00 00 00 E7 70 16
```

解析：结果 `01` → 数组长度 `02`（两项）→ 每项都是 `Structure`，字段数 `02`：

| 位置 | 数值 | 发生时间 | 工程值 |
| --- | --- | --- | --- |
| 总量 | `00 00 30 39` = 12345 | 2026-10-05 12:30:00 | 1.2345 kW |
| 费率 1 | `00 00 1F 40` = 8000 | 2026-10-05 09:15:00 | 0.8000 kW |

数组长度由**请求方**的 `tariff_count` 校验。如果设备返回 5 项而你按四费率配置校验，读取会失败——见[常见失败](#常见失败)。

### 读取当前有功需量

请求（`2017/2/0`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 20 17 02 00 00 65 2A 16
```

响应（`Int32` 12345，即 1.2345 kW）：

```text
68 1E 00 C3 05 00 00 00 00 00 00 00 9E 54 85 01 00 20 17 02 00 01 05 00 00 30 39 00 00 13 6C 16
```

注意这里**没有 `Structure` 包装**，选择子后直接是数据类型 `05`（`Int32`）和值 —— 当前需量不带发生时间。

### 读取当前无功需量

请求（`2018/2/0`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 20 18 02 00 00 9C 98 16
```

### 读取当前视在需量

请求（`2019/2/0`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 20 19 02 00 00 27 84 16
```

### 读取最大需量周期

请求（`4100/2/0`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 41 00 02 00 00 12 14 16
```

响应（`UInt8` 15，即 15 min）：

```text
68 1B 00 C3 05 00 00 00 00 00 00 00 5A 5F 85 01 00 41 00 02 00 01 11 0F 00 00 A4 D0 16
```

## 相关配置

| 配置 | 位置 | 说明 |
| --- | --- | --- |
| `DeviceLayout::tariff_count` | 库本地配置 | 需量数组长度 = `tariff_count + 1`，必须与设备实际费率数一致 |
| `DeviceLayout::wiring` | 库本地配置 | 单相配置下 B/C 相需量对象不可用 |
| `SessionOptions::request_timeout` | 库连接参数 | 单次 GET 的超时，默认 5 秒。与需量周期无关 |
| 最大需量周期 `4100` | **电表内部参数** | 需量统计窗口，通常 15 或 30 min |
| 滑差时间 `4101` | **电表内部参数** | 需量更新的滑差步长 |

## 常见失败

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| DAR=4 | 设备不提供该需量对象 | 老型号表可能只算有功需量，无功/视在需量需查设备点表 |
| DAR=2 | 设备认为参数越界 | 检查属性号与索引是否匹配该型号 |
| `demand_values` 返回数组长度不匹配 | `tariff_count` 与设备不一致 | 先读 `400C/2/0` 的费率数字段，再构造 `DeviceLayout` |
| `demand_values` 返回 `unsupported_tag` | 把当前需量当成了最大需量 | 当前需量用 `engineering_values`，不要用 `demand_values` |
| 结构解析出负值但期望正数 | 组合无功需量是 `Int32` | 有符号家族按 `Int32` 解析 |
| 单相表读 B/C 相报 `invalid_value` | 单相配置没有 B/C 对象 | `phase_oad`/`demand_oad` 会直接拒绝，属正常 |
| 需量值长期不变 | 需量周期未到，或设备未到结算点 | 读 `4100` 确认周期；跨结算点才会产生新的最大值 |