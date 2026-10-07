---
title: 看懂数据地址
description: OI、属性、索引三个字段的含义，以及如何选择整体、单相和单费率。
---

# 看懂数据地址

DL/T 698.45 里每一个读取请求都带一个三字节的地址。它决定读到的是什么，必须逐字段理解。

## 三个字段

```text
00 10 02 01
└┬─┘ └┬┘ └┬┘
 │   │   └── 元素索引
 │   └────── 属性编号（含特征位）
 └────────── 对象标识 OI
```

**OI（对象标识）** — 两位，指这是什么对象。`0010` 是正向有功电能，`2000` 是电压，`4000` 是日期时间。同一个 OI 下的所有数据由属性区分。

**属性编号** — 一位的低五位是属性号，高三位是特征位。本库的辅助构造接口只支持特征零（字节 `0x02` 表示属性 2），厂家自定义特征不要用辅助接口，手写 `Oad` 并配合高级分层接口。

**元素索引** — 选择属性内部的第几项。**索引 0 表示整个属性，非零从 1 开始。** 这是最容易搞错的地方：索引 0 不是"总量"，而是"完整数组"。

```cpp
model::Oad{0x0010, 2, 0}   // 200F/2/0：正向有功电能的完整属性（总量+费率数组）
model::Oad{0x0010, 2, 1}   // 0010/2/1：仅总量，UInt32
model::Oad{0x0010, 2, 2}   // 0010/2/2：仅费率 1，UInt32
```

## 用辅助接口代替手写

手写 OAD 容易写错属性号或越界。`dlt698::standard` 提供了一组构造接口，它们会校验并返回错误：

```cpp
#include <dlt698/standard/catalog.hpp>
using namespace dlt698;
namespace oi = standard::oi;

// 设备布局：必须与实际设备一致
const standard::DeviceLayout layout{standard::Wiring::three_phase, 4, 21};

// 电压：整体数组和分相
auto all_voltage = standard::phase_oad(oi::voltage, standard::Phase::total, layout);
auto a_voltage    = standard::phase_oad(oi::voltage, standard::Phase::a, layout);
auto b_voltage    = standard::phase_oad(oi::voltage, standard::Phase::b, layout);
auto c_voltage    = standard::phase_oad(oi::voltage, standard::Phase::c, layout);

// 电能：家族 + 相别 + 费率
auto total_energy = standard::energy_oad(oi::forward_active_energy, standard::Phase::total, 0, layout);
auto a_energy     = standard::energy_oad(oi::forward_active_energy, standard::Phase::a, 0, layout);
auto tariff2      = standard::energy_oad(oi::forward_active_energy, standard::Phase::total, 2, layout);
auto hp_energy    = standard::energy_oad(oi::forward_active_energy, standard::Phase::total, 0, layout, true);

// 需量
auto total_demand = standard::demand_oad(oi::forward_active_maximum_demand, standard::Phase::total, 0, layout);

// 谐波
auto a_harmonic_total  = standard::harmonic_oad(oi::voltage_harmonics, standard::Phase::a, 0, layout);
auto b_harmonic_2      = standard::harmonic_oad(oi::voltage_harmonics, standard::Phase::b, 2, layout);

// 通用
auto oad = standard::make_oad(oi::frequency, 2, 0, layout);

// 每个 Result 都要检查，失败时 error().context 说明原因
if (!a_voltage) {
    std::cerr << a_voltage.error().context << '\n';
}
```

这些接口的好处不只是拼字节：它们按你的 `DeviceLayout` 校验相别和费率合法性，配置和实际不符时直接返回错误，而不是生成一个语义错误的地址。

## 相别的两种表达方式

**分相在 OI 里** —— 电能和需量。每个相别是独立的对象：`0010`（总量）、`0011`（A 相）、`0012`（B 相）、`0013`（C 相）。这时相别不出现在索引里，`energy_oad(..., Phase::a, ...)` 会自动换成 `0011`。

**分相在索引里** —— 电压、电流、功率、功率因数、谐波。这些对象只有一个 OI，数组顺序表示相别：

| 对象 | 布局 | 索引 1 | 索引 2 | 索引 3 |
| --- | --- | --- | --- | --- |
| 电压 `2000`、电流 `2001`、波形失真度 `200B`/`200C` | 只有相别 | A | B | C |
| 有功 `2004`、无功 `2005`、视在 `2006`、功率因数 `200A` | 总量+相别 | 总 | A | B、C |

注意功率类有"总"：索引 1 是总功率，索引 2～4 才是 A/B/C。电压电流类没有总量概念，索引 1 直接是 A 相。

## 费率索引

电能的属性 2 是一个数组：**索引 1 是总量，索引 2～n 是费率 1～n-1**。

