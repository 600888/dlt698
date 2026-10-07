---
title: 批量读取
description: 用 GET-NormalList 一次读多个属性；按 OAD 分派响应、自动分块的原理与 DAR 逐项处理。
---

# 批量读取

## 能读取什么

逐个调 `get` 读 N 个属性就是 N 次往返。批量读取用**一个 GET-Request（选择子 2）** 把多个属性放进同一个 APDU，设备一次返回全部结果。

| 场景 | 用什么 |
| --- | --- |
| 读 1 个属性 | `get` |
| 读多个属性 | `get_list` |
| 读多个记录对象 | `get_record_list` |

:::tip 什么时候值得批量
- 属性数量 ≥ 3：往返次数是主要开销，批量明显更快
- 属性之间有关联（同一相位的电压和电流）：一次响应保证同一时刻的快照
- 属性只有 1～2 个：直接用 `get`，代码更简单

批量读取**不会改变设备侧的取值时刻语义**。设备仍在收到请求时采样，批量只是减少了网络往返。
:::

## 数据地址表

批量读的地址就是普通 OAD，没有任何特殊之处。参见各数据类别页：

| 想读 | OAD | 参见 |
| --- | --- | --- |
| 电压 | `2000/2/1`～`2000/2/3` | [电压电流功率](./voltage.md) |
| 电流 | `2001/2/1`～`2001/2/3` | [电压电流功率](./voltage.md) |
| 功率 | `2004/2/0` | [电压电流功率](./voltage.md) |
| 功率因数 | `200A/2/0` | [功率因数](./power-factor.md) |
| 频率 | `200F/2/0` | [频率与温度](./frequency-temperature.md) |

## 最小完整示例

一次读三相电压、电流和频率：

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

    auto u = standard::make_oad(oi::voltage, 2, 0, layout);
    auto i = standard::make_oad(oi::current, 2, 0, layout);
    auto f = standard::make_oad(oi::frequency, 2, 0, layout);
    if (!u || !i || !f) {
        std::cerr << "bad oad\n";
        client.disconnect();
        return 1;
    }

    auto response = client.get_list({u.value(), i.value(), f.value()});
    if (!response) {
        std::cerr << response.error().context << '\n';
        client.disconnect();
        return 1;
    }

    // 响应项带回显 OAD，按 OAD 分派而不是依赖请求顺序
    for (const auto& entry : response.value().attributes) {
        // entry.attribute 是回显的 OAD
        if (entry.attribute.oi != oi::voltage || entry.attribute.attribute != 2) {
            continue;
        }
        // result 是 variant：uint8_t 分支是 DAR
        if (const auto dar = std::get_if<std::uint8_t>(&entry.result)) {
            std::cerr << "voltage read failed, DAR=" << unsigned(*dar) << '\n';
            continue;
        }
        const auto& data = std::get<model::Data>(entry.result);
        auto numbers = standard::engineering_values(entry.attribute, data, layout);
        if (numbers) {
            for (std::size_t p = 0; p < numbers.value().size(); ++p) {
                std::cout << "voltage[" << p << "] = "
                          << standard::decimal_text(numbers.value()[p]) << " V\n";
            }
        }
    }

    client.disconnect();
    return 0;
}
```

## 常用变体

### 必须按 OAD 分派，不能按下标

:::danger 这是批量读取最容易出错的地方
`GetResponse::attributes` 里每一项都带**回显的 OAD**（`entry.attribute`）。设备返回的顺序、条数都可能与请求不同 —— 有的设备会省略不支持的属性，有的会调整顺序。

**按下标 `attributes[0]`、`attributes[1]` 取数据是错的。** 正确做法是遍历全部项，按 `entry.attribute` 匹配你要的数据。
:::

```cpp
// 正确：按 OAD 匹配
for (const auto& entry : response.value().attributes) {
    if (entry.attribute != target) continue;
    // ...
}

