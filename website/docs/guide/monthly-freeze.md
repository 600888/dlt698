---
title: 月冻结查询
description: 按序号或时间区间查询月冻结数据；与日冻结的差异、结算列选择和跨月区间处理。
---

# 月冻结查询

## 能读取什么

月冻结是**每月某个时刻**把计量数据固化下来的记录，用于电费结算和账单核对。

| 业务含义 | OI | 接口类 | 说明 |
| --- | --- | --- | --- |
| 月冻结 | `5006` | 9（冻结） | 属性 2 是冻结数据表 |

查询方式、报文骨架、`Result` 结构与[日冻结](./daily-freeze.md)**完全相同** —— 库为两者提供的模板列一致，行选择方式也一致。本页只讲月冻结特有的部分。

:::warning 不是所有表都有月冻结
库定义了 `5006`，不代表设备一定提供。部分表只支持日冻结（`5004`），此时读 `5006` 会得到 DAR=4（对象不存在）。设备支持哪些冻结对象，以设备点表为准。
:::

## 数据地址表

| 属性 | 名称 | 类型 | 说明 |
| --- | --- | --- | --- |
| 1 | 逻辑名 | `OctetString` | 通常不读 |
| 2 | 冻结数据表 | `Array\<Structure\>` | 记录型属性，必须用 GET-Record |

**属性 2 是记录型（`record = true`）**，普通 GET 会返回 `unsupported_service`。

库的月冻结模板固定两列，与日冻结相同：

| 定位列 | OI | 类型 | 说明 |
| --- | --- | --- | --- |
| 冻结记录序号 | `2023` | `UInt32` | 递增序号 |
| 数据冻结时间 | `2021` | `DateTimeS` | 数据冻结时刻 |

在此基础上可加任意普通点位作为数据列。

## 最小完整示例

按序号区间取三个月的正向有功总电能：

```cpp
#include <dlt698/app.hpp>
#include <dlt698/standard/records.hpp>
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

    // 序号区间 [1, 4)：取序号 1、2、3 三条
    auto query = standard::record_sequences(
        oi::monthly_freeze, 1, 4,
        {{oi::freeze_sequence, 2, 0},
         {oi::freeze_time, 2, 0},
         {oi::forward_active_energy, 2, 1}},
        layout);
    if (!query) {
        std::cerr << query.error().context << '\n';
        client.disconnect();
        return 1;
    }

    auto result = client.get_record(query.value());
    if (!result) {
        std::cerr << result.error().context << '\n';
        client.disconnect();
        return 1;
    }

    // 外层成功不等于业务成功：result 字段可能是 DAR
    if (const auto dar = std::get_if<std::uint8_t>(&result.value().result)) {
        std::cerr << "record query failed, DAR=" << unsigned(*dar) << '\n';
        client.disconnect();
        return 1;
    }

    const auto& record = result.value();

    // record.result 是 variant：uint8_t 分支是 DAR，vector 分支才是行数据
    const auto* rows = std::get_if<std::vector<protocol::apdu::RecordRow>>(&record.result);
    if (!rows || rows->empty()) {
        std::cout << "no monthly freeze record\n";
        client.disconnect();
        return 0;
    }

    for (const auto& row : *rows) {
        // 列顺序由请求 RCSD 决定，与 record.columns 一致
        for (std::size_t i = 0; i < row.size(); ++i) {
            std::cout << "cell " << i << " type=" << unsigned(row[i].type()) << '\n';
        }
        // 最后一列是电能，换算成工程值输出
        const model::Oad energy_column{oi::forward_active_energy, 2, 1};
        const auto numbers = standard::engineering_values(energy_column, row.back(), layout);
        if (numbers && !numbers.value().empty())
            std::cout << "energy " << standard::decimal_text(numbers.value().front())
                      << " kWh\n";
    }

    client.disconnect();
    return 0;
}
```

## 常用变体

### 结算常用列组合

对账场景通常要"序号 + 冻结时间 + 正向有功电能"，跨费率时再加各费率：

