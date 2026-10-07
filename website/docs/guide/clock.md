---
title: 日期时间与校时
description: 读取表计日期时间、校时模式、精准校时参数和时钟源；写时钟的注意事项。
---

# 日期时间与校时

## 能读取什么

时钟信息分布在三个对象上：

| 业务含义 | OI | 属性 | 类型 | 说明 |
| --- | --- | --- | --- | --- |
| 日期时间 | `4000` | 2 | `DateTimeS` | 表计当前时钟 |
| 校时模式 | `4000` | 3 | `Enum` | 不调整 / 允许校时 / 其他 |
| 精准校时参数 | `4000` | 4 | `Structure<5×UInt8>` | 对时心跳过滤参数 |
| 时钟源 | `4006` | 2 | `Structure<Enum,Enum>` | 当前时钟源及状态 |

**`4000` 的属性 2、3、4 是同一个对象上的不同属性**，属性 2 是最常用的读时钟入口。

## 数据地址表

### `4000` 日期时间对象

| 属性 | 名称 | 类型 | 读写 | 说明 |
| --- | --- | --- | --- | --- |
| 1 | 逻辑名 | `OctetString` | 只读 | 通常不读 |
| 2 | 日期时间 | `DateTimeS` | **可写** | 7 字节 |
| 3 | 校时模式 | `Enum` | **可写** | 允许值 `0, 1, 2, 255` |
| 4 | 精准校时参数 | `Structure` | **可写** | 5 个 `UInt8` 字段 |

三个属性索引都固定 0。

### `DateTimeS` 的 7 个字节

| 字节 | 含义 | 取值 |
| --- | --- | --- |
| 0～1 | 年，大端 | 0～9999，如 2026 = `07 EA` |
| 2 | 月 | 1～12 |
| 3 | 日 | 1～31 |
| 4 | 时 | 0～23 |
| 5 | 分 | 0～59 |
| 6 | 秒 | 0～59 |

**只有 6 个语义字节加一个毫秒字节**。实际上协议布局是「年(2) 月 日 时 分 秒 毫秒」共 7 字节。

:::warning 未指定值与时区
协议用 `FF`/`FFFF` 表示"未指定"。例如 `FF FF` 年表示年份未知。库里：

- **不做时区转换**，不做夏令时处理
- **不转成 `time_t`**，不校验日期合法性（2 月 31 日也能构造出来）
- 保留原始字节，需要自己判断是否为"未指定"

要本地化请自行按年月日时分秒组装。
:::

### 精准校时参数（属性 4）

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| 1 最近心跳总个数 | `UInt8` | 采集的心跳总数 |
| 2 最大值剔除个数 | `UInt8` | 剔除偏大的样本数 |
| 3 最小值剔除个数 | `UInt8` | 剔除偏小的样本数 |
| 4 通讯延时阈值 | `UInt8` | 单位 s（倍率 0） |
| 5 最少有效个数 | `UInt8` | 剔除后至少要剩的样本数 |

**索引可选顶层结构字段**：索引 1～5 分别读这 5 个字段，索引 0 读整个结构。

### 时钟源（`4006`）

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| 1 时钟源 | `Enum` | 协议枚举，最大 4 |
| 2 状态 | `Enum` | 最大 1 |

## 最小完整示例

读取表计时钟并输出：

```cpp
#include <dlt698/app.hpp>
#include <dlt698/standard/catalog.hpp>
#include <iomanip>
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

    auto oad = standard::make_oad(oi::date_time, 2, 0, layout); // 4000/2/0
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
        std::cerr << "meter does not provide date time, DAR=" << unsigned(*dar) << '\n';
        client.disconnect();
        return 1;
    }

    const auto& data = std::get<model::Data>(value.value());
    if (data.type() != model::DataType::date_time_s) {
        std::cerr << "unexpected type\n";
        client.disconnect();
        return 1;
    }
    // DateTimeS 是 std::array<std::uint8_t,7>：年(2 大端) 月 日 时 分 秒 毫秒
    const auto& t = data.as<model::DateTimeS>().value;
    const unsigned year = unsigned(t[0]) * 256 + unsigned(t[1]);
    // FF 表示未指定，年份为 0xFFFF 时说明设备没提供有效时钟
    const bool unspecified = year == 0xFFFFu;

    std::cout << std::setfill('0') << std::setw(4) << year << '-'
              << std::setw(2) << unsigned(t[2]) << '-' << std::setw(2) << unsigned(t[3]) << ' '
              << std::setw(2) << unsigned(t[4]) << ':' << std::setw(2) << unsigned(t[5]) << ':'
              << std::setw(2) << unsigned(t[6]) << '\n';
    if (unspecified) std::cerr << "warning: clock not set\n";
    // 输出：2026-10-07 14:16:26

    client.disconnect();
    return 0;
}
```

