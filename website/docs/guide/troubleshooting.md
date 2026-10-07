---
title: 排查手册
description: 按现象定位 DL/T 698 常见问题；连接失败、DAR、超时、读不到数据与数据不对的成因和修法。
---

# 排查手册

按**现象**组织。先看症状，再跳到对应小节。

## 连接问题

### 连不上

```text
connect_tcp 返回 io_error / timeout
```

按顺序检查：

| 检查项 | 怎么确认 |
| --- | --- |
| IP 和端口 | 服务端是否真的在监听？`Server::local_port()` 返回实际端口 |
| 防火墙 | 目标端口是否放行 |
| 关联模式配对 | 服务端和客户端是否都用了 `local_public`？ |
| SA 地址 | 服务端的 `server.bytes` 是否与客户端配置一致 |
| 连接数上限 | TCP 超过 `max_connections`（默认 16）会拒绝 |
| 会话角色 | `SessionOptions::role` 与实际服务端行为是否匹配 |

### 关联被拒绝

```text
connect 返回 association_failed
```

**这是业务失败，不是传输失败** —— TCP 连上了，但 CONNECT 被设备拒绝。

| ConnectResponse::result | 含义 |
| --- | --- |
| 1 | 密码错或未授权 |
| 2 | 逻辑地址不匹配 |
| 3 | 通道忙 |
| 其他 | 见设备文档 |

```cpp
// 必须检查外层 Result 之后，再检查业务字段
auto connected = client.connect_tcp("127.0.0.1", 6980);
if (!connected) {
    std::cerr << "transport: " << connected.error().context << '\n';   // 连不上
    return 1;
}
```

### `busy`

```text
第二个并发请求返回 busy
```

库采用**单在途事务**策略：第二个请求直接返回 `busy`，**不入队**。

```cpp
// 错误：以为会排队
auto a = client.get(oad1);
auto b = client.get(oad2);   // busy

// 正确：批量读
auto both = client.get_list({oad1, oad2});
```

如果确实需要并发读不同设备，为每个设备各建一个 `Client`。

### 超时

```text
request_timeout（默认 5 秒）触发
```

:::warning 超时会关闭物理通道
线上没有 generation 字段，无法区分迟到响应。库的取舍是：**超时就断开**，不做"再等一会儿看看有没有迟到响应"的尝试。

这意味着超时后必须重新连接，且超时前发出的写操作**可能已经在设备侧执行**。
:::

常见原因：

| 原因 | 处理 |
| --- | --- |
| 记录查询数据量大 | 调大 `request_timeout` |
| 设备响应慢（首次唤醒、通信模块休眠） | 调大超时，或先发一个轻量请求预热 |
| 网络丢包 | 查链路质量 |
| 批量太大，分块收齐超时 | 减少单次属性数 |

## 读取问题

### 返回 `Result` 成功但没数据

这是最容易误判的一点。**外层 `Result` 成功只表示"请求-响应往返完成"**，业务结果在字段里。

| 操作 | 业务结果在哪 |
| --- | --- |
| `get` | `ObjectValue` = `variant<uint8_t, Data>`，`uint8_t` 分支是 DAR |
| `get_list` | 每个 `AttributeResult::result` |
| `get_record` | `RecordResult::result` |
| `set` | 返回值本身就是 `uint8_t` DAR |
| `action` | `ActionValue::dar` |

```cpp
// get：正确
auto value = client.get(oad);
if (!value) {
    std::cerr << "transport: " << value.error().context << '\n';
} else if (const auto dar = std::get_if<std::uint8_t>(&value.value())) {
    std::cerr << "device DAR=" << unsigned(*dar) << '\n';   // 业务失败
} else {
    const auto& data = std::get<model::Data>(value.value());  // 成功
}
```

### DAR 速查

| DAR | 含义 | 常见原因 |
| --- | --- | --- |
| 1 | 其他错误 | 查设备文档 |
| 2 | 请求数据不能接受 | Data 类型与属性定义不匹配 |
| 3 | 请求被拒绝 | 权限不足、状态不允许、选择器不支持 |
| 4 | 对象不存在 | **设备没有这个 OI** —— 最常见 |
| 5 | 属性不存在 | 属性号写错 |
| 6 | 属性不可写 | 该属性在设备点表里是只读 |
| 7 | 属性值超出范围 | 值超出设备允许区间 |
| 8 | 密码错 / 未授权 | 权限相关 |
| 9 | 密码重复 | 连续写保护 |
| 10 | 无效的逻辑地址 | SA 配置不对 |
| 11 | 无空间 | 存储已满 |
| 255 | 其他 | 查设备文档 |

