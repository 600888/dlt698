---
title: 功率读取
description: 有功、无功、视在功率；总量加三相的数组布局；符号约定。
---

# 功率读取

## 能读取什么

功率是**瞬时量**，分为有功、无功、视在三种：

| 业务含义 | OI | 类型 | 倍率 | 单位 |
| --- | --- | --- | --- | --- |
| 有功功率 | `2004` | `Int32` | -1 | W |
| 无功功率 | `2005` | `Int32` | -1 | var |
| 视在功率 | `2006` | `Int32` | -1 | VA |

:::tip 功率有"总"，电压和电流没有
功率对象的数组是 **总量 + 三相**，长度 = `wiring == three_phase ? 4 : 2`：

| 索引 | 含义 |
| --- | --- |
| 1 | 总量 |
| 2 / 3 / 4 | A / B / C 相 |

单相表仍然有"总量"和"A 相"两项。电压、电流只有三相、没有总量，详见[电压](./voltage.md)和[电流](./current.md)。
:::

相别表达用 `phase_oad`：

```cpp
standard::phase_oad(oi::active_power, standard::Phase::total, layout); // 2004/2/1
standard::phase_oad(oi::active_power, standard::Phase::a, layout);     // 2004/2/2
```

## 数据地址表

三个功率对象结构相同：

| 属性 | 名称 | 类型 | 倍率 | 单位 | 读写 |
| --- | --- | --- | --- | --- | --- |
| 1 | 逻辑名 | `OctetString` | — | — | 只读 |
| 2 | 数值 | Array\<`Int32`\> | -1 | W / var / VA | 只读 |
| 3 | 换算及单位 | `ScalerUnit` | — | — | 只读 |

:::warning 功率一律是 `Int32`
有功、无功、视在功率全部是 `Int32`：

- 有功：正向为正、反送为负
- 无功：符号按象限约定
- 视在：通常为非负，但类型仍是有符号

**单位分别是 W、var、VA，不是 kW**。倍率 -1 表示线上 60000 是 6000.0 W。库把它换算成 kW 是另一套地址（[当前需量](./demand.md) 用 kW），不要混。
:::

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

    // 有功功率整体数组：总量 + A/B/C
    auto oad = standard::make_oad(oi::active_power, 2, 0, layout);
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
        std::cerr << "meter does not provide active power, DAR=" << unsigned(*dar) << '\n';
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
    static const char* kLabels[] = {"total", "A", "B", "C"};
    for (std::size_t i = 0; i < numbers.value().size(); ++i)
        std::cout << "active power " << kLabels[i] << " = "
                  << standard::decimal_text(numbers.value()[i]) << ' '
                  << standard::unit_symbol(numbers.value()[i].scaling.unit) << '\n';
    // 输出：
    // active power total = 6000.0 W
    // active power A = 2010.0 W
    // active power B = 1990.0 W
    // active power C = 2000.0 W

    client.disconnect();
    return 0;
}
```

## 常用变体

### 同时读三种功率

```cpp
auto active   = standard::make_oad(oi::active_power, 2, 0, layout);
auto reactive = standard::make_oad(oi::reactive_power, 2, 0, layout);
auto apparent = standard::make_oad(oi::apparent_power, 2, 0, layout);

auto result = client.get_list({active.value(), reactive.value(), apparent.value()});
```

### 只读某一相

```cpp
auto a = standard::phase_oad(oi::reactive_power, standard::Phase::a, layout); // 2005/2/2
```

### 主动功率和视在功率算功率因数

库已经提供 `200A` 直接读功率因数，不必自己算；但如果你要做一致性校验：

```cpp
// 有功 2010.0 W、视在 6500.0 VA → 2010/6500 ≈ 0.309
// 设备直接读出的 200A 才是权威值，两者不一致说明设备配置或倍率有差异
```

**优先读 `200A`**，自己算的结果和设备读数不一致时，以设备为准，见[功率因数读取](./power-factor.md)。

## 请求与响应报文

以下示例固定：SA 六个 `00` 字节、逻辑地址 0、CA=0、PIID=0、控制域请求 `43`／响应 `C3`。

### 读取总有功功率

请求（`2004/2/1`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 20 04 02 01 00 D1 D5 16
```

