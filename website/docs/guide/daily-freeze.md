---
title: 日冻结查询
description: 按时刻或时间区间查询日冻结数据；冻结记录结构、列选择和空结果处理。
---

# 日冻结查询

## 能读取什么

日冻结是**每天某个时刻**把计量数据固化下来的记录，用于电费结算和对账。

| 业务含义 | OI | 接口类 | 说明 |
| --- | --- | --- | --- |
| 日冻结 | `5004` | 9（冻结） | 属性 2 是冻结数据表 |

冻结记录的**列不是固定的**，由查询时的 RCSD 指定。库为日冻结提供了模板，固定定位两列：

| 定位列 | OI | 类型 | 说明 |
| --- | --- | --- | --- |
| 冻结记录序号 | `2023` | `UInt32` | 递增序号 |
| 数据冻结时间 | `2021` | `DateTimeS` | 数据冻结时刻 |

在此基础上可以加任意普通点位作为数据列：电能、需量、电压、电流、功率等。

:::warning 冻结数据是"快照"，不是实时值
冻结记录里的数据是**冻结时刻的值**，之后不再变化。要看当前值请读实时对象（[电能](./energy.md)、[电压](./voltage.md)等）。

日冻结的时间通常是当天 00:00:00，冻结的是**前一天**的日累计值。具体冻结时刻由设备配置决定。
:::

## 数据地址表

| 属性 | 名称 | 类型 | 说明 |
| --- | --- | --- | --- |
| 1 | 逻辑名 | `OctetString` | 通常不读 |
| 2 | 冻结数据表 | `Array\<Structure\>` | 记录型属性，必须用 GET-Record |

**属性 2 是记录型（`record = true`）**，不能用普通 GET 读，会返回 `unsupported_service`。

常用数据列的 OAD 与普通点位完全一致：

| 数据列 | OAD | 备注 |
| --- | --- | --- |
| 正向有功总电能 | `0010/2/1` | 最常用的结算列 |
| 正向有功总需量 | `1010/2/1` | 含发生时间 |
| A/B/C 相电能 | `0011/2/1` `0012/2/1` `0013/2/1` | 需量同理 |
| A/B/C 相电压 | `2000/2/1` `2000/2/2` `2000/2/3` | |
| A/B/C 相电流 | `2001/2/1` `2001/2/2` `2001/2/3` | |

数据列是**平面 OAD**（元素 OAD），不是索引 0 的整体数组。

### 三种行选择方式

| 方式 | 接口 | RSD 类型 | 用途 |
| --- | --- | --- | --- |
| `record_at` | Selector1 | `Oad` + `DateTimeS` | 恰好某一天 |
| `record_between` | Selector2 | 时间区间 | 前闭后开的日期范围 |
| `record_sequences` | Selector2 | 序号区间 | 前闭后开的序号范围 |

三个接口都**不设置采样间隔**（`interval = NULL`）。

## 最小完整示例

