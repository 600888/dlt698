---
title: 电压读取
description: 三相电压瞬时值、整体数组、相别索引；电压波形失真度和电压合格率参数。
---

# 电压读取

## 能读取什么

电压是**瞬时量**，按相别分为 A、B、C 三相。属性 2 是数值组：

- 索引 0：整个数组（三相全部，一次读完）
- 索引 1/2/3：分别是 A/B/C 相

**电压没有"总"这一项**，三相电压的合成没有业务含义，所以不存在"总电压"。

| 业务含义 | OI | 类型 | 倍率 | 单位 |
| --- | --- | --- | --- | --- |
| 电压 | `2000` | `UInt16` | -1 | V |

电压相关的另外两个对象：

| 业务含义 | OI | 说明 |
| --- | --- | --- |
| 电压波形失真度 | `200B` | 各相 THD，见[谐波与波形失真度](./harmonics.md) |
| 电压合格率参数 | `4030` | 考核/合格上下限，见[电表参数读取与设置](./meter-parameters.md) |

## 数据地址表

| 属性 | 名称 | 类型 | 倍率 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- |
| 1 | 逻辑名 | `OctetString` | — | — | 通常不读 |
| 2 | 数值 | Array\<`UInt16`\> | -1 | V | 索引 0 整体，1/2/3 = A/B/C |
| 3 | 换算及单位 | `ScalerUnit` | — | — | 设备声明的倍率和单位 |

数组长度 = `wiring == three_phase ? 3 : 1`。单相表只有 1 项，对应 A 相位置。

**单位是 V，倍率 -1 已经得到伏特**，线上 2413 是 241.3 V，不是 24130 V。

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

    // 一次读三相电压：索引 0 读整个数组
    auto oad = standard::make_oad(oi::voltage, 2, 0, layout);
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
        std::cerr << "meter does not provide voltage, DAR=" << unsigned(*dar) << '\n';
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
    static const char* kPhases[] = {"A", "B", "C"};
    for (std::size_t i = 0; i < numbers.value().size(); ++i)
        std::cout << "voltage " << kPhases[i] << " = "
                  << standard::decimal_text(numbers.value()[i]) << ' '
                  << standard::unit_symbol(numbers.value()[i].scaling.unit) << '\n';
    // 输出：
    // voltage A = 241.3 V
    // voltage B = 241.4 V
    // voltage C = 241.5 V

    client.disconnect();
    return 0;
}
```

## 常用变体

### 读单相

用 `phase_oad` 构造相别点位：

```cpp
auto a = standard::phase_oad(oi::voltage, standard::Phase::a, layout); // 2000/2/1
auto b = standard::phase_oad(oi::voltage, standard::Phase::b, layout); // 2000/2/2
auto c = standard::phase_oad(oi::voltage, standard::Phase::c, layout); // 2000/2/3

auto result = client.get_list({a.value(), b.value(), c.value()});
```

单相表用 `Wiring::single_phase`，此时读 B/C 相会被 `phase_oad` 直接拒绝：

```cpp
const standard::DeviceLayout single{standard::Wiring::single_phase, 4, 21};
auto b = standard::phase_oad(oi::voltage, standard::Phase::b, single);
// b.error().code == ErrorCode::invalid_value，单相表没有 B 相
```

### 电压偏差判断

配合 `4104` 额定电压判断偏差率：

```cpp
auto rated = standard::make_oad(oi::rated_voltage, 2, 0, layout);
auto measured = standard::make_oad(oi::voltage, 2, 1, layout);
```

额定电压是 `VisibleString`（如 `"220V"`），不是数值，比较前要先解析字符串。

## 请求与响应报文

以下示例固定：SA 六个 `00` 字节、逻辑地址 0、CA=0、PIID=0、控制域请求 `43`／响应 `C3`。

### 读取 A 相电压

请求（`2000/2/1`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 20 00 02 01 00 3D A7 16
```

响应（`UInt16` 2413，即 241.3 V）：

```text
68 1C 00 C3 05 00 00 00 00 00 00 00 BC FF 85 01 00 20 00 02 01 01 12 09 6D 00 00 DE F1 16
```

| 字段 | 字节 | 含义 |
| --- | --- | --- |
| `85` | | 服务：GET-Response |
| `01` | | GET-Normal |
| `00` | | PIID-ACD=0 |
| `20 00 02 01` | | 回显 OAD：电压，属性 2，索引 1（A 相） |
| `01` | | 选择子：Data |
| `12` | | 数据类型：`UInt16` |
| `09 6D` | | 原始值 2413 |
| `00` / `00` | | FollowReport / TimeTag 不存在 |

`09 6D` = 2413，乘以 10^-1 得 **241.3 V**。

### 读取三相电压数组

请求（`2000/2/0`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 20 00 02 00 00 E5 BE 16
```

响应（三项 `UInt16`）：

```text
68 24 00 C3 05 00 00 00 00 00 00 00 D9 D0 85 01 00 20 00 02 00 01 01 03 12 09 6D 12 09 6E 12 09 6F 00 00 D7 FA 16
```

解析：`01`（Data）→ `01`（数组）→ `03`（3 项）→ 3 个 `12`（UInt16）：

| 索引 | 原始字节 | 原始值 | 工程值 |
| --- | --- | --- | --- |
| 1 A 相 | `09 6D` | 2413 | 241.3 V |
| 2 B 相 | `09 6E` | 2414 | 241.4 V |
| 3 C 相 | `09 6F` | 2415 | 241.5 V |

### 读取额定电压

请求（`4104/2/0`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 41 04 02 00 00 FE 66 16
```

响应（`VisibleString` `"220V"`）：

```text
68 1F 00 C3 05 00 00 00 00 00 00 00 0F 01 85 01 00 41 04 02 00 01 0A 04 32 32 30 56 00 00 F7 38 16
```

`0A` 是 `VisibleString`，`04` 是字节数，后面 `32 32 30 56` 是 ASCII `"220V"`。

## 相关配置

| 配置 | 位置 | 说明 |
| --- | --- | --- |
| `DeviceLayout::wiring` | 库本地配置 | 三相为 3 项，单相为 1 项；必须与设备一致 |
| `SessionOptions::request_timeout` | 库连接参数 | 单次 GET 超时，默认 5 秒 |

## 常见失败

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| DAR=4 | 设备不提供电压对象 | 纯脉冲式输出设备可能没有电压测量 |
| 数组长度校验失败 | `wiring` 配错 | 单相表按三相校验会失败 |
| `phase_oad` 对 B/C 相返回 `invalid_value` | 单相配置 | 属正常拒绝，改用 `Wiring::three_phase` 前先确认设备接线 |
| 值比预期大 10 倍 | 把原始整数当成了工程值 | 线上 2413 已是 0.1 V 单位，必须走 `engineering_values` |
| `unit_symbol` 返回空字符串 | 设备单位枚举未收录 | 附录 B 未收录的单位返回空视图，按 OAD 表的单位处理 |