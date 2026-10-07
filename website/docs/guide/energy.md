---
title: 电能读取
description: 正反向有功、组合无功、四象限无功、视在电能；总量、分相、分费率和扩展精度。
---

# 电能读取

## 能读取什么

电能是累计量，按**方向**和**无功类型**分成若干家族，每个家族又分**相别**和**费率**：

| 家族 | 业务含义 | 符号 |
| --- | --- | --- |
| 正向有功电能 | 从电网吸收的有功电量（正向） | 无符号 |
| 反向有功电能 | 向电网倒送的有功电量 | 无符号 |
| 组合无功 1 电能 | 组合无功 1 型无功电量 | 有符号 |
| 组合无功 2 电能 | 组合无功 2 型无功电量 | 有符号 |
| 第一～第四象限无功电能 | 按功率象限划分的无功电量 | 无符号 |
| 正向/反向视在电能 | 视在电量 | 无符号 |
| 组合有功电能 | 组合有功（代数和） | 有符号 |

对每个家族可以读：**总量**、**分相（A/B/C）**、**分费率（费率 1～n）**，以及**扩展精度**。

**有符号家族的原始值可能为负**，不要用无符号类型存储。

## 数据地址表

每个家族按标准编号展开，总对象 + 三个相别对象：

| 家族 | OI（总、A、B、C） | 类型 | 倍率 | 单位 |
| --- | --- | --- | --- | --- |
| 正向有功电能 | `0010` `0011` `0012` `0013` | Array\<UInt32\> | -2 | kWh |
| 反向有功电能 | `0020` `0021` `0022` `0023` | Array\<UInt32\> | -2 | kWh |
| 组合无功 1 电能 | `0030` `0031` `0032` `0033` | Array\<Int32\> | -2 | kvarh |
| 组合无功 2 电能 | `0040` `0041` `0042` `0043` | Array\<Int32\> | -2 | kvarh |
| 第一象限无功 | `0050` `0051` `0052` `0053` | Array\<UInt32\> | -2 | kvarh |
| 第二象限无功 | `0060` `0061` `0062` `0063` | Array\<UInt32\> | -2 | kvarh |
| 第三象限无功 | `0070` `0071` `0072` `0073` | Array\<UInt32\> | -2 | kvarh |
| 第四象限无功 | `0080` `0081` `0082` `0083` | Array\<UInt32\> | -2 | kvarh |
| 正向视在电能 | `0090` `0091` `0092` `0093` | Array\<UInt32\> | -2 | kVAh |
| 反向视在电能 | `00A0` `00A1` `00A2` `00A3` | Array\<UInt32\> | -2 | kVAh |
| 组合有功电能 | `0000` 仅总 | Array\<Int32\> | -2 | kWh |

每个电能对象收录的属性：

| 属性 | 名称 | 类型 | 倍率 | 说明 |
| --- | --- | --- | --- | --- |
| 1 | 逻辑名 | `OctetString` | — | 设备命名，通常不读 |
| 2 | 总及费率电能量 | Array\<UInt32\> 或 Array\<Int32\> | -2 | 索引 1 总量，2～n+1 费率 |
| 3 | 换算及单位 | `ScalerUnit` | — | 设备声明的倍率和单位 |
| 4 | 扩展精度总及费率电能量 | Array\<UInt64\> 或 Array\<Int64\> | -4 | 4 位小数 |
| 5 | 扩展精度换算及单位 | `ScalerUnit` | — | 属性 4 的单位 |

数组长度 = `tariff_count + 1`。属性 2/3/4/5 都是**只读**，电能对象不提供写入。

**单位是 kWh/kvarh/kVAh，不是 Wh。** 倍率 -2 已经是这个单位的结果。

## 最小完整示例