## 常用变体

### 读校时模式

```cpp
auto oad = standard::make_oad(oi::date_time, 3, 0, layout); // 4000/3/0
auto value = client.get(oad.value());
if (value && !std::get_if<std::uint8_t>(&value.value())) {
    const auto& data = std::get<model::Data>(value.value());
    if (data.type() == model::DataType::enumeration) {
        const auto mode = data.as<model::Enum>().value;
        switch (mode) {
        case 0: std::cout << "no adjustment\n"; break;
        case 1: std::cout << "allowed to be set\n"; break;
        case 2: std::cout << "other mode\n"; break;
        case 255: std::cout << "not configured\n"; break;
        default: std::cout << "unknown mode " << unsigned(mode) << '\n';
        }
    }
}
```

枚举**没有倍率**，不要用 `engineering_values`。

### 读精准校时参数

整个结构：

```cpp
auto oad = standard::make_oad(oi::date_time, 4, 0, layout); // 4000/4/0
```

单个字段（索引 1～5）：

```cpp
auto threshold = standard::make_oad(oi::date_time, 4, 4, layout); // 4000/4/4 通讯延时阈值
auto value = client.get(threshold.value());
if (value && !std::get_if<std::uint8_t>(&value.value())) {
    const auto& data = std::get<model::Data>(value.value());
    // 只有通讯延时阈值有倍率（0，单位 s），其余字段无倍率。
    auto numbers = standard::engineering_values(threshold.value(), data, layout);
    if (numbers && !numbers.value().empty())
        std::cout << "delay threshold = "
                  << standard::decimal_text(numbers.value()[0]) << ' '
                  << standard::unit_symbol(numbers.value()[0].scaling.unit) << '\n';
    // UInt8 5 → 5 s
}
```

### 读时钟源

```cpp
auto oad = standard::make_oad(oi::clock_source, 2, 0, layout); // 4006/2/0
auto value = client.get(oad.value());
if (value && !std::get_if<std::uint8_t>(&value.value())) {
    const auto& data = std::get<model::Data>(value.value());
    const auto& fields = data.as<model::Structure>().value;
    // fields[0] = 时钟源，fields[1] = 状态，都是 Enum
}
```

### 写时钟

```cpp
auto dar = client.set({oi::date_time, 2, 0},
                      model::Data{model::DateTimeS{{0x07, 0xea, 10, 7, 15, 0, 0}}});
if (!dar) {
    std::cerr << dar.error().context << '\n';
} else if (dar.value() != 0) {
    std::cerr << "meter refused to set clock, DAR=" << unsigned(dar.value()) << '\n';
}
```

**写时钟前必须确认校时模式允许**，否则设备返回 DAR=3。详见[写参数与执行方法](./write-action.md)。

## 请求与响应报文

以下示例固定：SA 六个 `00` 字节、逻辑地址 0、CA=0、PIID=0、控制域请求 `43`／响应 `C3`。

### 读取日期时间

请求（`4000/2/0`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 40 00 02 00 00 56 1F 16
```

响应（2026-10-07 14:16:26）：

```text
68 21 00 C3 05 00 00 00 00 00 00 00 1D DB 85 01 00 40 00 02 00 01 1C 07 EA 0A 07 0E 10 1A 00 00 E4 C5 16
```

| 字段 | 字节 | 含义 |
| --- | --- | --- |
| `85` | | 服务：GET-Response |
| `01` | | GET-Normal |
| `00` | | PIID-ACD=0 |
| `40 00 02 00` | | 回显 OAD：日期时间，属性 2，索引 0 |
| `01` | | 选择子：Data |
| `1C` | | 数据类型：`DateTimeS` |
| `07 EA` | | 年 2026 |
| `0A 07` | | 月 10 日 7 |
| `0E 10` | | 时 14 分 16 |
| `1A` | | 秒 26 |
| `00` / `00` | | FollowReport / TimeTag 不存在 |

`DateTimeS` 在线上占 7 字节，最后一个字节是秒。

### 读取校时模式

请求（`4000/3/0`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 40 00 03 00 00 8A 45 16
```

响应（`Enum` 1，允许校时）：

```text
68 1B 00 C3 05 00 00 00 00 00 00 00 5A 5F 85 01 00 40 00 03 00 01 16 01 00 00 B6 45 16
```

数据类型 `16` 是 `Enum`，值 `01`。

### 读取精准校时参数

