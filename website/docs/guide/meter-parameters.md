---
title: 电表参数读取与设置
description: 时区时段数、最大需量周期、滑差时间、电压合格率参数等电表内部参数；两个决定数组长度的关键参数。
---

# 电表参数读取与设置

## 能读取什么

本页的**全部内容都是电表内部参数**，不是库的连接参数。两者的区别见[配置速查](./configuration.md)。

| 业务含义 | OI | 类型 | 标准可写 | 说明 |
| --- | --- | --- | --- | --- |
| **时区时段数** | `400C` | `Structure<5×UInt8>` | 可写 | 含费率数，**决定电能数组长度** |
| 阶梯数 | `400D` | `UInt8` | 可写 | 电价阶梯个数 |
| 谐波分析次数 | `400E` | `UInt8` | 可写 | 电表算谐波用到的次数 |
| 计量元件数 | `4010` | `UInt8` | 只读 | 允许值 1/2/3 |
| 周休日特征字 | `4012` | `BitString(8)` | 可写 | 8 位位串 |
| 电压合格率参数 | `4030` | `Structure<4×UInt16>` | 可写 | 考核/合格上下限 |
| 最大需量周期 | `4100` | `UInt8` | 可写 | 单位 min |
| 滑差时间 | `4101` | `UInt8` | 可写 | 单位 min |

设备信息类的字符串参数在[设备信息读取](./device-info.md)，时钟参数在[日期时间与校时](./clock.md)。

:::warning 两个参数决定所有数组的长度
`DeviceLayout` 的三个字段中，有两个必须**从设备读回来**：

| `DeviceLayout` 字段 | 从哪个参数读 | 影响 |
| --- | --- | --- |
| `tariff_count` | `400C/2/0` 的第 4 个字段「费率数」 | 所有电能/需量数组长度 = `tariff_count + 1` |
| `harmonic_order` | `200D/5/0`（电压谐波「最高谐波次数」） | 谐波数组长度 = `harmonic_order` |
| `wiring` | `4010/2/0`（计量元件数）间接判断 | 电压/电流长度 3 或 1，功率长度 4 或 2 |

**不读就猜，读取一定失败或数据错位。**
:::

## 数据地址表

### `400C` 时区时段数

| 字段 | 类型 | 上限 | 说明 |
| --- | --- | --- | --- |
| 1 年时区数 | `UInt8` | 14 | 一年分几个时区 |
| 2 日时段表数 | `UInt8` | 8 | 用了几个日时段表 |
| 3 日时段数 | `UInt8` | 14 | 每天几个时段 |
| 4 **费率数** | `UInt8` | 63 | **电能数组长度的来源** |
| 5 公共假日数 | `UInt8` | 254 | 每年节假日数 |

索引可选顶层字段：索引 1～5 分别读这 5 个字段。

### `4030` 电压合格率参数

| 字段 | 类型 | 倍率 | 单位 |
| --- | --- | --- | --- |
| 1 电压考核上限 | `UInt16` | -1 | V |
| 2 电压考核下限 | `UInt16` | -1 | V |
| 3 电压合格上限 | `UInt16` | -1 | V |
| 4 电压合格下限 | `UInt16` | -1 | V |

索引 1～4 对应四个字段，索引 0 读整个结构。

### 单值参数

| 参数 | 索引 | 读取方式 |
| --- | --- | --- |
| `400D`、`4010`、`400E`、`4100`、`4101` | 固定 0 | 普通 GET，`UInt8` |
| `4012` | 固定 0 | 普通 GET，`BitString(8)` |

:::tip 无倍率参数不能用 `engineering_values`
除最大需量周期和滑差时间（有倍率 0，单位 min）外，`400D`、`4010`、`400E` 都是**纯计数，没有倍率**。对它们调用 `engineering_values` 会返回：

```text
non-numeric standard attribute
```

这是设计取舍，不是缺陷。直接用 `data.as<model::UInt8>().value` 取原始整数。
:::

## 最小完整示例

**先读布局参数，再读电能**：