```cpp
#include <dlt698/app.hpp>
#include <dlt698/standard/catalog.hpp>
#include <iostream>

int main() {
    using namespace dlt698;
    namespace oi = standard::oi;

    // 费率数必须与设备一致；先读 400C 得到实际值再构造布局。
    const standard::DeviceLayout layout{standard::Wiring::three_phase, 4, 21};

    app::ClientOptions options;
    options.protocol.server.bytes = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    app::Client client(options);
    auto connected = client.connect_tcp("127.0.0.1", 6980);
    if (!connected) {
        std::cerr << connected.error().context << '\n';
        return 1;
    }

    // 正向有功总电能：家族=0010，相别=total，费率=0（总量）
    auto oad = standard::energy_oad(oi::forward_active_energy, standard::Phase::total, 0, layout);
    if (!oad) {
        std::cerr << oad.error().context << '\n';
        return 1;
    }

    auto value = client.get(oad.value());
    if (!value) {
        std::cerr << value.error().context << '\n';
        client.disconnect();
        return 1;
    }
    if (const auto dar = std::get_if<std::uint8_t>(&value.value())) {
        // DAR=4 表示设备没有提供这个对象
        std::cerr << "meter does not provide it, DAR=" << unsigned(*dar) << '\n';
        client.disconnect();
        return 1;
    }

    const auto& data = std::get<model::Data>(value.value());
    // 校验类型并换算倍率：线上 123456 是原始整数，1234.56 kWh 才是工程值。
    auto numbers = standard::engineering_values(oad.value(), data, layout);
    if (!numbers || numbers.value().empty()) {
        std::cerr << "unexpected value\n";
        client.disconnect();
        return 1;
    }
    std::cout << "total forward active energy = "
              << standard::decimal_text(numbers.value()[0]) << ' '
              << standard::unit_symbol(numbers.value()[0].scaling.unit) << '\n';
    // 输出：total forward active energy = 1234.56 kWh

    client.disconnect();
    return 0;
}
```

## 常用变体

### 一次读总量和所有费率

索引 0 读整个数组，`engineering_values` 按数组顺序输出所有数值：

```cpp
auto oad = standard::make_oad(standard::oi::forward_active_energy, 2, 0, layout);
auto value = client.get(oad.value());
if (value && !std::get_if<std::uint8_t>(&value.value())) {
    const auto& data = std::get<model::Data>(value.value());
    auto numbers = standard::engineering_values(oad.value(), data, layout);
    if (numbers) {
        // 顺序：总量、费率 1、费率 2 ...
        for (std::size_t i = 0; i < numbers.value().size(); ++i)
            std::cout << (i == 0 ? "total" : "tariff " + std::to_string(i))
                      << " = " << standard::decimal_text(numbers.value()[i]) << '\n';
    }
}
```

### 分相电能

相别在 **OI** 里，不在索引里：

```cpp
auto total = standard::energy_oad(oi::forward_active_energy, standard::Phase::total, 0, layout);
auto a     = standard::energy_oad(oi::forward_active_energy, standard::Phase::a, 0, layout);
auto b     = standard::energy_oad(oi::forward_active_energy, standard::Phase::b, 0, layout);
auto c     = standard::energy_oad(oi::forward_active_energy, standard::Phase::c, 0, layout);
// 分别对应 0010/2/1、0011/2/1、0012/2/1、0013/2/1

auto result = client.get_list({total.value(), a.value(), b.value(), c.value()});
```

**三相四线表的分相电能之和通常不等于总量**，差额是线路损耗，不能当作数据错误。

### 分费率

`tariff` 参数从 0 开始（0 是总量），内部转换成索引 `tariff + 1`：

```cpp
for (std::size_t tariff = 0; tariff <= layout.tariff_count; ++tariff) {
    auto oad = standard::energy_oad(oi::forward_active_energy, standard::Phase::total, tariff, layout);
    // tariff=0 → 0010/2/1（总量）
    // tariff=1 → 0010/2/2（费率 1）
}
```

### 扩展精度

