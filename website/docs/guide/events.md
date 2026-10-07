---
title: 事件记录查询
description: 查询掉电事件和终端初始化事件；四列结构、NULL 来源列的含义与限制。
---

# 事件记录查询

## 能读取什么

事件记录描述**设备侧发生过的离散事件**，与[冻结](./daily-freeze.md)不同：冻结是周期性的数据快照，事件是不定时发生的动作记录。

库为两类事件提供了记录模板：

| 业务含义 | OI | 接口类 | 模板列数 |
| --- | --- | --- | --- |
| 电能表掉电事件 | `3011` | 7（事件） | 4 |
| 终端初始化事件 | `3100` | 7（事件） | 3 |

:::warning 不是所有表都有事件记录
库定义了 `3011` 和 `3100`，不代表设备一定提供。事件类对象（接口类 7）是否实现、存储深度多大，由设备点表和配置决定。不支持的读取会得到 DAR=4。
:::

## 数据地址表

| 属性 | 名称 | 类型 | 说明 |
| --- | --- | --- | --- |
| 1 | 逻辑名 | `OctetString` | 通常不读 |
| 2 | 事件记录表 | `Array\<Structure\>` | 记录型属性，必须用 GET-Record |

事件的列结构与冻结不同，多了**发生时间**和**来源**：

| 定位列 | OI | 类型 | 掉电事件 | 初始化事件 |
| --- | --- | --- | --- | --- |
| 事件记录序号 | `2022` | `UInt32` | 有 | 有 |
| 事件发生时间 | `201E` | `DateTimeS` | 有 | 有 |
| 事件结束时间 | `2020` | `DateTimeS` | 有 | 有 |
| 事件发生源 | `2024` | `Null` | 有 | 无 |

**事件发生源 `2024` 在本批实现中固定为 `Null`**，库会拒绝任何非 NULL 的值：

```cpp
// 库里对 2024 列的校验逻辑（cpp/src/standard/records.cpp）
if (column == model::Oad{oi::event_source, 2, 0}) {
    if (value.type() != model::DataType::null)
        return Error{ErrorCode::invalid_value, 0, "event NULL source"};
}
```

这是因为标准里该字段的类型定义依赖具体事件类别，库不做猜测。**不能据此推断其他事件的来源列类型** —— 如果你的设备返回了非 NULL 的来源值，需要自己处理，且不要用 `validate_record_result` 去校验它。

:::tip 终止事件没有结束时间吗
有。掉电事件的"结束时间"就是恢复供电的时刻。如果设备只填了发生时间、结束时间为 NULL，那是设备侧数据不完整，库不做补齐。
:::

## 最小完整示例

查询掉电事件：

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

    // 序号区间 [1, 2)，用模板的四列
    auto query = standard::record_sequences(oi::meter_power_down_event, 1, 2, {}, layout);
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
        std::cerr << "event query failed, DAR=" << unsigned(*dar) << '\n';
        client.disconnect();
        return 1;
    }

    const auto& record = result.value();
    const auto* rows = std::get_if<std::vector<protocol::apdu::RecordRow>>(&record.result);
    if (!rows) {
        client.disconnect();
        return 1;
    }

    for (const auto& row : *rows) {
        std::cout << "event with " << row.size() << " cells\n";
        for (std::size_t i = 0; i < row.size() && i < record.columns.size(); ++i) {
            const auto& column = std::get<model::Oad>(record.columns[i]);
            std::cout << "  oi=" << std::hex << column.oi << std::dec
                      << " attr=" << unsigned(column.attribute) << " type="
                      << unsigned(row[i].type()) << '\n';
            // 时间列按下标取：t[0..1] 年(大端) t[2] 月 t[3] 日 t[4] 时 t[5] 分 t[6] 秒
            if (row[i].type() == model::DataType::date_time_s) {
                const auto& t = row[i].as<model::DateTimeS>().value;
                std::cout << "    " << (unsigned(t[0]) << 8 | t[1]) << '-'
                          << unsigned(t[2]) << '-' << unsigned(t[3]) << ' '
                          << unsigned(t[4]) << ':' << unsigned(t[5]) << '\n';
            }
        }
    }

    client.disconnect();
    return 0;
}
```

## 常用变体

### 只取发生时间

不指定 `columns` 时用模板列。要精简就显式列出需要的列：

```cpp
auto query = standard::record_sequences(
    oi::meter_power_down_event, 1, 10,
    {{oi::event_sequence, 2, 0}, {oi::event_start_time, 2, 0}}, layout);