```cpp
std::vector<model::Oad> columns{
    {oi::freeze_sequence, 2, 0},          // 序号
    {oi::freeze_time, 2, 0},              // 冻结时间
    {oi::forward_active_energy, 2, 0},     // 正向有功总电能（整体）
};
for (std::uint8_t t = 1; t <= layout.tariff_count; ++t)
    columns.push_back({oi::forward_active_energy, 2, t});  // 各费率
```

:::tip 索引 0 和索引 1 的区别
`0010/2/0` 是**总电能**（一个值），`0010/2/1` 是**费率 1 的电能**。结算需要"总量 + 分费率"时两者都要查，不能用费率 1 代替总量。详见[电能](./energy.md)。
:::

### 按时间区间查询

```cpp
auto query = standard::record_between(oi::monthly_freeze,
                                      model::DateTimeS{{0x07, 0xea, 1, 1, 0, 0, 0}},   // 含
                                      model::DateTimeS{{0x07, 0xea, 4, 1, 0, 0, 0}},   // 不含
                                      columns, layout);
```

**区间是前闭后开**：`[2026-01-01, 2026-04-01)` 返回 1、2、3 三个月。

### 跨年区间

`DateTimeS` 的年是 2 字节大端（`07 EA` = 2026），库按公历严格校验月日，闰年 2 月 29 日也正确处理：

```cpp
// 2028-02-29 是合法日期
model::DateTimeS{{0x07, 0xf0, 2, 29, 0, 0, 0}}   // 0x07F0 = 2032
```

`record_at` / `record_between` 要求**完整合法时间**，不能有通配字段（月份不能是 `13`，也不能是 `FF`）。非法时间在构造查询时就返回 `invalid_value`，不会发到设备。

### 翻页拉取全部历史

序号区间不知道上界时，从小到大分页：

```cpp
std::uint32_t begin = 1;
for (int page = 0; page < 100; ++page) {   // 设上限，避免设备返回海量数据
    auto query = standard::record_sequences(oi::monthly_freeze, begin, begin + 12, columns, layout);
    if (!query) break;
    auto result = client.get_record(query.value());
    if (!result || std::get_if<std::uint8_t>(&result.value().result)) break;

    const auto& rows = *std::get_if<std::vector<protocol::apdu::RecordRow>>(&result.value().result);
    if (rows.empty()) break;               // 没有更多记录
    // ... 处理 rows ...

    // 用响应里的实际序号推进起点
    begin = rows.back().front().as<model::UInt32>().value + 1;
}
```

**不要假设序号连续**。设备可能因换表、格式化而重置或跳号，以上一次响应里的最大序号为准。

## 请求与响应报文

以下示例固定：SA 六个 `00` 字节、逻辑地址 0、CA=0、PIID=0、控制域请求 `43`／响应 `C3`。