:::tip DAR=4 多半是"设备不支持"
库里定义了某个 OI，**不代表你的设备实现了它**。DL/T 698.45 定义了大量对象，实际电表只实现其中一部分。

库里内置的模拟设备（示例程序）覆盖的对象也比真实电表少。写代码前先确认目标设备的点表。
:::

### 读到了属性但类型不对

```cpp
data.type() == model::DataType::int32   // 期望 uint16
```

**按精确类型访问**：`Data::as<T>()` 在类型不符时抛 `std::bad_variant_access`。

```cpp
// 先判类型再取值
if (data.type() == model::DataType::uint16) {
    std::cout << data.as<model::UInt16>().value << '\n';
} else {
    std::cerr << "expected uint16, got " << unsigned(data.type()) << '\n';
}
```

常见原因：

- 索引 0（整体数组）和索引 1（单元素）类型不同
- 索引 2、3 是相别，1 是总量 —— 别把 B 相当成总量
- 谐波相别在**属性号**（2/3/4），不在索引
- 记录列的 OAD 与普通点位 OAD 相同，但设备可能返回聚合类型

### `engineering_values` 报错

| 报错 | 原因 | 处理 |
| --- | --- | --- |
| `non-numeric standard attribute` | 该属性无数值倍率 | 直接用原始值，不要换算 |
| `unsupported_tag` | 该属性是位串（如状态字） | 读原始 `BitString`，库不解释位含义 |

无倍率属性的典型例子：`200D/5`（最高谐波次数）、`4010`（计量元件数）。这些是**计数值不是测量值**，用原始整数即可。

### `resource_limit`

```text
resource_limit: ...
```

四种可能：

| 场景 | 调什么 |
| --- | --- |
| 记录行数过多 | `Limits::max_elements` |
| 属性数过多 | `Limits::max_elements` |
| 帧太长 | `Limits::max_frame_bytes`（默认 16385） |
| Data 树太深/太大 | `Limits::max_depth`、`Limits::max_data_bytes` |

记录查询**给了行数据却给空表头**时也返回 `resource_limit` —— 这是防止无法解释列含义的数据被误读，属正常保护。

## 数据不对

### 数值差一个数量级

十有八九是**倍率换算**搞反了。原始值是整数，工程值需要乘/除以倍率：

| 数据 | 原始类型 | 倍率 | 例 |
| --- | --- | --- | --- |
| 电压 | `UInt16` | -1 | 2413 → 241.3 V |
| 电流 | `Int32` | -3 | 5000 → 5.000 A |
| 有功功率 | `Int32` | -1 | 60000 → 6000 W |
| 电能 | `UInt32` | -2 | 123456 → 1234.56 kWh |
| 频率 | `UInt16` | -2 | 5000 → 50.00 Hz |

用 `engineering_values` + `decimal_text` 换算，不要自己乘除：

```cpp
auto numbers = standard::engineering_values(oad, data, layout);
if (numbers && !numbers.value().empty())
    std::cout << standard::decimal_text(numbers.value().front()) << '\n';
```

各数据类别页有完整的倍率表。

### 符号错了

有符号量用二进制补码。协议原码是负数时（C++ 的 `uint8_t` 看不到负号）：

| 数据 | 类型 | 负数示例 |
| --- | --- | --- |
| 电流 | `Int32` | `05 FF FF FF F4` → -12 |
| 功率 | `Int32` | 高位为 1 → 负 |
| 功率因数 | `Int16` | `FC 90` → -880 → -0.880 |
| 温度 | `Int16` | 低温为负 |

**用 `Int32`/`Int16` 类型的 `Value` 取值，不要按 `UInt*` 解读。**

### 数组长度不对

| 数据 | 长度取决于 |
| --- | --- |
| 电压、电流、功率 | `DeviceLayout::wiring`（三相 3，单相 1） |
| 电能 | `DeviceLayout::tariff_count` + 1（索引 0 是总量） |
| 谐波 | `DeviceLayout::harmonic_order` |
| 时间区段表 | 电表参数 `400C` 的时区时段数 |

**两个决定长度的参数要从设备读**：

| 参数 | OAD | 决定 |
| --- | --- | --- |
| 时区时段数 | `400C` 第 4 字段 | 电能数组长度（费率数） |
| 最高谐波次数 | `200D/5/0` | 谐波数组长度 |

读不到这些参数时只能猜，猜错就会数组越界或长度不符。

### 需量结构取错字段

需量是 `Array<Structure>`，每个结构两个成员：**数值** + **`DateTimeS` 发生时间**。

