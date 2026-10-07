---
title: 设备信息读取
description: 通信地址、表号、客户编号、资产管理编码、额定电压电流、准确度等级、电能表型号。
---

# 设备信息读取

## 能读取什么

设备信息是一组**字符串型参数**，主要用于台账、资产管理和配置核对：

| 业务含义 | OI | 类型 | 定长 | 说明 |
| --- | --- | --- | --- | --- |
| 通信地址 | `4001` | `OctetString` | 6 字节 | 设备在 698 网络中的地址 |
| 表号 | `4002` | `OctetString` | — | 电能表出厂编号 |
| 客户编号 | `4003` | `OctetString` | — | 用户/户号标识 |
| 资产管理编码 | `4103` | `VisibleString` | 32 字节 | 电网资产编码 |
| 额定电压 | `4104` | `VisibleString` | 6 字节 | 如 `"220V"` |
| 额定电流/基本电流 | `4105` | `VisibleString` | 6 字节 | 如 `"5(60)A"` |
| 最大电流 | `4106` | `VisibleString` | 6 字节 | 如 `"60A"` |
| 有功准确度等级 | `4107` | `VisibleString` | 4 字节 | 如 `"1.0S"` |
| 无功准确度等级 | `4108` | `VisibleString` | 4 字节 | 如 `"2.0L"` |
| 电能表型号 | `410B` | `VisibleString` | 32 字节 | 厂商型号 |

:::warning `OctetString` 和 `VisibleString` 完全不同
| 类型 | 线上编码 | 内容 |
| --- | --- | --- |
| `OctetString`（类型 `09`） | **十六进制字节** | `12 34` 是两个字节 0x12、0x34，**不可打印** |
| `VisibleString`（类型 `0A`） | **ASCII 码** | `"220V"` 是 `32 32 30 56`，可读文本 |

通信地址、表号、客户编号是 `OctetString`，**直接按十六进制打印会得到乱码**。资产管理编码、型号等是 `VisibleString`，是 ASCII 文本。
:::

## 数据地址表

这些对象结构相同，只有属性 1 和 2：

| 属性 | 名称 | 类型 | 说明 |
| --- | --- | --- | --- |
| 1 | 逻辑名 | `OctetString` | 通常不读 |
| 2 | 参数值 | 见上表 | 索引固定 0 |

**全部索引固定 0**，没有数组实例。

定长约束：

| 参数 | 定长 | 超长会怎样 |
| --- | --- | --- |
| 资产管理编码 `4103` | 32 字节 | `validate_value` 返回 `invalid_length` |
| 额定电压 `4104` | 6 字节 | 同上 |
| 额定电流 `4105` | 6 字节 | 同上 |
| 最大电流 `4106` | 6 字节 | 同上 |
| 准确度等级 `4107`/`4108` | 4 字节 | 同上 |
| 电能表型号 `410B` | 32 字节 | 同上 |

表号和客户编号**没有定长约束**。

## 最小完整示例

一次读全设备信息：

```cpp
#include <dlt698/app.hpp>
#include <dlt698/standard/catalog.hpp>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {
// OctetString 是原始字节，编成十六进制便于和设备配置对照。
std::string to_hex(const model::OctetString& value) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (std::uint8_t byte : value.value) out << std::setw(2) << unsigned(byte);
    return out.str();
}
}  // namespace

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

    struct Field { std::uint16_t oi; const char* label; };
    const Field fields[] = {
        {oi::communication_address, "communication address"},
        {oi::meter_number, "meter number"},
        {oi::customer_number, "customer number"},
        {oi::asset_code, "asset code"},
        {oi::rated_voltage, "rated voltage"},
        {oi::rated_current, "rated current"},
        {oi::maximum_current, "maximum current"},
        {oi::active_accuracy_class, "active accuracy class"},
        {oi::reactive_accuracy_class, "reactive accuracy class"},
        {oi::meter_model, "meter model"},
    };

    for (const auto& field : fields) {
        auto oad = standard::make_oad(field.oi, 2, 0, layout);
        if (!oad) {
            std::cerr << field.label << ": " << oad.error().context << '\n';
            continue;
        }
        auto value = client.get(oad.value());
        if (!value) {
            std::cerr << field.label << ": " << value.error().context << '\n';
            continue;
        }
        if (const auto dar = std::get_if<std::uint8_t>(&value.value())) {
            std::cerr << field.label << ": unsupported, DAR=" << unsigned(*dar) << '\n';
            continue;
        }
        const auto& data = std::get<model::Data>(value.value());
        // 必须按实际类型分支，OctetString 和 VisibleString 都要处理。
        if (data.type() == model::DataType::octet_string) {
            std::cout << field.label << " = " << to_hex(data.as<model::OctetString>()) << '\n';
        } else if (data.type() == model::DataType::visible_string) {
            std::cout << field.label << " = "
                      << data.as<model::VisibleString>().value << '\n';
        } else {
            std::cerr << field.label << ": unexpected type\n";
        }
    }
    // 输出示例：
    // communication address = 123456789012
    // meter number = 1234
    // customer number = abcd
    // asset code = GDZM-2026-000123
    // rated voltage = 220V
    // rated current = 5(60)A
    // maximum current = 60A
    // active accuracy class = 1.0S
    // reactive accuracy class = 2.0L
    // meter model = DTZY-200

    client.disconnect();
    return 0;
}
```