按时刻查询一天的冻结数据：

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

    // 2026-10-05 00:00:00 的日冻结，只取正向有功总电能
    auto query = standard::record_at(oi::daily_freeze,
                                     model::DateTimeS{{0x07, 0xea, 10, 5, 0, 0, 0}},
                                     {{oi::forward_active_energy, 2, 1}}, layout);
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
    if (const auto dar = std::get_if<std::uint8_t>(&result.value().result)) {
        std::cerr << "record query failed, DAR=" << unsigned(*dar) << '\n';
        client.disconnect();
        return 1;
    }

    const auto& record = result.value();
    std::cout << "columns = " << record.columns.size() << '\n';

    // record.result 是 variant，确认不是 DAR 后取出行数据分支
    const auto* rows = std::get_if<std::vector<protocol::apdu::RecordRow>>(&record.result);
    if (!rows) return 1;   // 上面已排除 DAR，这里只是防御

    for (const auto& row : *rows) {
        std::cout << "row with " << row.size() << " cells\n";
        for (std::size_t i = 0; i < row.size() && i < record.columns.size(); ++i) {
            // 表头列与行单元一一对应，按下标配对
            const auto& column = std::get<model::Oad>(record.columns[i]);
            std::cout << "  column oi=" << std::hex << column.oi << std::dec
                      << " attr=" << unsigned(column.attribute)
                      << " index=" << unsigned(column.index)
                      << " type=" << unsigned(row[i].type()) << '\n';
        }
    }

    client.disconnect();
    return 0;
}
```

## 常用变体

### 按时间区间查询

```cpp
auto query = standard::record_between(oi::daily_freeze,
                                       model::DateTimeS{{0x07, 0xea, 10, 3, 0, 0, 0}},  // 含
                                       model::DateTimeS{{0x07, 0xea, 10, 6, 0, 0, 0}},  // 不含
                                       {{oi::forward_active_energy, 2, 1},
                                        {oi::voltage, 2, 1},
                                        {oi::current, 2, 1}},
                                       layout);
