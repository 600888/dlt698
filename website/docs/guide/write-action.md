---
title: 写入与调用方法
description: 用 SET 写属性、ACTION 调方法；两层写权限的区别、DAR 处理与可回滚的写法。
---

# 写入与调用方法

## 能读取什么

| 服务 | 选择子 | 用途 | 客户端方法 |
| --- | --- | --- | --- |
| SET | 1 | 写属性值 | `set` / `set_list` |
| ACTION | 2 | 调用方法（可带参数、可有返回） | `action` / `action_list` |

**写权限默认关闭。** 库的标准目录只声明了有限的写属性，实际设备能写什么由设备点表决定。

### 两种"能不能写"

这是最容易混淆的地方 —— "能写"有两层完全不同的含义：

| 层 | 由谁决定 | 失败表现 |
| --- | --- | --- |
| **库的 schema 是否允许写** | 你在服务端/模拟端声明的 `AttributeSchema::writable` | 请求根本不会被处理，或本地 `invalid_value` |
| **设备是否接受这个写入** | 设备的点表、权限、当前状态 | 远端返回非零 DAR |

`AttributeSchema::writable` **默认是 `false`**：

```cpp
struct AttributeSchema {
    std::uint8_t number = 2;
    model::DataType type = model::DataType::null;
    bool readable = true;
    bool writable = false;  ///< 写入须显式授权，已有读取 schema 不会自动变成可写
    bool record = false;
};
```

**一个属性"能读"不代表"能写"。** 库不会因为某属性可读就默认它可写。

## 数据地址表

常见的可写属性：

| 业务含义 | OAD | 类型 | 说明 |
| --- | --- | --- | --- |
| 日期时间 | `4000/2/0` | `DateTimeS` | 写时钟 |
| 校时模式 | `4000/3/0` | `Enum` | 0/1/2/255 |
| 最大需量周期 | `4100/2/0` | `UInt16` | 分钟 |
| 需量滑差时间 | `4101/2/0` | `UInt16` | 秒 |
| 状态字模式字 | `2014/4/0` | `BitString` | 部分设备可写 |

这些属性的可写性**必须逐个设备确认**。库的标准目录能告诉你属性号和类型，但不代表你的设备开放了写权限。

## 最小完整示例

写校时模式：

```cpp
#include <dlt698/app.hpp>
#include <iostream>

int main() {
    using namespace dlt698;
    namespace oi = standard::oi;

    app::ClientOptions options;
    options.protocol.server.bytes = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    app::Client client(options);
    auto connected = client.connect_tcp("127.0.0.1", 6980);
    if (!connected) {
        std::cerr << connected.error().context << '\n';
        return 1;
    }

    // 校时模式 = 1（自动校时）
    auto dar = client.set(model::Oad{oi::date_time, 3, 0}, model::Data{model::Enum{1}});
    if (!dar) {
        std::cerr << "set failed: " << dar.error().context << '\n';   // 本地错误
        client.disconnect();
        return 1;
    }
    // 外层成功不等于写入成功：dar 可能是非零的协议原码
    if (dar.value() != 0) {
        std::cerr << "device rejected, DAR=" << unsigned(dar.value()) << '\n';
        client.disconnect();
        return 1;
    }

    std::cout << "time sync mode set\n";
    client.disconnect();
    return 0;
}
```

## 常用变体

### 写多个属性

```cpp
auto response = client.set_list({
    {model::Oad{oi::maximum_demand_period, 2, 0}, model::Data{model::UInt16{15}}},
    {model::Oad{oi::sliding_interval, 2, 0}, model::Data{model::UInt16{60}}},
});
if (!response) {
    std::cerr << response.error().context << '\n';
} else {
    for (const auto& item : response.value().attributes) {
        std::cout << "oi=" << std::hex << item.attribute.oi << std::dec
                  << " attr=" << unsigned(item.attribute.attribute)
                  << " DAR=" << unsigned(item.dar) << '\n';
    }
}
```

:::danger 批量写入不回滚
`set_list` 是**逐项独立执行**的。第一项成功、第二项失败时，第一项的写入**不会被撤销**。

需要"全部成功才提交"的语义，必须自己实现：先全部读一遍确认现值 → 逐项写 → 失败时手动写回原值。这个补偿逻辑得你自己写，库不提供事务。
:::

### 调用方法

```cpp
auto result = client.action(model::Omd{0x2000, 1, 0}, model::Data{model::UInt16{42}});
if (!result) {
    std::cerr << "action failed: " << result.error().context << '\n';
} else if (result.value().dar != 0) {
    std::cerr << "device rejected, DAR=" << unsigned(result.value().dar) << '\n';
} else if (result.value().data) {
    // 方法有返回值
    const auto& data = *result.value().data;
    std::cout << "returned type=" << unsigned(data.type()) << '\n';
} else {
    std::cout << "method executed, no return value\n";
}
```