```cpp
auto normal  = standard::energy_oad(oi::forward_active_energy, standard::Phase::total, 0, layout, false); // 0010/2/1
auto precise = standard::energy_oad(oi::forward_active_energy, standard::Phase::total, 0, layout, true);  // 0010/4/1
```

属性 4 是独立属性，**不是所有设备都提供**，不支持时返回 DAR=4。类型 `UInt64`，倍率 -4，`decimal_text()` 不经过浮点，能精确表示 4 位小数。

### 读取多个家族

```cpp
std::vector<model::Oad> oads;
for (auto family : {oi::forward_active_energy, oi::reverse_active_energy,
                    oi::combination_reactive_energy_1, oi::quadrant_1_reactive_energy}) {
    auto oad = standard::energy_oad(family, standard::Phase::total, 0, layout);
    if (oad) oads.push_back(oad.value());
}
auto result = client.get_list(oads);
```

单个列表里 OAD 数量过多时响应会分块，客户端自动收齐，但总耗时会增加。

## 请求与响应报文

以下示例固定：SA 六个 `00` 字节、逻辑地址 0、CA=0、PIID=0、控制域请求 `43`／响应 `C3`。

### 读取正向有功总电能

请求（GET Normal，读取 `0010/2/1`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 00 10 02 01 00 0D 04 16
```

| 字段 | 字节 | 含义 |
| --- | --- | --- |
| `68` / `16` | | 起始符 / 结束符 |
| `17 00` | | 长度域 23，完整帧 25 字节 |
| `43` | | 控制域：客户机发起，无分帧，无扰码 |
| `05` + 六个 `00` | | 单地址、逻辑地址 0、SA 长度 6 |
| `00` | | CA=0 |
| `72 79` | | HCS |
| `05` | | 服务：GET-Request |
| `01` | | GET-Normal |
| `00` | | PIID=0 |
| `00 10 02 01` | | OAD：正向有功电能，属性 2，索引 1（总量） |
| `00` | | TimeTag 不存在 |
| `0D 04` | | FCS |
| `16` | | 结束符 |

响应（`UInt32` 123456，即 1234.56 kWh）：

```text
68 1E 00 C3 05 00 00 00 00 00 00 00 9E 54 85 01 00 00 10 02 01 01 06 00 01 E2 40 00 00 5A D4 16
```

| 字段 | 字节 | 含义 |
| --- | --- | --- |
| `85` | | 服务：GET-Response |
| `01` | | GET-Normal |
| `00` | | PIID-ACD=0 |
| `00 10 02 01` | | 回显 OAD |
| `01` | | 选择子：Data |
| `06` | | 数据类型：`UInt32` |
| `00 01 E2 40` | | 原始值 123456 |
| `00` / `00` | | FollowReport 不存在 / TimeTag 不存在 |

`00 01 E2 40` = 123456，乘以 10^-2 得 **1234.56 kWh**。

### 读取整个电能数组（四费率）

请求（`0010/2/0`，索引 0 = 完整数组）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 00 10 02 00 00 D5 1D 16
```

响应（总量 123456，四费率 10000、20000、30000、63456）：

```text
68 34 00 C3 05 00 00 00 00 00 00 00 9C A1 85 01 00 00 10 02 00 01 01 05 06 00 01 E2 40 06 00 00 27 10 06 00 00 4E 20 06 00 00 75 30 06 00 00 F7 E0 00 00 CC B3 16
```

解析：结果选择子 `01` → 数组长度 `05`（5 项）→ 5 个 `06`（UInt32）值，顺序为总量、费率 1～4：

| 索引 | 原始字节 | 原始值 | 工程值 |
| --- | --- | --- | --- |
| 1 总量 | `00 01 E2 40` | 123456 | 1234.56 kWh |
| 2 费率 1 | `00 00 27 10` | 10000 | 100.00 kWh |
| 3 费率 2 | `00 00 4E 20` | 20000 | 200.00 kWh |
| 4 费率 3 | `00 00 75 30` | 30000 | 300.00 kWh |
| 5 费率 4 | `00 00 F7 E0` | 63456 | 634.56 kWh |