```cpp
#include <dlt698/app.hpp>
#include <dlt698/standard/catalog.hpp>
#include <iostream>

int main() {
    using namespace dlt698;
    namespace oi = standard::oi;

    app::ClientOptions options;
    options.protocol.server.bytes = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    app::Client client(options);
    auto connected = client.connect_tcp("127.0.0.1", 6980);
    if (!connected) {
        std::cerr << connected.error().context << '\n';
        return 1;
    }

    // 第一步：读时区时段数，取出费率数。
    const standard::DeviceLayout provisional{standard::Wiring::three_phase, 4, 21};
    auto periods = standard::make_oad(oi::time_period_counts, 2, 0, provisional);
    if (!periods) {
        std::cerr << periods.error().context << '\n';
        client.disconnect();
        return 1;
    }
    auto periods_value = client.get(periods.value());
    if (!periods_value || std::get_if<std::uint8_t>(&periods_value.value())) {
        std::cerr << "cannot read 400C, assuming 4 tariffs\n";
        // 读不到就退回默认值，但要记录下来
    } else {
        const auto& data = std::get<model::Data>(periods_value.value());
        if (data.type() == model::DataType::structure) {
            const auto& fields = data.as<model::Structure>().value;
            if (fields.size() >= 4 && fields[3].type() == model::DataType::uint8) {
                const std::size_t tariffs = fields[3].as<model::UInt8>().value;
                std::cout << "tariff count = " << tariffs << '\n';
                // 用真实费率数重建布局
                const standard::DeviceLayout layout{standard::Wiring::three_phase, tariffs, 21};
                // ... 后续读取用 layout
            }
        }
    }

    // 第二步：用读到的布局读电能。
    const standard::DeviceLayout layout{standard::Wiring::three_phase, 4, 21};
    auto oad = standard::make_oad(oi::forward_active_energy, 2, 0, layout);
    auto value = client.get(oad.value());
    if (value && !std::get_if<std::uint8_t>(&value.value())) {
        const auto& data = std::get<model::Data>(value.value());
        auto numbers = standard::engineering_values(oad.value(), data, layout);
        if (numbers)
            for (std::size_t i = 0; i < numbers.value().size(); ++i)
                std::cout << "tariff " << i << " = "
                          << standard::decimal_text(numbers.value()[i]) << '\n';
        // 四费率设备：索引 0 总量，1～4 费率
    }

    client.disconnect();
    return 0;
}
```

## 常用变体

### 读最大需量周期和滑差时间

这两个**有倍率**（倍率 0，单位 min）：

```cpp
auto period  = standard::make_oad(oi::maximum_demand_period, 2, 0, layout); // 4100/2/0
auto sliding = standard::make_oad(oi::sliding_interval, 2, 0, layout);      // 4101/2/0

auto result = client.get_list({period.value(), sliding.value()});
if (result) {
    for (const auto& entry : result.value().attributes) {
        if (std::get_if<std::uint8_t>(&entry.result)) continue;
        const auto& data = std::get<model::Data>(entry.result);
        auto numbers = standard::engineering_values(entry.attribute, data, layout);
        if (numbers && !numbers.value().empty())
            std::cout << standard::decimal_text(numbers.value()[0]) << ' '
                      << standard::unit_symbol(numbers.value()[0].scaling.unit) << '\n';
        // 4100 → 15 min，4101 → 1 min
    }
}
```

### 读电压合格率参数

```cpp
auto oad = standard::make_oad(oi::voltage_quality_limits, 2, 0, layout); // 4030/2/0
auto value = client.get(oad.value());
if (value && !std::get_if<std::uint8_t>(&value.value())) {
    const auto& data = std::get<model::Data>(value.value());
    const auto& fields = data.as<model::Structure>().value;
    static const char* kNames[] = {"考核上限", "考核下限", "合格上限", "合格下限"};
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const model::Oad field_oad{oi::voltage_quality_limits, 2, i + 1};
        auto numbers = standard::engineering_values(field_oad, fields[i], layout);
        if (numbers && !numbers.value().empty())
            std::cout << kNames[i] << " = " << standard::decimal_text(numbers.value()[0])
                      << ' ' << standard::unit_symbol(numbers.value()[0].scaling.unit) << '\n';
    }
    // 198.0 V / 162.0 V / 220.0 V / 176.0 V
}
```

### 只读单个上下限