```

注意**定位列不必出现在数据列里**。RSD 用 `2022`（序号）定位，RCSD 可以只要 `201E`（发生时间）。

### 按时间区间查事件

```cpp
auto query = standard::record_between(oi::terminal_initialization_event,
                                      model::DateTimeS{{0x07, 0xea, 10, 1, 0, 0, 0}},
                                      model::DateTimeS{{0x07, 0xea, 10, 8, 0, 0, 0}},
                                      {}, layout);
```

定位列由模板决定：事件按 `201E`（发生时间）定位，冻结按 `2021`（冻结时间）定位。

### 拉取全部历史事件

```cpp
std::uint32_t begin = 1;
for (int page = 0; page < 100; ++page) {
    auto query = standard::record_sequences(oi::meter_power_down_event, begin, begin + 20, {}, layout);
    if (!query) break;
    auto result = client.get_record(query.value());
    if (!result || std::get_if<std::uint8_t>(&result.value().result)) break;
    const auto* rows =
        std::get_if<std::vector<protocol::apdu::RecordRow>>(&result.value().result);
    if (!rows || rows->empty()) break;
    // ... 处理 rows ...
    begin = rows->back().front().as<model::UInt32>().value + 1;   // 第 0 列是序号
}
```

**事件序号同样可能跳变**（设备重启、存储滚动），以响应里的实际序号推进。

### 严格校验

```cpp
auto result = client.get_record(query.value());
if (result && !std::get_if<std::uint8_t>(&result.value().result)) {
    auto valid = standard::validate_record_result(query.value(), result.value(), layout);
    if (!valid)
        std::cerr << "invalid event record: " << valid.error().context << '\n';
}
```

校验内容：属性回显、列顺序、行宽、每列精确类型、编码资源上限。事件来源列 `2024` 必须是 `Null`，否则返回 `invalid_value`。

## 请求与响应报文

以下示例固定：SA 六个 `00` 字节、逻辑地址 0、CA=0、PIID=0、控制域请求 `43`／响应 `C3`。

响应的字节骨架见[日冻结的骨架说明](./daily-freeze.md#get-record-响应的固定骨架)：`85 03 | PIID-ACD | OAD | 列数 | 列 OAD | 结果选择子 | 行数 | 行数据`。

### 掉电事件，四列

请求（序号 `[1, 2)`，显式四列）：

```text
68 3C 00 43 05 00 00 00 00 00 00 00 E1 D9 05 03 00 30 11 02 00 02 20 22 02 00 06 00 00 00 01 06 00 00 00 02 00 04 00 20 22 02 00 00 20 1E 02 00 00 20 20 02 00 00 20 24 02 00 00 FE 37 16
```

| 字段 | 字节 | 含义 |
| --- | --- | --- |
| 长度域 | `3C 00` | 60 字节数据，完整帧 63 字节 |
| `05 03` | | 服务 5（GET-Request），选择子 3（GET-Record） |
| `00` | | PIID=0 |
| `30 11 02 00` | | OAD：掉电事件，属性 2，索引 0 |
| `02` | | RSD 类型 2（区间） |
| `20 22 02 00` | | 定位列：事件记录序号 `2022/2/0` |
| `06 00 00 00 01` | | `UInt32` begin = 1 |
| `06 00 00 00 02` | | `UInt32` end = 2 |
| `00` | | interval = NULL |
| `04` | | RCSD：4 列 |
| `00 20 22 02 00` | | 列 1：事件记录序号 |
| `00 20 1E 02 00` | | 列 2：事件发生时间 |
| `00 20 20 02 00` | | 列 3：事件结束时间 |
| `00 20 24 02 00` | | 列 4：事件发生源 |

响应（一行，第四列为 `00` 即 `Null`）：

```text
68 45 00 C3 05 00 00 00 00 00 00 00 C7 AA 85 03 00 30 11 02 00 04 00 20 22 02 00 00 20 1E 02 00 00 20 20 02 00 00 20 24 02 00 01 01 06 00 00 00 01 1C 07 EA 0A 05 08 00 00 1C 07 EA 0A 05 08 00 1E 00 00 00 DC A5 16
```

| 字段 | 字节 | 含义 |
| --- | --- | --- |
| `03` | | 表头列数 = 4 |
| `00 20 22 02 00` | | 列 1：事件记录序号 |
| `00 20 1E 02 00` | | 列 2：事件发生时间 |
| `00 20 20 02 00` | | 列 3：事件结束时间 |
| `00 20 24 02 00` | | 列 4：事件发生源 |
| `01` | | 结果选择子 1：记录 |
| `01` | | 行数 = 1 |
| `06 00 00 00 01` | | 单元 1：`UInt32` 序号 = 1 |
| `1C 07 EA 0A 05 08 00 00` | | 单元 2：`DateTimeS` 2026-10-05 08:00:00.000 |
| `1C 07 EA 0A 05 08 00 1E` | | 单元 3：`DateTimeS` 2026-10-05 08:00:00.**30**（毫秒 30） |
| `00` | | 单元 4：**`Null`**，一个字节，没有后续数据 |

`DateTimeS` 的 `1C` 之后固定 7 字节：年（2 字节大端）月 日 时 分 秒 毫秒。单元 2 和 3 只差最后一个字节 `1E` = 30，即相差 30 毫秒 —— 掉电和恢复几乎同时。

**`Null` 只占一个字节 `00`**，不携带任何数据。

### 终端初始化事件，三列

请求（序号 `[1, 2)`，`columns` 传空表示采用后端配置的全部列）：

```text
68 28 00 43 05 00 00 00 00 00 00 00 F1 F6 05 03 00 31 00 02 00 02 20 22 02 00 06 00 00 00 01 06 00 00 00 02 00 00 00 68 C6 16
```

末尾的 `00` 是 RCSD 列数 = 0，即"采用后端配置的全部列"。

响应里设备**回显了具体的三列**：

```text
68 3F 00 C3 05 00 00 00 00 00 00 00 85 E3 85 03 00 31 00 02 00 03 00 20 22 02 00 00 20 1E 02 00 00 20 20 02 00 01 01 06 00 00 00 01 1C 07 EA 0A 06 09 00 00 1C 07 EA 0A 06 09 00 05 00 00 3C 8C 16
```

| 字段 | 字节 | 含义 |
| --- | --- | --- |
| `03` | | 表头列数 = 3（**设备回显了具体列**） |
| `00 20 22 02 00` | | 列 1：事件记录序号 |
| `00 20 1E 02 00` | | 列 2：事件发生时间 |
| `00 20 20 02 00` | | 列 3：事件结束时间 |
| `01` | | 结果选择子 1：记录 |
| `01` | | 行数 = 1 |
| `06 00 00 00 01` | | 单元 1：序号 = 1 |
| `1C 07 EA 0A 06 09 00 00` | | 单元 2：2026-10-06 09:00:00.000 |
| `1C 07 EA 0A 06 09 00 05` | | 单元 3：2026-10-06 09:00:00.005（相差 5 毫秒） |

初始化事件没有来源列，所以表头只有 3 列。**请求列数为 0 不代表响应列数也是 0** —— 空 columns 只表示"采用后端配置"，响应必须回显具体列，否则库返回 `resource_limit`。

## 相关配置

| 配置 | 位置 | 说明 |
| --- | --- | --- |
| 设备事件存储深度 | 设备内部参数 | 决定可回溯多少条事件，库不感知 |
| `SessionOptions::limits.max_elements` | 库连接参数 | 限制行数和列数，防恶意响应 |
| `SessionOptions::request_timeout` | 库连接参数 | 事件多时响应可能较大 |

## 常见失败

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| DAR=4 | 设备不提供该事件对象 | 部分表只支持冻结不支持事件 |
| DAR=3 | 设备拒绝该查询 | 事件未启用，或序号区间不被支持 |
| DAR=5 | 属性不是记录型 | 用了普通 GET 而非 GET-Record |
| `resource_limit` | 响应给了行数据但空表头 | 设备实现问题，库的保护性报错 |
| `invalid_value` on 来源列 | 来源列不是 `Null` | 本批只接受 `Null`，需自己处理非 NULL 值 |
| 事件条数比预期少 | 设备存储深度有限，旧的已被覆盖 | 及时拉取，不要长期累积 |
| 序号突然变小 | 设备重启或存储格式化 | 以响应实际序号为准，不要假定单调 |
| 结束时间等于 `Null` 或缺失 | 设备未记录该字段 | 库不补齐，按缺失处理 |
| 拿到的是 `4000` 时钟对象的时间 | 混淆了定位列 | 事件按 `201E` 定位，冻结按 `2021` 定位 |