四者之和 634.56 + 100 + 200 + 300 = 1234.56，正好等于总量。

### 读取 A 相正向有功电能

请求（`0011/2/1`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 00 11 02 01 00 B6 18 16
```

响应（UInt32 400000，即 4000.00 kWh）：

```text
68 1E 00 C3 05 00 00 00 00 00 00 00 9E 54 85 01 00 00 11 02 01 01 06 00 06 1A 80 00 00 7E 29 16
```

### 读取第一象限无功电能

请求（`0050/2/1`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 00 50 02 01 00 BA 12 16
```

响应（UInt32 54321，即 543.21 kvarh）：

```text
68 1E 00 C3 05 00 00 00 00 00 00 00 9E 54 85 01 00 00 50 02 01 01 06 00 00 D4 31 00 00 77 CF 16
```

### 读取扩展精度

请求（`0010/4/1`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 00 10 04 01 00 D4 D2 16
```

响应（UInt64 12345678，即 1234.5678 kWh）：

```text
68 22 00 C3 05 00 00 00 00 00 00 00 AE 25 85 01 00 00 10 04 01 01 15 00 00 00 00 00 BC 61 4E 00 00 62 BB 16
```

`15` 是 UInt64 标签，后 8 字节是 12345678。

### PIID 和地址变化

PIID 是请求匹配响应的标识，每帧应不同：

```text
PIID=0    68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 20 0F 02 00 00 1C 0C 16
PIID=1    68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 01 20 0F 02 00 00 37 08 16
PIID=0x21 68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 21 20 0F 02 00 00 57 8D 16
```

SA 改成 `123456789012`（线序 `12 90 78 56 34 12`）后，**HCS 和 FCS 都要重算**：

```text
68 17 00 43 05 12 90 78 56 34 12 00 A3 2E 05 01 00 20 0F 02 00 00 1C 0C 16
```

## 相关配置

| 配置 | 影响 |
| --- | --- |
| `DeviceLayout::tariff_count` | 决定电能数组长度校验。必须与设备实际费率数一致 |
| `DeviceLayout::wiring` | 单相配置不受理 B/C 相电能对象 |
| `parameters.apdu_bytes` | 数组较长时响应可能分块 |
| `request_timeout` | 分块读取时需要放宽 |
| `400C/2/0`（时区时段数） | 读取可得实际费率数，是设置 `tariff_count` 的依据 |

读取费率数的方法见[电表参数读取与设置](./meter-parameters.md#读取费率数)。

## 常见失败

| 现象 | 原因和处理 |
| --- | --- |
| DAR=4 | 设备不提供这个对象。老表或单功能表可能只有正向有功。确认设备型号支持 |
| DAR=8 | 索引越界。费率号超过了 `tariff_count`，或索引 0 用在了标量属性上 |
| `invalid_length` 校验失败 | 本地 `tariff_count` 与设备不一致。读 `400C/2/0` 取得实际费率数 |
| DAR=3 | 访问被拒绝。设备权限限制 |
| 数值差 100 倍 | 倍率用错。电能属性 2 是 -2、属性 4 是 -4，不要混用 |
| 符号错误 | 有符号家族（组合无功 1/2、组合有功）可能为负，用 Int32/Int64 存储 |
| 单位当成 Wh | 线上单位是 kWh/kvarh/kVAh，倍率 -2 已经换算过了 |
| 分相加不等于总量 | 正常现象，差额是线路损耗 |
| 超时 | 响应分块且超时过短，放宽 `request_timeout` 或对齐 `apdu_bytes` |

## 下一步

- 需量（数值带发生时间）：[需量读取](./demand.md)
- 批量读取多个家族：[一次读取多个数据](./batch-read.md)
- 电能对象目录与只读绑定：[标准固定点位](../protocol/standard-points.md)