```cpp
auto readings = standard::demand_values(oad, data, layout);
if (readings && !readings.value().empty()) {
    const auto& reading = readings.value().front();
    std::cout << standard::decimal_text(reading.number) << '\n';
    // DateTimeS 是 7 字节数组，不是具名字段
    const auto& t = reading.occurred_at.value;
    std::cout << (unsigned(t[0]) << 8 | t[1]) << '-' << unsigned(t[2]) << '-' << unsigned(t[3]) << '\n';
}
```

:::danger `DateTimeS` 没有 `year`/`month`/`day` 字段
它是 `Value<date_time_s, std::array<std::uint8_t, 7>>`：

| 下标 | 含义 |
| --- | --- |
| `t[0]`, `t[1]` | 年，2 字节大端 |
| `t[2]` `t[3]` `t[4]` `t[5]` `t[6]` | 月 日 时 分 秒 毫秒 |

写成 `t.year` 编译不过。
:::

库里**不做**时区换算、不转 `time_t`、不校验日期合法性（记录查询的时间校验除外）。

### 时间对不上

| 检查项 | 说明 |
| --- | --- |
| 时区 | 设备时间可能是 UTC 或本地时间，库不换算 |
| `FF` / `FFFF` | `DateTimeS` 里全 `FF` 表示"未指定"字段 |
| 设备时钟错 | 先读 `4000/2/0` 核对，见[日期时间与校时](./clock.md) |
| 毫秒字段 | 冻结和事件的时间精确到毫秒，肉眼对比看不出来 |

## 串口问题

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| 完全收不到数据 | 波特率/校验位/停止位不匹配 | 三方都要一致 |
| 偶发丢帧 | 帧间隔不足 | 至少 33 位时间；检查线缆 |
| 首字节丢 | 设备需要唤醒时间 | 先发前导字节 |
| 长帧被截断 | 缓冲太小 | 调大通道预算 |
| RS-485 收到自己发的数据 | 方向切换时机错 | **必须用真实 `async_drain`**，时间估算不够 |

详见[串口连接](./serial.md)。

## 记录查询问题

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| DAR=5 | 用了普通 GET | 记录型属性必须 `get_record` |
| `unsupported_service` on GET | 同上 | 同上 |
| 返回 0 行 | 设备没有那天的记录 | 正常情况，不是错误 |
| 查不到应有数据 | 冻结时刻未到、设备时钟错 | 先读当前时钟 |
| 序号跳变 | 换表、格式化、存储滚动 | 以响应实际序号推进 |
| 序号区间无结果 | 超出设备存储范围 | 先读最大序号 |
| `resource_limit` | 响应空表头 | 设备实现问题，库的保护 |
| `invalid_value` on 时间 | 时间非法 | 记录查询要求完整合法公历时间 |

## 排查流程

遇到问题时按这个顺序走：

1. **抓包**，用 [`dlt698_decode`](./packet-debug.md) 确认链路层通不通
2. 链路层通了 → 看 APDU 的服务号和选择子对不对
3. 请求正确 → 看响应里的**逐项 DAR**
4. DAR 是 0 但值不对 → 查**类型**和**倍率**
5. 类型对值还不对 → 查**数组长度**和**索引语义**

大部分问题在前三步就能定位。

## 该报告什么

向库或设备方反馈问题时，提供这些信息能大幅加快定位：

| 信息 | 怎么拿 |
| --- | --- |
| 完整报文 | `dlt698_decode` 的输入输出 |
| 精确 OAD | 打印 `model::Oad` 的三个字段 |
| 期望值与实际值 | 包括类型和倍率 |
| `Result` 与逐项 DAR | 两者都要 |
| 设备型号和点表版本 | 设备方提供 |
| 连接参数 | 关联模式、SA、波特率 |
| `Limits` 配置 | 非默认值要说明 |

## 常见失败速查

| 错误 | 含义 |
| --- | --- |
| `io_error` | 传输层失败 |
| `timeout` | 超时，**通道已关闭** |
| `cancelled` | 被取消，通道已关闭 |
| `busy` | 已有在途事务 |
| `not_associated` | 未完成协议关联 |
| `association_failed` | CONNECT 被拒绝 |
| `address_mismatch` | 响应地址与请求不符 |
| `direction_mismatch` | 协议角色与传输方向不符 |
| `checksum_header` / `checksum_frame` | 校验和错误 |
| `trailing_data` | 帧后有多余数据 |
| `need_more_data` | 数据不完整 |
| `resource_limit` | 超出资源上限 |
| `unsupported_service` | 服务或变体未实现 |
| `unsupported_tag` | 数据类型不支持该操作 |
| `invalid_value` | 值非法 |
| `bad_variant_access` | `as<T>()` 类型不符（异常，非 `Result`） |