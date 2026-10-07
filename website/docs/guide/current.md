---
title: 电流读取
description: 三相电流瞬时值、整体数组、零线电流。
---

# 电流读取

## 能读取什么

电流是**瞬时量**，按相别分为 A、B、C 三相。属性 2 是数值组：

- 索引 0：整个数组（三相全部）
- 索引 1/2/3：分别是 A/B/C 相

**电流没有"总"这一项**，三相电流求和没有业务含义。

| 业务含义 | OI | 类型 | 倍率 | 单位 |
| --- | --- | --- | --- | --- |
| 电流 | `2001` | `Int32` | -3 | A |
| 零线电流 | 属性 4 | `Int32` | -3 | A |

电流相关的另外两个对象：

| 业务含义 | OI | 说明 |
| --- | --- | --- |
| 电流波形失真度 | `200C` | 见[谐波与波形失真度](./harmonics.md) |
| 电流谐波含有量 | `200E` | 见[谐波与波形失真度](./harmonics.md) |

## 数据地址表

| 属性 | 名称 | 类型 | 倍率 | 单位 | 读写 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 逻辑名 | `OctetString` | — | — | 只读 | 通常不读 |
| 2 | 数值 | Array\<`Int32`\> | -3 | A | 只读 | 索引 0 整体，1/2/3 = A/B/C |
| 3 | 换算及单位 | `ScalerUnit` | — | — | 只读 | 设备声明的倍率和单位 |
| 4 | 零线电流 | `Int32` | -3 | A | 只读 | 三相四线才有意义 |

数组长度 = `wiring == three_phase ? 3 : 1`。

:::warning 电流是 `Int32`，不是 `UInt32`
电流**有符号**：反向流动时读数为负。线上 `Int32` 5000 是 +5.000 A，`Int32` -5000 是 -5.000 A。用无符号类型解析会把负电流显示成巨大的正数。
:::

**单位是 A，倍率 -3 是毫安级分辨率**，线上 5000 是 5.000 A。

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

    auto oad = standard::make_oad(oi::current, 2, 0, layout);
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
        std::cerr << "meter does not provide current, DAR=" << unsigned(*dar) << '\n';
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
        std::cout << "current " << kPhases[i] << " = "
                  << standard::decimal_text(numbers.value()[i]) << ' '
                  << standard::unit_symbol(numbers.value()[i].scaling.unit) << '\n';
    // 输出：
    // current A = 5.000 A
    // current B = 4.980 A
    // current C = 5.010 A

    client.disconnect();
    return 0;
}
```

## 常用变体

### 读单相电流

```cpp
auto a = standard::phase_oad(oi::current, standard::Phase::a, layout); // 2001/2/1
auto b = standard::phase_oad(oi::current, standard::Phase::b, layout); // 2001/2/2
auto c = standard::phase_oad(oi::current, standard::Phase::c, layout); // 2001/2/3

auto result = client.get_list({a.value(), b.value(), c.value()});
```

### 读零线电流

零线电流是**独立属性**，不是数组元素，索引固定 0：

```cpp
auto oad = standard::make_oad(oi::current, 4, 0, layout); // 2001/4/0
auto value = client.get(oad.value());
if (value && !std::get_if<std::uint8_t>(&value.value())) {
    const auto& data = std::get<model::Data>(value.value());
    auto numbers = standard::engineering_values(oad.value(), data, layout);
    if (numbers && !numbers.value().empty())
        std::cout << "neutral = "
                  << standard::decimal_text(numbers.value()[0]) << ' '
                  << standard::unit_symbol(numbers.value()[0].scaling.unit) << '\n';
    // Int32 1200 → 1.200 A
}
```

:::tip 三相不平衡度检测
把三相电流和零线电流一起读，就能算不平衡度：

```cpp
auto phases = standard::make_oad(oi::current, 2, 0, layout);
auto neutral = standard::make_oad(oi::current, 4, 0, layout);
// 正常三相四线：零线电流接近 0 A
// 零线电流明显不为 0：可能存在接线错误或单相负荷
```

**不是所有设备都提供属性 4**，不支持时返回 DAR=4，属正常。
:::

## 请求与响应报文

以下示例固定：SA 六个 `00` 字节、逻辑地址 0、CA=0、PIID=0、控制域请求 `43`／响应 `C3`。

### 读取 A 相电流

请求（`2001/2/1`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 20 01 02 01 00 86 BB 16
```

响应（`Int32` 5000，即 5.000 A）：

```text
68 1E 00 C3 05 00 00 00 00 00 00 00 9E 54 85 01 00 20 01 02 01 01 05 00 00 13 88 00 00 DC DF 16
```

| 字段 | 字节 | 含义 |
| --- | --- | --- |
| `85` | | 服务：GET-Response |
| `01` | | GET-Normal |
| `00` | | PIID-ACD=0 |
| `20 01 02 01` | | 回显 OAD：电流，属性 2，索引 1（A 相） |
| `01` | | 选择子：Data |
| `05` | | 数据类型：`Int32` |
| `00 00 13 88` | | 原始值 5000 |
| `00` / `00` | | FollowReport / TimeTag 不存在 |

注意数据类型是 `05`（`Int32`），不是电压用的 `12`（`UInt16`）。

### 读取三相电流数组

请求（`2001/2/0`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 20 01 02 00 00 5E A2 16
```

响应（三项 `Int32`）：

```text
68 2A 00 C3 05 00 00 00 00 00 00 00 04 99 85 01 00 20 01 02 00 01 01 03 05 00 00 13 88 05 00 00 13 74 05 00 00 13 92 00 00 DE 7C 16
```

解析：`01`（Data）→ `01`（数组）→ `03`（3 项）→ 3 个 `05`（Int32）：

| 索引 | 原始字节 | 原始值 | 工程值 |
| --- | --- | --- | --- |
| 1 A 相 | `00 00 13 88` | 5000 | 5.000 A |
| 2 B 相 | `00 00 13 74` | 4980 | 4.980 A |
| 3 C 相 | `00 00 13 92` | 5010 | 5.010 A |

### 读取零线电流

请求（`2001/4/0`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 20 01 04 00 00 87 74 16
```

响应（`Int32` 1200，即 1.200 A）：

```text
68 1E 00 C3 05 00 00 00 00 00 00 00 9E 54 85 01 00 20 01 04 00 01 05 00 00 04 B0 00 00 1F AE 16
```

## 相关配置

| 配置 | 位置 | 说明 |
| --- | --- | --- |
| `DeviceLayout::wiring` | 库本地配置 | 三相为 3 项，单相为 1 项 |
| `SessionOptions::request_timeout` | 库连接参数 | 单次 GET 超时，默认 5 秒 |

## 常见失败

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| DAR=4 读属性 4 | 设备没有零线电流测量 | 三相三线设备通常不提供，属正常 |
| 负电流显示成大正数 | 按 `UInt32` 解析 | 电流是 `Int32`，`engineering_values` 会保留符号 |
| 数组长度校验失败 | `wiring` 配错 | 单相表按三相校验会失败 |
| 三相电流都是 0 | 设备只累计不测量，或未接入互感器 | 纯计量型终端可能不提供实时量 |
| 值比预期大 1000 倍 | 把原始整数当成了工程值 | 线上 5000 是 5.000 A，倍率 -3 |