响应（`Int32` 60000，即 6000.0 W）：

```text
68 1E 00 C3 05 00 00 00 00 00 00 00 9E 54 85 01 00 20 04 02 01 01 05 00 00 EA 60 00 00 33 95 16
```

| 字段 | 字节 | 含义 |
| --- | --- | --- |
| `85` | | 服务：GET-Response |
| `01` | | GET-Normal |
| `00` | | PIID-ACD=0 |
| `20 04 02 01` | | 回显 OAD：有功功率，属性 2，索引 1（总量） |
| `01` | | 选择子：Data |
| `05` | | 数据类型：`Int32` |
| `00 00 EA 60` | | 原始值 60000 |
| `00` / `00` | | FollowReport / TimeTag 不存在 |

### 读取有功功率数组

请求（`2004/2/0`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 20 04 02 00 00 09 CC 16
```

响应（四项 `Int32`：总量、A、B、C）：

```text
68 2F 00 C3 05 00 00 00 00 00 00 00 C0 92 85 01 00 20 04 02 00 01 01 04 05 00 00 EA 60 05 00 00 4E 84 05 00 00 4D BC 05 00 00 4E 20 00 00 79 9C 16
```

解析：`01`（Data）→ `01`（数组）→ `04`（4 项）：

| 索引 | 原始字节 | 原始值 | 工程值 |
| --- | --- | --- | --- |
| 1 总量 | `00 00 EA 60` | 60000 | 6000.0 W |
| 2 A 相 | `00 00 4E 84` | 20100 | 2010.0 W |
| 3 B 相 | `00 00 4D BC` | 19900 | 1990.0 W |
| 4 C 相 | `00 00 4E 20` | 20000 | 2000.0 W |

分相之和 2010 + 1990 + 2000 = 6000 W，正好等于总量。**这只是该示例数据的关系**；实际现场分相之和通常因线路损耗略小于总量。

### 读取 A 相无功功率

请求（`2005/2/2`，索引 2 = A 相）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 20 05 02 02 00 02 E3 16
```

响应（`Int32` 5000，即 500.0 var）：

```text
68 1E 00 C3 05 00 00 00 00 00 00 00 9E 54 85 01 00 20 05 02 02 01 05 00 00 13 88 00 00 8E 57 16
```

### 读取 A 相视在功率

请求（`2006/2/2`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 20 06 02 02 00 CF C6 16
```

响应（`Int32` 65000，即 6500.0 VA）：

```text
68 1E 00 C3 05 00 00 00 00 00 00 00 9E 54 85 01 00 20 06 02 02 01 05 00 00 FD E8 00 00 B8 B6 16
```

## 相关配置

| 配置 | 位置 | 说明 |
| --- | --- | --- |
| `DeviceLayout::wiring` | 库本地配置 | 三相为 4 项，单相为 2 项 |
| `SessionOptions::request_timeout` | 库连接参数 | 单次 GET 超时，默认 5 秒 |

## 常见失败

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| 数组长度校验失败 | `wiring` 配错 | 单相表按三相四线校验会失败（期望 2 项，实际 4 项） |
| 期望 4 项但返回 3 项 | 误按电压的相别规则理解 | 功率数组含"总量"，长度是 `wiring == 三相 ? 4 : 2` |
| 数值比预期大 1000 倍 | 把 W 当成了 kW | `2004` 是 W；kW 读的是[当前需量](./demand.md) `2017` |
| 无功符号与预期相反 | 各厂家象限约定不同 | 库保留设备原始符号，不做翻转 |
| 电压电流齐全但功率 DAR=4 | 设备不支持功率测量 | 只做累计的终端不提供瞬时功率 |