响应的字节骨架见[日冻结的骨架说明](./daily-freeze.md#get-record-响应的固定骨架)：`85 03 | PIID-ACD | OAD | 列数 | 列 OAD | 结果选择子 | 行数 | 行数据`。

### 按序号区间查询三列

请求（序号 `[1, 3)`，列 = 序号、冻结时间、正向有功电能费率 1）：

```text
68 37 00 43 05 00 00 00 00 00 00 00 F8 9B 05 03 00 50 06 02 00 02 20 23 02 00 06 00 00 00 01 06 00 00 00 03 00 03 00 20 23 02 00 00 20 21 02 00 00 00 10 02 01 00 AF 50 16
```

| 字段 | 字节 | 含义 |
| --- | --- | --- |
| 长度域 | `37 00` | 55 字节数据，完整帧 58 字节 |
| `05 03` | | 服务 5（GET-Request），选择子 3（GET-Record） |
| `00` | | PIID=0 |
| `50 06 02 00` | | OAD：月冻结，属性 2，索引 0 |
| `02` | | RSD 类型 2（区间） |
| `20 23 02 00` | | 区间定位列：冻结记录序号 `2023/2/0` |
| `06 00 00 00 01` | | `UInt32` begin = 1 |
| `06 00 00 00 03` | | `UInt32` end = 3 |
| `00` | | interval = NULL，不设采样间隔 |
| `03` | | RCSD：3 列 |
| `00 20 23 02 00` | | 列 1：冻结记录序号 |
| `00 20 21 02 00` | | 列 2：数据冻结时间 |
| `00 00 10 02 01` | | 列 3：正向有功电能，费率 1 |

注意 `06` 是 `UInt32` 的类型标签，`00 00 00 01` 才是数值 1。

响应（两行）：

```text
68 4E 00 C3 05 00 00 00 00 00 00 00 DE E8 85 03 00 50 06 02 00 03 00 20 23 02 00 00 20 21 02 00 00 00 10 02 01 01 02 06 00 00 00 01 1C 07 EA 09 01 00 00 00 06 00 01 B2 07 06 00 00 00 02 1C 07 EA 0A 01 00 00 00 06 00 03 64 0E 00 00 D0 A2 16
```

| 字段 | 字节 | 含义 |
| --- | --- | --- |
| `03` | | **表头列数 = 3** |
| `00 20 23 02 00` | | 列 1：冻结记录序号 |
| `00 20 21 02 00` | | 列 2：数据冻结时间 |
| `00 00 10 02 01` | | 列 3：正向有功电能，费率 1 |
| `01` | | 结果选择子 1：记录 |
| `02` | | **行数 = 2**（序号 1 和 2，序号 3 不含在内） |
| `06 00 00 00 01` | | 行 1 列 1：`UInt32` 序号 = 1 |
| `1C 07 EA 09 01 00 00 00` | | 行 1 列 2：`DateTimeS` 2026-09-01 00:00:00 |
| `06 00 01 B2 07` | | 行 1 列 3：`UInt32` 111111 → 1111.11 kWh |
| `06 00 00 00 02` | | 行 2 列 1：序号 = 2 |
| `1C 07 EA 0A 01 00 00 00` | | 行 2 列 2：2026-10-01 00:00:00 |
| `06 00 03 64 0E` | | 行 2 列 3：222222 → 2222.22 kWh |

`DateTimeS` 的 `1C` 之后是 7 个字节：年（2 字节大端）月 日 时 分 秒 毫秒。`07 EA` = 2026。

## 相关配置

| 配置 | 位置 | 说明 |
| --- | --- | --- |
| `DeviceLayout::tariff_count` | 库本地配置 | 校验冻结数据列里的电能数组长度 |
| `DeviceLayout::wiring` | 库本地配置 | 校验冻结数据列里的相别数组 |
| `SessionOptions::request_timeout` | 库连接参数 | 记录查询可能较慢，建议适当调大 |
| `SessionOptions::limits.max_elements` | 库连接参数 | 限制行数和列数，防恶意响应 |

## 常见失败

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| DAR=4 | 设备不提供月冻结 | 改用日冻结，见[日冻结查询](./daily-freeze.md) |
| DAR=3 | 设备拒绝该查询 | 冻结未启用，或序号区间不被支持，改用时间区间 |
| DAR=5 | 属性不是记录型 | 用了普通 GET 而非 GET-Record |
| 返回 0 行但设备应有记录 | 序号区间超出设备实际范围 | 先读 `2023/2/0` 看当前最大序号 |
| 序号跳变 | 设备换表、格式化或存储深度不足 | 以响应实际序号推进，不要假定连续 |
| 电能值比预期大一个数量级 | 误把索引 0 整体当成费率 1 | 见[电能](./energy.md)的索引说明 |
| 查询成功但 `columns` 为空 | 设备没回表头 | 库返回 `resource_limit`，属正常保护 |
| `invalid_value` on 时间 | 时间字段非法 | 月冻结查询要求完整合法公历时间 |
| 普通 GET 报 `unsupported_service` | 冻结属性是记录型 | 必须用 `get_record` |