请求（`4000/4/0`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 40 00 04 00 00 8F C9 16
```

响应（8、2、1、5、4）：

```text
68 25 00 C3 05 00 00 00 00 00 00 00 48 85 85 01 00 40 00 04 00 01 02 05 11 08 11 02 11 01 11 05 11 04 00 00 39 50 16
```

| 字段 | 字节 | 含义 |
| --- | --- | --- |
| `02` | | 数据类型：`Structure` |
| `05` | | 字段数 5 |
| `11 08` | | 字段 1：`UInt8` 8（最近心跳总个数） |
| `11 02` | | 字段 2：`UInt8` 2（最大值剔除个数） |
| `11 01` | | 字段 3：`UInt8` 1（最小值剔除个数） |
| `11 05` | | 字段 4：`UInt8` 5（通讯延时阈值，秒） |
| `11 04` | | 字段 5：`UInt8` 4（最少有效个数） |

每个 `UInt8` 字段都带 `11` 类型标记，因为结构字段必须自带类型。

### 读取单个结构字段

请求（`4000/4/4`，只读通讯延时阈值）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 40 00 04 04 00 EF AE 16
```

响应（`UInt8` 5，即 5 s）：

```text
68 1B 00 C3 05 00 00 00 00 00 00 00 5A 5F 85 01 00 40 00 04 04 01 11 05 00 00 42 A6 16
```

索引 4 直接对应第 4 个字段，**响应里没有 `Structure` 包装，直接是 `11`（UInt8）+ 值**。

### 读取时钟源

请求（`4006/2/0`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 40 06 02 00 00 CC 54 16
```

响应（时钟源 1，状态 0）：

```text
68 1F 00 C3 05 00 00 00 00 00 00 00 0F 01 85 01 00 40 06 02 00 01 02 02 16 01 16 00 00 00 B2 87 16
```

`02` 是 `Structure`，`02` 是字段数，两个字段都是 `16`（`Enum`）：`01` 和 `00`。

### 写日期时间

请求（设为 2026-10-07 15:00:00）：

```text
68 1F 00 43 05 00 00 00 00 00 00 00 D8 C5 06 01 00 40 00 02 00 1C 07 EA 0A 07 0F 00 00 00 06 D5 16
```

响应（成功，DAR=0）：

```text
68 19 00 C3 05 00 00 00 00 00 00 00 78 F4 86 01 00 40 00 02 00 00 00 00 89 B6 16
```

服务 `86` 是 SET-Response，`06` 是属性个数，`01` 是 SET-Normal，最后一个 `00` 是 DAR。

### 写校时模式

请求（设为 1）：

```text
68 19 00 43 05 00 00 00 00 00 00 00 AF 30 06 01 00 40 00 03 00 16 01 00 AF 55 16
```

响应（成功）：

```text
68 19 00 C3 05 00 00 00 00 00 00 00 78 F4 86 01 00 40 00 03 00 00 00 00 CD BD 16
```

### 写校时模式被拒

响应返回 DAR=3：

```text
68 19 00 C3 05 00 00 00 00 00 00 00 78 F4 86 01 00 40 00 03 00 03 00 00 A9 52 16
```

**`Result` 成功但业务失败** —— 这是本库最重要的约定之一。DAR=3 通常表示设备侧策略不允许，或参数越界。

## 相关配置

| 配置 | 位置 | 说明 |
| --- | --- | --- |
| `SessionOptions::request_timeout` | 库连接参数 | 单次 GET 超时，默认 5 秒 |
| `SessionOptions::id_reuse_delay` | 库连接参数 | PIID 复用间隔，默认 120 秒 |
| 校时模式 `4000/3` | **电表内部参数** | 决定是否允许外部改时钟 |
| 精准校时参数 `4000/4` | **电表内部参数** | 对时心跳的过滤参数 |

## 常见失败

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| 年份是 65535 | 设备时钟未设置，协议用 `FFFF` 表示未指定 | 不是解析错误，属正常状态 |
| 写时钟返回 DAR=3 | 校时模式不允许，或设备策略限制 | 先读 `4000/3/0` 确认模式 |
| 写时钟成功但读回没变 | 设备时钟漂移后重新校准过 | 属正常，可对比前后值 |
| `as<Enum>()` 抛异常 | 实际类型不是 `Enum` | 先用 `data.type()` 判断 |
| `engineering_values` 对属性 3/4 的非延时字段报错 | 那些字段没有倍率 | 直接取原始整数 |
| 时间差一个时区 | 库不做时区转换 | 自己按本地时区组装 |
| 读索引 1～5 失败 | 属性 4 才有结构字段 | 其他属性的索引不是字段选择 |