```cpp
auto upper = standard::make_oad(oi::voltage_quality_limits, 2, 3, layout); // 合格上限
auto value = client.get(upper.value());
if (value && !std::get_if<std::uint8_t>(&value.value())) {
    const auto& data = std::get<model::Data>(value.value());
    auto numbers = standard::engineering_values(upper.value(), data, layout);
    if (numbers && !numbers.value().empty())
        std::cout << standard::decimal_text(numbers.value()[0]) << ' '
                  << standard::unit_symbol(numbers.value()[0].scaling.unit) << '\n';
    // UInt16 2200 → 220.0 V
}
```

### 判断接线方式

```cpp
auto oad = standard::make_oad(oi::metering_element_count, 2, 0, layout); // 4010/2/0
auto value = client.get(oad.value());
if (value && !std::get_if<std::uint8_t>(&value.value())) {
    const auto& data = std::get<model::Data>(value.value());
    if (data.type() == model::DataType::uint8) {
        const auto elements = data.as<model::UInt8>().value;
        // 1 → 单相（Wiring::single_phase）
        // 2 或 3 → 三相（Wiring::three_phase）
        const standard::DeviceLayout layout{
            elements == 1 ? standard::Wiring::single_phase : standard::Wiring::three_phase,
            tariffs, 21};
    }
}
```

这是**间接判断**：计量元件数不等同于接线方式，但 1 个元件基本对应单相。

### 写参数

```cpp
// 写最大需量周期为 15 分钟
auto dar = client.set({oi::maximum_demand_period, 2, 0}, model::Data{model::UInt8{15}});
if (!dar) {
    std::cerr << dar.error().context << '\n';
} else if (dar.value() != 0) {
    std::cerr << "meter refused, DAR=" << unsigned(dar.value()) << '\n';
}
```

:::warning 「标准可写」不等于「你能写」
`AttributeDefinition::writable = true` 只表示**标准定义了写入能力**。实际写入要过三关：

1. **客户端 schema**：必须在 `Client` 的运行 schema 里显式开放该属性
2. **设备策略**：设备可能拒绝（如参数越界、厂家锁定）
3. **权限**：现场设备通常需要授权才能改

写入详情见[写参数与执行方法](./write-action.md)。
:::

## 请求与响应报文

以下示例固定：SA 六个 `00` 字节、逻辑地址 0、CA=0、PIID=0、控制域请求 `43`／响应 `C3`。

### 读取时区时段数

请求（`400C/2/0`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 40 0C 02 00 00 62 88 16
```

响应（年时区 1、日时段表 1、日时段 8、费率 4、公共假日 0）：

```text
68 25 00 C3 05 00 00 00 00 00 00 00 48 85 85 01 00 40 0C 02 00 01 02 05 11 01 11 01 11 08 11 04 11 00 00 00 DB 22 16
```

| 字段 | 字节 | 含义 |
| --- | --- | --- |
| `02` | | `Structure` |
| `05` | | 字段数 5 |
| `11 01` | | 年时区数 1 |
| `11 01` | | 日时段表数 1 |
| `11 08` | | 日时段数 8 |
| `11 04` | | **费率数 4** |
| `11 00` | | 公共假日数 0 |

第 4 个字段就是 `DeviceLayout::tariff_count` 的来源。

### 读取阶梯数

请求（`400D/2/0`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 40 0D 02 00 00 D9 94 16
```

响应（`UInt8` 4）：

```text
68 1B 00 C3 05 00 00 00 00 00 00 00 5A 5F 85 01 00 40 0D 02 00 01 11 04 00 00 22 0E 16
```

### 读取谐波分析次数

请求（`400E/2/0`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 40 0E 02 00 00 14 B1 16
```

响应（`UInt8` 21）：

```text
68 1B 00 C3 05 00 00 00 00 00 00 00 5A 5F 85 01 00 40 0E 02 00 01 11 15 00 00 BB 5B 16
```

:::tip `400E` 和 `200D/5` 不一定是同一个值
- `400E`（谐波分析次数）是**电表内部参数**，决定设备自己算到什么次数
- `200D/5`（最高谐波次数）是**谐波对象的属性**，声明该对象数组的长度

标准上它们应一致，但**设备可能填不同的值**。以 `200D/5` 为准构造 `harmonic_order`，因为它直接决定数组长度。
:::

### 读取计量元件数

请求（`4010/2/0`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 40 10 02 00 00 F7 DC 16
```

响应（`UInt8` 3）：