// 错误：假设顺序一致
const auto& first = response.value().attributes[0];   // 不一定是 voltage
```

### 部分失败不影响其他项

批量读取的**部分成功是常态**：某些属性设备不支持，某些属性此刻读不到。逐项 DAR 才是正确处理方式：

```cpp
int ok = 0, failed = 0;
for (const auto& entry : response.value().attributes) {
    if (std::get_if<std::uint8_t>(&entry.result)) {
        ++failed;
        std::cerr << "oi=" << std::hex << entry.attribute.oi << std::dec
                  << " failed\n";
    } else {
        ++ok;
    }
}
std::cout << ok << " ok, " << failed << " failed\n";
// 库不会因为一项失败就整体返回错误
```

### 请求数量上限

`Limits::max_elements` 默认 65536，同时限制单次 GET 的属性数。实际批量建议控制在**几十个以内**：

- 单个 APDU 超过 `max_frame_bytes`（默认 16385）时，链路层会分帧
- 响应过大时，协议层会启用 **GET-NormalList 分块**（GET-Next），库**自动收齐所有块**

分块对调用方是透明的，`get_list` 返回的是完整结果。但如果响应实在太大，收齐所有块的时间会超过 `request_timeout`，此时应减少单次请求的属性数量。

### 批量读记录对象

多个记录对象用 `get_record_list`：

```cpp
std::vector<protocol::apdu::GetRecord> queries{
    standard::record_sequences(oi::daily_freeze, 1, 4, columns, layout).value(),
    standard::record_sequences(oi::meter_power_down_event, 1, 3, {}, layout).value(),
};
auto response = client.get_record_list(queries);
// response.value().records[i] 对应 queries[i]
```

**响应按查询顺序返回**，每个 `RecordResult` 仍带自己的回显 OAD 和列定义。某个查询失败时该位置的 `result` 是 DAR，不影响其他查询。

## 请求与响应报文

以下示例固定：SA 六个 `00` 字节、逻辑地址 0、CA=0、PIID=0、控制域请求 `43`／响应 `C3`。

### 一次读五个属性

请求（选择子 2 = GET-NormalList）：

```text
68 28 00 43 05 00 00 00 00 00 00 00 F1 F6 05 02 00 05 20 00 02 00 20 01 02 01 20 04 02 01 20 0A 02 01 20 0F 02 00 00 85 BA 16
```

| 字段 | 字节 | 含义 |
| --- | --- | --- |
| 长度域 | `28 00` | 40 字节数据，完整帧 43 字节 |
| `05 02` | | 服务 5（GET-Request），**选择子 2（GET-NormalList）** |
| `00` | | PIID=0 |
| `05` | | 属性个数 = 5 |
| `20 00 02 00` | | `2000/2/0` 电压整体 |
| `20 01 02 01` | | `2001/2/1` A 相电流 |
| `20 04 02 01` | | `2004/2/1` A 相有功功率 |
| `20 0A 02 01` | | `200A/2/1` A 相功率因数 |
| `20 0F 02 00` | | `200F/2/0` 频率 |

响应：

```text
68 47 00 C3 05 00 00 00 00 00 00 00 E5 01 85 02 00 05 20 00 02 00 01 01 03 12 09 6D 12 09 6E 12 09 6F 20 01 02 01 01 05 00 00 13 88 20 04 02 01 01 05 00 00 EA 60 20 0A 02 01 01 10 03 7E 20 0F 02 00 00 04 00 00 D9 07 16
```

| 字段 | 字节 | 含义 |
| --- | --- | --- |
| `85 02` | | GET-Response，**选择子 2（GET-NormalList）** |
| `00` | | PIID-ACD=0 |
| `05` | | **结果个数 = 5** |
| `20 00 02 00` | | 第 1 项回显 OAD：电压整体 |
| `00` | | 结果选择子 0：**数据** |
| `01` | | 数组元素数 = 1（三相电压） |
| `01 03` | | `Array` + 元素数 3 |
| `12 09 6D` | | `UInt16` 2413 → 241.3 V |
| `12 09 6E` | | 2414 → 241.4 V |
| `12 09 6F` | | 2415 → 241.5 V |
| `20 01 02 01` | | 第 2 项：A 相电流 |
| `00` | | 结果选择子 0：数据 |
| `01 05 00 00 13 88` | | `Int32` 5000 → 5.000 A |
| `20 04 02 01` | | 第 3 项：A 相有功功率 |
| `00` | | 结果选择子 0：数据 |
| `01 05 00 00 EA 60` | | `Int32` 60000 → 6000 W |
| `20 0A 02 01` | | 第 4 项：A 相功率因数 |
| `00` | | 结果选择子 0：数据 |
| `01 10 03 7E` | | `Int16` 894 → 0.894 |
| `20 0F 02 00` | | 第 5 项：频率 |
| `00` | | 结果选择子 0：数据 |
| `04 00 00` | | `UInt16` 5000 → 50.00 Hz |

关键点：

- **每一项前面都有回显 OAD**，这是按 OAD 分派的依据
- 每项的第二个字节是**结果选择子**：`00` = 数据，`01` = DAR
- 上例 5 项全部成功；若某项失败，该位置会是 `01 <DAR>`

### 混合成功与失败

把一个设备可能不支持的对象放进批量请求。请求（电压、状态字、频率）：

```text
68 20 00 43 05 00 00 00 00 00 00 00 5B 4A 05 02 00 03 20 00 02 00 20 14 02 00 20 0F 02 00 00 78 4A 16
```

响应（电压和频率正常，状态字 DAR=4）：

```text
68 33 00 C3 05 00 00 00 00 00 00 00 7A 01 85 02 00 03 20 00 02 00 01 01 03 12 09 6D 12 09 6E 12 09 6F 20 14 02 00 00 04 20 0F 02 00 01 12 13 88 00 00 1F 81 16
```

| 字段 | 字节 | 含义 |
| --- | --- | --- |
| `85 02` | | GET-Response，选择子 2 |
| `00 03` | | 结果个数 = 3 |
| `20 00 02 00` | | 第 1 项回显 OAD：电压 |
| `00` | | 结果选择子 0：**数据** |
| `01 01 03 12 09 6D 12 09 6E 12 09 6F` | | 三相电压数组 |
| `20 14 02 00` | | 第 2 项回显 OAD：状态字 |
| `01` | | **结果选择子 1：DAR** |
| `04` | | DAR=4：对象不存在 |
| `20 0F 02 00` | | 第 3 项回显 OAD：频率 |
| `00` | | 结果选择子 0：数据 |
| `01 12 13 88` | | `UInt16` 5000 → 50.00 Hz |

关键点：**结果个数仍是 3**，失败项也在列表里，只是结果选择子变成 `01` 且紧跟一个 DAR 字节。库**不会**因为这一项失败而让整个 `get_list` 返回错误 —— 外层 `Result` 成功只表示"请求-响应往返完成"，逐项结果要看 `AttributeResult::result`。

这也说明为什么必须**遍历并按回显 OAD 匹配**，而不能假设 `attributes[i]` 对应请求的第 i 项。

## 相关配置

| 配置 | 位置 | 说明 |
| --- | --- | --- |
| `Limits::max_elements` | 库连接参数 | 单次 GET 的属性数上限，默认 65536 |
| `Limits::max_frame_bytes` | 库连接参数 | 完整帧字节上限，默认 16385 |
| `SessionOptions::request_timeout` | 库连接参数 | 分块收齐的总超时时间，批量大时需调大 |
| `SessionOptions::prefer_get_blocks` | 库连接参数 | 是否允许协议层分块，默认 true |
| `DeviceLayout::wiring` | 库本地配置 | 决定电压数组长度是 3（三相）还是 1（单相） |

## 常见失败

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| 取到的数据串位 | 按下标取 `attributes[i]` | 必须按 `entry.attribute` 匹配 |
| 某项缺失 | 设备省略了不支持的属性 | 遍历时判断是否存在，别假设条数 |
| 整个 `get_list` 返回错误 | 链路层失败或本地资源超限 | 看 `error().context`，不是设备 DAR |
| 响应超时 | 批量太大，分块收齐超时 | 减少单次属性数，或调大 `request_timeout` |
| `resource_limit` | 超过 `max_elements` 或 `max_frame_bytes` | 拆成多次请求 |
| 电压数组长度是 1 而不是 3 | `DeviceLayout::wiring` 设成了单相 | 与设备实际接线一致 |
| 部分项 DAR=4 | 设备不支持该属性 | 逐项容错，不要整体失败 |
| 部分项 DAR=3 | 设备拒绝该读取 | 检查属性是否可读、是否需要更高权限 |
| 内存暴涨 | 单次读了超大数组 | 检查请求的 OAD 是否指向了整体大数组 |