```text
四费率设备的属性 2 数组（长度 5）：
索引 1 → 总量
索引 2 → 费率 1
索引 3 → 费率 2
索引 4 → 费率 3
索引 5 → 费率 4
```

`tariff_oad(oi, tariff, ...)` 的 `tariff` 参数从 0 开始（0 是总量），内部转换成索引 `tariff + 1`。用 `energy_oad`/`demand_oad` 时也是传 0 表示总量。

数组长度等于 `tariff_count + 1`，其中 `tariff_count` 来自 `DeviceLayout`。**这个值必须与设备的实际费率数一致**，否则读取时数组长度校验会失败。

## DeviceLayout：必须按设备配置

```cpp
struct DeviceLayout {
    Wiring wiring = Wiring::three_phase;   // 三相或单相
    std::size_t tariff_count = 4;         // 费率数 0～254，不含总量
    std::size_t harmonic_order = 21;      // 最高谐波次数 2～255
};
```

**默认三相、四费率、21 次谐波，这些只是默认值，不代表你的设备。** 配置错误会导致：

| 配置不匹配 | 现象 |
| --- | --- |
| 费率数不对 | 电能数组长度校验失败，`invalid_length` |
| 单相设备按三相查 | B/C 相对象返回配置错误 |
| 谐波次数不对 | 谐波数组长度不符，或查一个设备不存在的次数 |
| 单相查 B/C 相 | `invalid_value`，"standard phase unavailable" |

单相设备还有区别：单相电压/电流数组只有一项（A 相，即"某一相"），单相功率数组有两项（总量和 A 相）。**单相配置不受理 B/C 相的电能和需量对象。**

## 扩展精度：属性 4

电能对象除属性 2 外还收录属性 4（扩展精度），类型 `UInt64`/`Int64`，倍率 -4。

```cpp
auto normal = standard::tariff_oad(oi::forward_active_energy, 0, layout, false);  // 0010/2/1
auto precise = standard::tariff_oad(oi::forward_active_energy, 0, layout, true);  // 0010/4/1
```

两者的属性号不同，是两个独立属性。**不是所有设备都提供属性 4**，不支持时返回 DAR=4。`UInt64` 最大值配倍率 -4 可以精确表示 4 位小数的完整 64 位整数，`decimal_text()` 直接插入小数点，不经过浮点。

## 查元数据

不确定某个 OI 的属性、类型、倍率时，查目录：

```cpp
const auto* object = standard::find_object(oi::forward_active_energy);
if (object) {
    std::cout << object->name << " interface=" << unsigned(object->class_id) << '\n';
    for (const auto& attribute : object->attributes)
        std::cout << attribute.number << ' ' << attribute.name
                  << " readable=" << attribute.readable
                  << " writable=" << attribute.writable << '\n';
}

const auto* definition = standard::find_attribute({oi::voltage, 2, 0});
if (definition && definition->scaling) {
    std::cout << "scaler=" << int(definition->scaling.scaler)
              << " unit=" << standard::unit_symbol(definition->scaling.unit) << '\n';
}
```

完整目录也可以直接遍历 `standard::objects()`。这些接口在客户端不需要创建会话，属于 `dlt698::core`。

## 校验地址和取值

`make_oad` 校验 OAD 是否有效，`validate_oad` 校验类型和索引范围。收到数据后用 `validate_value` 校验结构，用 `engineering_values` 换算：

```cpp
auto valid = standard::validate_value(oad, data, layout);
if (!valid) {
    std::cerr << valid.error().context << '\n';  // 类型不符、数组长度错、字段越界
}
auto numbers = standard::engineering_values(oad, data, layout);
```

校验失败和"设备不支持"是两回事：前者说明数据来了但不符合标准定义，后者是设备根本没提供这个属性。

## 常见错误

**属性 1 和 2 搞混。** 属性 1 是逻辑名（`OctetString`），属性 2 才是数据。读电能值用 `{oi, 2, 0}`，读逻辑名用 `{oi, 1, 0}`。

**索引 0 当成总量。** 索引 0 是整个数组。要只读总量用索引 1。数组长度要匹配 `DeviceLayout.tariff_count + 1`，读整体时校验会检查。

**地址字节按主机序填。** `ServerAddress::bytes` 按线序保存，低有效字节在前。`123456789012` 是 `{0x12, 0x90, 0x78, 0x56, 0x34, 0x12}`。

**用 `.value()` 不检查 Result。** 辅助接口失败时返回错误，`value()` 是未定义行为。

**厂家扩展 OI 用标准接口。** 本库辅助构造只覆盖已收录的标准对象，厂家自定义对象手写 `Oad` 并走通用 API。

## 下一步

- 各数据类别的具体地址表：从[使用指南首页](./index.md)进入
- 参数对象的完整属性和结构：[电表参数读取与设置](./meter-parameters.md)
- 完整目录和元数据说明：[标准固定点位](../protocol/standard-points.md)