```text
68 1B 00 C3 05 00 00 00 00 00 00 00 5A 5F 85 01 00 40 10 02 00 01 11 03 00 00 82 63 16
```

### 读取周休日特征字

请求（`4012/2/0`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 40 12 02 00 00 81 E5 16
```

响应（`BitString` 8 位）：

```text
68 1C 00 C3 05 00 00 00 00 00 00 00 BC FF 85 01 00 40 12 02 00 01 04 08 00 00 00 FA 9E 16
```

`04` 是 `BitString`，`08` 十六进制 = 8 位，一个字节 `00`。

### 读取电压合格率参数

请求（`4030/2/0`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 40 30 02 00 00 A4 53 16
```

响应（198.0 / 162.0 / 220.0 / 176.0 V）：

```text
68 27 00 C3 05 00 00 00 00 00 00 00 6A 2E 85 01 00 40 30 02 00 01 02 04 12 07 BC 12 06 54 12 08 98 12 06 E0 00 00 0B 55 16
```

| 字段 | 字节 | 原始值 | 工程值 |
| --- | --- | --- | --- |
| 考核上限 | `07 BC` | 1980 | 198.0 V |
| 考核下限 | `06 54` | 1620 | 162.0 V |
| 合格上限 | `08 98` | 2200 | 220.0 V |
| 合格下限 | `06 E0` | 1760 | 176.0 V |

### 读单个上下限

请求（`4030/2/3`，只读合格上限）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 40 30 02 03 00 CC 79 16
```

响应（`UInt16` 2200，即 220.0 V）：

```text
68 1C 00 C3 05 00 00 00 00 00 00 00 BC FF 85 01 00 40 30 02 03 01 12 08 98 00 00 34 9A 16
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

### 读取滑差时间

请求（`4101/2/0`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 41 01 02 00 00 A9 08 16
```

响应（`UInt8` 1，即 1 min）：

```text
68 1B 00 C3 05 00 00 00 00 00 00 00 5A 5F 85 01 00 41 01 02 00 01 11 01 00 00 00 41 16
```

### 写最大需量周期

请求（设为 15）：

```text
68 19 00 43 05 00 00 00 00 00 00 00 AF 30 06 01 00 41 00 02 00 11 0F 00 2B D7 16
```

响应（DAR=3，设备拒绝）：

```text
68 19 00 C3 05 00 00 00 00 00 00 00 78 F4 86 01 00 41 00 02 00 03 00 00 38 C6 16
```

**注意：这次 SET 成功了（帧收发正常），但业务上被拒绝（DAR=3）。**

## 相关配置

| 配置 | 位置 | 说明 |
| --- | --- | --- |
| `DeviceLayout::tariff_count` | 库本地配置 | 从 `400C/2/0` 第 4 字段读，范围 0～254 |
| `DeviceLayout::harmonic_order` | 库本地配置 | 从 `200D/5/0` 读，范围 2～255 |
| `DeviceLayout::wiring` | 库本地配置 | 由 `4010/2/0` 间接判断 |
| `SessionOptions::request_timeout` | 库连接参数 | 单次 GET 超时，**与需量周期无关** |
| 最大需量周期 `4100` | **电表内部参数** | 需量统计窗口 |
| 滑差时间 `4101` | **电表内部参数** | 需量更新步长 |

## 常见失败

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| 电能数组长度校验失败 | `tariff_count` 猜的 | 先读 `400C/2/0` |
| 谐波数组长度校验失败 | `harmonic_order` 猜的 | 先读 `200D/5/0` |
| `engineering_values` 报 `non-numeric standard attribute` | 读了无倍率参数 | 直接取 `data.as<UInt8>().value` |
| 电压合格率参数读成整数 1980 | 忘了倍率 -1 | 1980 是 0.1 V 单位，即 198.0 V |
| 写参数 DAR=3 | 参数越界或设备策略拒绝 | 核对取值范围；现场设备常有写入保护 |
| 写参数 DAR=7 | 类型不匹配 | 如 `OctetString` 参数写成了 `VisibleString` |
| 写参数根本没发出请求 | schema 未开放该属性 | 客户端默认**写入权限关闭**，见[写参数与执行方法](./write-action.md) |
| 400E 与 200D/5 值不同 | 设备填了不同的值 | 以 `200D/5` 为准，它直接决定数组长度 |