auto result = client.get_record(query.value());
```

**区间是前闭后开**：`[10-03, 10-06)` 只返回 10-03、10-04、10-05 三天，不含 10-06。`end` 必须大于 `begin`，否则 `record_between` 返回 `invalid_value`。

### 按序号查询

不知道确切日期时按序号翻页：

```cpp
auto query = standard::record_sequences(oi::daily_freeze, 1, 4, columns, layout);
// 取序号 1、2、3 三条
auto result = client.get_record(query.value());
// 拿到响应里的实际序号后，用最后一个序号作为下一次的 begin
```

### 用默认列

`columns` 传空表示"采用后端配置的全部列"：

```cpp
auto query = standard::record_at(oi::daily_freeze, time, {}, layout);
```

:::warning 空 columns 的真正含义
空 columns 只表示「**后端配置的全部列**」，**不是「不带表头」**。

响应的 `RecordResult::columns` **必须回显具体列 OAD**。如果响应给了行数据却给了空表头，库会返回 `resource_limit` 错误 —— 这不是 bug，而是防止无法解释列含义的数据被误读。

设备返回的"全部列"具体是哪几列，由设备点表决定，库不猜测。
:::

### 校验响应

对不可信的设备数据做严格校验：

```cpp
auto valid = standard::validate_record_result(query.value(), result.value(), layout);
if (!valid) {
    std::cerr << "invalid record: " << valid.error().context << '\n';
}
```

校验内容：属性、列顺序、行宽、每个单元的类型和长度、整包资源上限。

### 解析带时间的需量

冻结列里的最大需量是结构：

```cpp
// 拿到某一列的 Data 后：
if (data.type() == model::DataType::structure) {
    const auto& fields = data.as<model::Structure>().value;
    // fields[0] 是需量值，fields[1] 是 DateTimeS 发生时间
    auto numbers = standard::engineering_values(column_oad, data, layout);
    // 需要发生时间用 demand_values
    auto readings = standard::demand_values(column_oad, data, layout);
    if (readings && !readings.value().empty()) {
        const auto& t = readings.value()[0].occurred_at.value;
        std::cout << "demand " << standard::decimal_text(readings.value()[0].number)
                  << " at " << unsigned(t[2]) << '-' << unsigned(t[3]) << '\n';
    }
}
```

### 处理空结果

设备**可能有这一天，也可能没有**（未冻结、设备时间不对、时区问题）：

```cpp
auto result = client.get_record(query.value());
if (result && !std::get_if<std::uint8_t>(&result.value().result)) {
    const auto& record = result.value();
    // record.result 是 variant，这里已确认是行数据分支
    const auto& rows = *std::get_if<std::vector<protocol::apdu::RecordRow>>(&record.result);
    if (rows.empty())
        std::cout << "no record for that day\n";  // 正常情况，不是错误
}
```

:::tip `record.result` 是 variant，不是容器
`get_record` 返回 `RecordResult`，其中 `result` 是 `std::variant<std::uint8_t, std::vector<RecordRow>>`：
`uint8_t` 分支是 DAR，`vector` 分支才是行数据。

**不能直接写 `record.result.empty()` 或 `for (auto& row : record.result)`** —— variant 没有这些成员。必须先 `std::get_if` 取出指针。
:::

## 请求与响应报文

以下示例固定：SA 六个 `00` 字节、逻辑地址 0、CA=0、PIID=0、控制域请求 `43`／响应 `C3`。

### GET-Record 响应的固定骨架

记录型响应的字节布局是固定的，记住这一条就能自己读任何冻结/事件报文：

```text
85 03 | PIID-ACD | OAD(4) | 列数 | 列 OAD × N | 结果选择子 | 行数 | 行数据 | TimeTag 可选
```

- `85 03`：GET-Response，服务 5、选择子 3
- **OAD 之后立刻是列数**，没有额外的"选择子：Data"包装
- **结果选择子 `01` = 记录**（`00` = 数据），库已按此解码，你不需要自己判断
- 随后是行数，再按行优先展开单元
- 响应**没有 FollowReport 位**，末尾只有可选 TimeTag

这一骨架对[月冻结](./monthly-freeze.md)和[事件记录](./events.md)完全相同。

### 按时刻查询

请求（`5004` 的冻结时间 = 2026-10-05 00:00:00，列 = `0010/2/1`）：

```text
68 2A 00 43 05 00 00 00 00 00 00 00 D3 5D 05 03 00 50 04 02 00 01 20 21 02 00 1C 07 EA 0A 05 00 00 00 01 00 00 10 02 01 00 82 30 16
```

| 字段 | 字节 | 含义 |
| --- | --- | --- |
| 长度域 | `2A 00` | 42 字节数据，完整帧 45 字节 |
| `05 03` | | 服务 5（GET-Request），选择子 3（GET-Record） |
| `00` | | PIID=0 |
| `50 04 02 00` | | OAD：日冻结，属性 2，索引 0 |
| `01` | | RSD 类型 1：`Oad`（按时间定位） |
| `20 21 02 00` | | 时间列：数据冻结时间 `2021/2/0` |
| `1C 07 EA 0A 05 00 00 00` | | `DateTimeS` 2026-10-05 00:00:00 |
| `01` | | RCSD：1 列 |
| `00 00 10 02 01` | | 列 OAD：正向有功电能，属性 2，索引 1 |

响应（`UInt32` 123456，即 1234.56 kWh）：

```text
68 25 00 C3 05 00 00 00 00 00 00 00 48 85 85 03 00 50 04 02 00 01 00 00 10 02 01 01 01 06 00 01 E2 40 00 00 3A 61 16
```

| 字段 | 字节 | 含义 |
| --- | --- | --- |
| `85 03` | | GET-Response / GET-Record |
| `00` | | PIID-ACD=0 |
| `50 04 02 00` | | 回显 OAD：`5004` 属性 2 索引 0 |
| `01` | | **表头列数 = 1** |
| `00 00 10 02 01` | | 表头列 OAD：正向有功电能 `0010/2/1` |
| `01` | | **结果选择子 1：记录** |
| `01` | | **行数 = 1** |
| `06 00 01 E2 40` | | 该行唯一单元：`UInt32` 123456 |

**响应里 OAD 之后紧跟列数**，没有"选择子：Data"这一层包装。表头必须回显具体列，即使请求只查一列。

### 按区间查询三列

请求（`[2026-10-03, 2026-10-06)`，三列）：

```text
68 3D 00 43 05 00 00 00 00 00 00 00 70 8C 05 03 00 50 04 02 00 02 20 21 02 00 1C 07 EA 0A 03 00 00 00 1C 07 EA 0A 06 00 00 00 00 03 00 00 10 02 01 00 20 00 02 01 00 20 01 02 01 00 30 F4 16
```

`02` 是 RSD 类型 2（区间），后面两个 `DateTimeS` 是起止时间。`00` 是 interval（NULL，不设采样间隔）。

响应（两行）：

```text
68 44 00 C3 05 00 00 00 00 00 00 00 56 FF 85 03 00 50 04 02 00 03 00 00 10 02 01 00 20 00 02 01 00 20 01 02 01 01 02 06 00 01 E0 78 12 09 65 05 00 00 13 74 06 00 01 E2 40 12 09 6D 05 00 00 13 88 00 00 D0 E0 16
```

| 字段 | 字节 | 含义 |
| --- | --- | --- |
| `03` | | **表头列数 = 3** |
| `00 00 10 02 01` | | 列 1：`0010/2/1` 正向有功电能 |
| `00 20 00 02 01` | | 列 2：`2000/2/1`（A 相电压） |
| `00 20 01 02 01` | | 列 3：`2001/2/1`（A 相电流） |
| `01` | | **结果选择子 1：记录** |
| `02` | | **行数 = 2** |
| `06 00 01 E0 78` | | 行 1 列 1：`UInt32` 123000 → 1230.00 kWh |
| `12 09 65` | | 行 1 列 2：`UInt16` 2405 → 240.5 V |
| `05 00 00 13 74` | | 行 1 列 3：`Int32` 4980 → 4.980 A |
| `06 00 01 E2 40` | | 行 2 列 1：123456 → 1234.56 kWh |
| `12 09 6D` | | 行 2 列 2：2413 → 241.3 V |
| `05 00 00 13 88` | | 行 2 列 3：5000 → 5.000 A |

**行优先、列对齐**：每行的单元按表头顺序排列，行数在列 OAD 之后。

### 空结果

响应有表头但没有行：

```text
68 20 00 C3 05 00 00 00 00 00 00 00 8C 8E 85 03 00 50 04 02 00 01 00 00 10 02 01 01 00 00 00 D8 F3 16
```

`00 00 10 02 01` 是表头列 OAD（前一个 `01` 是列数），`01` 是结果选择子（记录），随后的 `00` 表示 **0 行**。

**这是正常响应，不是错误。** 设备只是没有那一天的冻结记录。

## 相关配置

| 配置 | 位置 | 说明 |
| --- | --- | --- |
| `DeviceLayout::tariff_count` | 库本地配置 | 校验冻结数据列里的电能数组 |
| `DeviceLayout::wiring` | 库本地配置 | 校验冻结数据列里的相别数组 |
| `SessionOptions::request_timeout` | 库连接参数 | 记录查询可能较慢，建议适当调大 |
| `SessionOptions::limits.max_elements` | 库连接参数 | 限制行数和列数，防恶意响应 |

## 常见失败

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| DAR=4 | 设备不提供日冻结 | 部分表只有月冻结 |
| DAR=3 | 设备拒绝该查询 | 选择器语义不被支持，或冻结未启用 |
| DAR=5 | 属性不是记录型 | 用了普通 GET 而非 GET-Record |
| 查询成功但 `columns` 为空 | 设备没回表头 | 库返回 `resource_limit`，属正常保护 |
| 普通 GET 报 `unsupported_service` | 冻结属性是记录型 | 必须用 `get_record` |
| 某列解析失败 | 该列 OAD 类型与实际不符 | 冻结列可能是聚合类型，检查设备点表 |
| `invalid_value` on 时间 | 时间字段非法 | `record_at` 要求完整合法公历时间，不能有通配 |
| `record_between` 报错 | `end` 不大于 `begin` | 区间必须非空且正序 |
| 查不到数据但设备应该有 | 冻结时刻未到、设备时钟错、时区不同 | 读当前时钟核对，见[日期时间与校时](./clock.md) |