## 常用变体

### 用批量读取减少往返

上面的循环每次一个 GET。用 `get_list` 一次读完：

```cpp
std::vector<model::Oad> oads;
for (const auto& field : fields)
    if (auto oad = standard::make_oad(field.oi, 2, 0, layout)) oads.push_back(oad.value());

auto response = client.get_list(oads);
if (response) {
    for (const auto& entry : response.value().attributes) {
        if (std::get_if<std::uint8_t>(&entry.result)) continue;  // 逐项判 DAR
        const auto& data = std::get<model::Data>(entry.result);
        // 按 entry.attribute.oi 分派
    }
}
```

**响应项带回显 OAD**，按 `entry.attribute.oi` 分派比依赖顺序可靠。

### 通信地址和链路地址的关系

链路帧里的**服务器地址（SA）** 就是通信地址 `4001` 的内容，但**字节序相反**：

```text
4001/2/0 返回 12 34 56 78 90 12
链路上 SA 显示    00 00 00 00 00 00（默认全 0）
```

配置链路时，SA 要填**线序**，见[TCP 连接](./tcp.md)。两者对不上是最常见的连接失败原因。

### 核对额定值

```cpp
auto rated_voltage = standard::make_oad(oi::rated_voltage, 2, 0, layout);   // "220V"
auto rated_current = standard::make_oad(oi::rated_current, 2, 0, layout);   // "5(60)A"
```

这些是**字符串**，不是数值。用于电压偏差判断时需要自己解析：

```cpp
// "220V" → 去掉尾部 'V' → 220
// "5(60)A" → 基本电流 5，最大电流 60
```

库不做字符串到数值的转换。

### 检查通信地址是否为本机

```cpp
auto oad = standard::make_oad(oi::communication_address, 2, 0, layout);
auto value = client.get(oad.value());
if (value && !std::get_if<std::uint8_t>(&value.value())) {
    const auto& data = std::get<model::Data>(value.value());
    if (data.type() == model::DataType::octet_string) {
        const auto& bytes = data.as<model::OctetString>().value;
        // 与连接时配置的 server.bytes 比对（注意线序反转）
    }
}
```

## 请求与响应报文

以下示例固定：SA 六个 `00` 字节、逻辑地址 0、CA=0、PIID=0、控制域请求 `43`／响应 `C3`。

### 读取通信地址

请求（`4001/2/0`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 40 01 02 00 00 ED 03 16
```

响应（6 字节 `123456789012`）：

```text
68 21 00 C3 05 00 00 00 00 00 00 00 1D DB 85 01 00 40 01 02 00 01 09 06 12 34 56 78 90 12 00 00 DB 07 16
```

| 字段 | 字节 | 含义 |
| --- | --- | --- |
| `85` / `01` / `00` | | GET-Response / Normal / PIID 0 |
| `40 01 02 00` | | 回显 OAD |
| `01` | | 选择子：Data |
| `09` | | 数据类型：`OctetString` |
| `06` | | 字节数 6 |
| `12 34 56 78 90 12` | | 内容 |
| `00` / `00` | | FollowReport / TimeTag 不存在 |

### 读取表号

请求（`4002/2/0`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 40 02 02 00 00 20 26 16
```

响应（2 字节 `1234`）：