:::tip 库没有内置方法目录
DL/T 698.45 里的方法（OMD）由设备点表定义，**库不预置方法清单**。你需要从设备文档拿到"方法号 + 参数类型 + 返回类型"，库只负责按你给的 OMD 编解码。

这也是本节示例用 `0x2000/1/0` 的原因 —— 那是为了演示编解码而构造的报文，不代表任何真实设备的方法。
:::

### 多方法批量调用

```cpp
auto response = client.action_list({
    {model::Omd{0x2000, 1, 0}, model::Data{model::UInt16{42}}},
    {model::Omd{0x2000, 2, 0}, model::Data{model::UInt8{1}}},
});
```

多参数时 APDU 的"多参数"标志位必须置 1。库的 `set_list` / `action_list` 会自动处理，手工构造 APDU 时需要注意。

### 服务端开放写权限

如果你是**被读的一方**（用 `app::Server` 模拟设备），必须在 schema 里显式声明可写：

```cpp
#include <dlt698/service.hpp>

// 在你的 provider 里
service::ObjectSchema schema;
schema.oi = 0x4000;
schema.name = "date_time";
schema.attributes = {
    {2, model::DataType::date_time_s, true, true, false},   // number, type, readable, writable, record
    {3, model::DataType::enumeration, true, true, false},
};
```

`writable` 保持默认 `false` 时，即使客户端发了 SET，服务端也会拒绝。**读取 schema 不会自动变成可写** —— 这是有意的设计，避免"能读就能改"的意外。

### 方法回调不要捕获 shared_ptr

```cpp
// 正确：用弱引用打破引用环
std::weak_ptr<MyDevice> weak = device;
schema.methods_handler = [weak](const model::Omd& method, const model::Data& param) {
    service::ActionValue out;
    if (auto locked = weak.lock()) {
        out = locked->handle(method, param);
    } else {
        out.dar = 5;   // 设备已销毁
    }
    return out;
};
```

目录持有 provider，provider 捕获 `shared_ptr` 会形成**引用环**，两者都析构不掉。用 `weak_ptr` 并在 `lock()` 失败时返回错误 DAR。

## 请求与响应报文

以下示例固定：SA 六个 `00` 字节、逻辑地址 0、CA=0、PIID=0、控制域请求 `43`／响应 `C3`。

### SET 写校时模式

请求：

```text
68 19 00 43 05 00 00 00 00 00 00 00 AF 30 06 01 00 40 00 03 00 16 01 00 AF 55 16
```

| 字段 | 字节 | 含义 |
| --- | --- | --- |
| `06 01` | | 服务 6（SET-Request），选择子 1 |
| `00` | | PIID=0 |
| `00` | | 单参数（非列表） |
| `40 00 03 00` | | OAD：日期时间，属性 3，索引 0 |
| `16` | | `Enum` 类型标签 |
| `01` | | 枚举值 1（自动校时） |

响应（成功，DAR=0）：

```text
68 19 00 C3 05 00 00 00 00 00 00 00 78 F4 86 01 00 40 00 03 00 00 00 00 CD BD 16
```

| 字段 | 字节 | 含义 |
| --- | --- | --- |
| `86 01` | | SET-Response，选择子 1 |
| `40 00 03 00` | | 回显 OAD |
| `00` | | **DAR = 0，成功** |

响应（DAR=3，设备拒绝）：

```text
68 19 00 C3 05 00 00 00 00 00 00 00 78 F4 86 01 00 40 00 03 00 03 00 00 A9 52 16
```

只有 DAR 字节从 `00` 变成 `03`。**外层解码依然成功** —— 这就是"外层 `Result` 成功不等于业务成功"的典型例子。

响应（DAR=7）：

```text
68 19 00 C3 05 00 00 00 00 00 00 00 78 F4 86 01 00 40 00 03 00 07 00 00 C8 31 16
```

### SET 一次写两个属性

请求：

```text
68 20 00 43 05 00 00 00 00 00 00 00 5B 4A 06 02 00 02 41 00 02 00 11 0F 41 01 02 00 11 01 00 11 B2 16
```

| 字段 | 字节 | 含义 |
| --- | --- | --- |
| `06 02` | | SET-Request，**选择子 2（带参数列表）** |
| `00 02` | | 列表标志 0，属性个数 2 |
| `41 00 02 00` | | 属性 1：`4100/2/0` 最大需量周期 |
| `11 0F` | | `UInt16` 15（分钟） |
| `41 01 02 00` | | 属性 2：`4101/2/0` 需量滑差时间 |
| `11 01` | | `UInt16` 1 |

响应（部分失败：第一项成功，第二项 DAR=3）：

```text
68 1F 00 C3 05 00 00 00 00 00 00 00 0F 01 86 02 00 02 41 00 02 00 00 41 01 02 00 03 00 00 7A 86 16
```

| 字段 | 字节 | 含义 |
| --- | --- | --- |
| `86 02` | | SET-Response，选择子 2 |
| `00 02` | | 列表标志 0，**结果个数 2** |
| `41 00 02 00` | | 回显 OAD 1 |
| `00` | | 第 1 项 **DAR = 0，成功** |
| `41 01 02 00` | | 回显 OAD 2 |
| `03` | | 第 2 项 **DAR = 3，失败** |

**批量写入的响应逐项独立**，一项失败不影响其他项，也不会回滚已成功的写入。

### ACTION 调用方法

请求（方法 `2000/1/0`，参数 `UInt16` 42）：

```text
68 1A 00 43 05 00 00 00 00 00 00 00 1C CE 07 01 00 20 00 01 00 12 00 2A 00 65 D5 16
```

| 字段 | 字节 | 含义 |
| --- | --- | --- |
| `07 01` | | 服务 7（ACTION-Request），选择子 1 |
| `00` | | 单方法 |
| `20 00 01 00` | | OMD：OI `2000`，方法 1，索引 0 |
| `12 00 2A` | | `UInt16` 参数 = 42 |

响应（带返回数据）：

```text
68 1D 00 C3 05 00 00 00 00 00 00 00 2D AA 87 01 00 20 00 01 00 00 01 12 00 2A 00 00 88 25 16
```

| 字段 | 字节 | 含义 |
| --- | --- | --- |
| `87 01` | | ACTION-Response，选择子 1 |
| `20 00 01 00` | | 回显 OMD |
| `00` | | **DAR = 0** |
| `01` | | **有返回数据** |
| `12 00 2A` | | 返回 `UInt16` 42 |

这里 `01` 是"有 Data"的标志。**它不存在**表示方法无返回值 —— 这与"返回了一个 `Null` Data"是**不同的结果**，库用 `std::optional<Data>` 区分：`nullopt` 是没有返回，`Data{Null{}}` 是返回了空值。

响应（DAR=3）：

```text
68 1A 00 C3 05 00 00 00 00 00 00 00 CB 0A 87 01 00 20 00 01 00 03 00 00 00 27 46 16
```

DAR=3 时没有"有返回数据"标志，响应更短。

## 相关配置

| 配置 | 位置 | 说明 |
| --- | --- | --- |
| `AttributeSchema::writable` | 服务端 schema | 默认 `false`，写入须显式授权 |
| `MethodSchema::executable` | 服务端 schema | 方法是否允许执行 |
| `SessionOptions::request_timeout` | 库连接参数 | 写操作通常不需要调大 |
| `SessionOptions::id_reuse_delay` | 库连接参数 | 防止 PIID 复用导致响应错配 |

:::warning 取消写入不保证远端未执行
`disconnect()` 或超时会关闭物理通道，但 **SET/ACTION 可能已经在设备侧执行了**。协议层没有事务回滚机制，需要"要么全做要么全不做"的场景必须自己在应用层设计幂等或补偿。
:::

## 常见失败

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| DAR=1 | 其他错误 | 看设备文档 |
| DAR=2 | 请求数据不能接受 | 检查 Data 类型与属性定义是否匹配 |
| DAR=3 | 请求被拒绝 | 设备权限不足、状态不允许（如运行中改需量周期） |
| DAR=4 | 对象不存在 | 设备没有这个 OI |
| DAR=5 | 属性不存在 | 属性号写错 |
| DAR=6 | 属性不可写 | **最常见**：该属性在设备点表里是只读的 |
| DAR=7 | 属性值超出范围 | 例如需量周期写得太小 |
| DAR=8 | 密码错 / 未授权 | 权限相关 |
| DAR=9 | 密码重复 | 连续写保护 |
| DAR=10 | 无效的逻辑地址 | SA 配置不对 |
| DAR=11 | 无空间 | 存储已满 |
| DAR=255 | 其他 | 查设备文档 |
| 本地 `invalid_value` | schema 未声明可写 | 打开 `writable` |
| 本地 `unsupported_service` | 用普通 GET 读了记录型属性 | 记录属性要用 GET-Record |
| `set_list` 部分成功 | 逐项独立执行 | 自行实现补偿回滚 |
| 方法调用 DAR=6 | 方法不存在或不可执行 | 核对设备方法表 |
| 批量写之后数据不对 | 无事务、部分已生效 | 先读后写 + 手动补偿 |