```text
68 1D 00 C3 05 00 00 00 00 00 00 00 2D AA 85 01 00 40 02 02 00 01 09 02 12 34 00 00 0E 1C 16
```

### 读取客户编号

请求（`4003/2/0`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 40 03 02 00 00 9B 3A 16
```

响应（2 字节 `AB CD`）：

```text
68 1D 00 C3 05 00 00 00 00 00 00 00 2D AA 85 01 00 40 03 02 00 01 09 02 AB CD 00 00 FC B8 16
```

### 读取资产管理编码

请求（`4103/2/0`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 41 03 02 00 00 DF 31 16
```

响应（16 字节 ASCII）：

```text
68 2B 00 C3 05 00 00 00 00 00 00 00 95 CC 85 01 00 41 03 02 00 01 0A 10 47 44 5A 4D 2D 32 30 32 36 2D 30 30 30 31 32 33 00 00 42 EF 16
```

数据类型是 `0A`（`VisibleString`），长度 `10` 十六进制 = 16 字节，内容 `47 44 5A 4D 2D ...` 是 `"GDZM-2026-000123"` 的 ASCII 码。

### 读取额定电压

请求（`4104/2/0`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 41 04 02 00 00 FE 66 16
```

响应（`"220V"`）：

```text
68 1F 00 C3 05 00 00 00 00 00 00 00 0F 01 85 01 00 41 04 02 00 01 0A 04 32 32 30 56 00 00 F7 38 16
```

### 读取额定电流

请求（`4105/2/0`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 41 05 02 00 00 45 7A 16
```

响应（`"5(60)A"`，6 字节）：

```text
68 21 00 C3 05 00 00 00 00 00 00 00 1D DB 85 01 00 41 05 02 00 01 0A 06 35 28 36 30 29 41 00 00 47 0E 16
```

`35 28 36 30 29 41` 是 `"5(60)A"` 的 ASCII。定长 6 字节刚好。

### 读取最大电流

请求（`4106/2/0`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 41 06 02 00 00 88 5F 16
```

响应（`"60A"`）：

```text
68 1E 00 C3 05 00 00 00 00 00 00 00 9E 54 85 01 00 41 06 02 00 01 0A 03 36 30 41 00 00 98 0D 16
```

### 读取有功准确度等级

请求（`4107/2/0`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 41 07 02 00 00 33 43 16
```

响应（`"1.0S"`）：

```text
68 1F 00 C3 05 00 00 00 00 00 00 00 0F 01 85 01 00 41 07 02 00 01 0A 04 31 2E 30 53 00 00 A9 49 16
```

### 读取无功准确度等级

请求（`4108/2/0`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 41 08 02 00 00 CA F1 16
```

响应（`"2.0L"`）：

```text
68 1F 00 C3 05 00 00 00 00 00 00 00 0F 01 85 01 00 41 08 02 00 01 0A 04 32 2E 30 4C 00 00 F2 02 16
```

### 读取电能表型号

请求（`410B/2/0`）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 41 0B 02 00 00 07 D4 16
```

响应（`"DTZY-200"`）：

```text
68 23 00 C3 05 00 00 00 00 00 00 00 3F 70 85 01 00 41 0B 02 00 01 0A 08 44 54 5A 59 2D 32 30 30 00 00 57 BA 16
```

## 相关配置

| 配置 | 位置 | 说明 |
| --- | --- | --- |
| `ClientOptions::protocol.server.bytes` | 库连接参数 | 链路上使用的 SA，要与设备通信地址的**线序**一致 |
| `SessionOptions::request_timeout` | 库连接参数 | 单次 GET 超时，默认 5 秒 |

## 常见失败

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| 通信地址打印成乱码 | 把 `OctetString` 当文本 | 它是原始字节，编成十六进制 |
| `as<VisibleString>()` 抛异常 | 实际是 `OctetString` | 先用 `data.type()` 判断 |
| DAR=4 | 设备不提供该参数 | 台账类参数并非必备 |
| 字符串长度与定长不符 | 校验失败或设备实现不同 | 标准定长见上表；设备不守规矩时读取会失败 |
| 连不上但参数看起来对 | SA 线序反了 | 链路 SA 与 `4001` 内容字节序相反，见[TCP 连接](./tcp.md) |
| 型号/编码里有非 ASCII | 设备误用 `VisibleString` | 按原始字节处理 |