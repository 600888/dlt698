---
title: 配置速查
description: 参数名称、用途、默认值、范围、设置代码和适用场景，明确区分库连接参数与电表内部参数。
---

# 配置速查

:::warning 先分清两类参数
这一页列出两类完全不同的参数：

- **库的连接参数** —— 配置在本机程序里，控制通信行为。超时、重试、地址、预算都属于这一类。
- **电表内部参数** —— 配置在电表内部，通过协议读写。最大需量周期、费率数、阶梯数属于这一类。

**电表的最大需量周期不是本库的请求超时。** 前者是表计的计量算法参数，存在电表里；后者是网络往返等待时间，存在你的程序里。两者没有任何联动。
:::

## 库的连接参数

### ClientOptions

```cpp
#include <dlt698/app.hpp>
dlt698::app::ClientOptions options;
```

| 参数 | 类型 | 默认值 | 用途 | 适用场景 |
| --- | --- | --- | --- | --- |
| `protocol` | `SessionOptions` | — | 地址、能力、超时、预算 | 几乎总要配置 SA/CA |
| `transport_timeout` | `milliseconds` | 5000 | DNS 加 TCP 建连的合计时限 | 内网可调小；跨公网放宽 |
| `login_timeout` | `milliseconds` | 5000 | 等待远程服务器 LINK 的阶段时限 | 对端登录慢时放宽 |
| `channel` | `ChannelOptions` | 见下 | 读写缓冲和队列预算 | 高频采集或大报文 |
| `diagnostic` | `function<void(const Error&)>` | 空 | 工作线程上的错误回调 | 排查连接问题 |

`transport_timeout` **不控制串口**。`open_serial` 是同步调用，操作系统打开端口的耗时不受它约束。

### ServerOptions

```cpp
dlt698::app::ServerOptions options;
```

| 参数 | 类型 | 默认值 | 用途 | 适用场景 |
| --- | --- | --- | --- | --- |
| `protocol` | `SessionOptions` | — | 地址、能力、超时 | 同客户端 |
| `max_connections` | `size_t` | 16 | TCP 活动连接上限 | 集中器后挂多主站时放宽 |
| `heartbeat_seconds` | `uint16_t` | 5 | 登录后的自动心跳周期，0 关闭 | 设为 0 只关心跳，**不取消初始登录** |
| `channel` | `ChannelOptions` | 见下 | 通道预算 | 多连接场景 |
| `diagnostic` | `function<void(uint64_t, const Error&)>` | 空 | 连接标识及错误，0 为监听器 | 生产环境建议配 |

### SessionOptions：地址、能力、超时、预算

客户端和服务端共用。地址由两端匹配决定，超时和预算各自独立。

| 参数 | 类型 | 默认值 | 用途 | 适用场景 |
| --- | --- | --- | --- | --- |
| `role` | `Role` | `client` | 协议角色 | 监听端也可以是协议服务器 |
| `server` | `ServerAddress` | 单地址 `{0}`、逻辑 0 | 服务器地址 | **必须按实际配置，含长度** |
| `client_address` | `uint8_t` | 0 | CA 客户端地址 | 两端必须一致 |
| `parameters` | `AssociationParameters` | 见下 | CONNECT 协商内容 | 协商尺寸和能力 |
| `request_timeout` | `milliseconds` | 5000 | 单个事务：CONNECT、请求、RELEASE | 大记录、串口要放宽 |
| `id_reuse_delay` | `milliseconds` | 120000 | 成功事务的序号隔离期 | 须覆盖对端最大响应寿命 |
| `require_login` | `bool` | false | 要求先由协议服务器发 LINK | 远程场景 |
| `preset_association` | `bool` | false | 显式启用本地预设关联 | 预设连接 |
| `clock_trusted` | `bool` | false | 本机时钟可信 | 校时相关 |
| `heartbeat_seconds` | `uint16_t` | 0 | 服务器自动心跳周期 | 已由 `ServerOptions` 覆盖 |
| `request_time_tag` | `optional<Ti>` | 空 | 客户机请求自动加时间标签 | 对端要求时启用 |
| `fragment_timeout` | `milliseconds` | 1000 | 单片确认超时 | 弱链路放宽 |
| `reassembly_timeout` | `milliseconds` | 5000 | 重组无进展超时 | 弱链路放宽 |
| `fragment_retries` | `unsigned` | 2 | 0～16，仅重发未确认片段 | 丢包严重时放宽 |
| `prefer_get_blocks` | `bool` | true | 超长 GET 优先按属性/记录行分块 | 大记录 |
| `calendar_clock` | `function<DateTime()>` | 空 | 注入日历时钟，空用 UTC | 需要本地时间 |
| `limits` | `Limits` | 见下 | 资源预算 | 防异常输入 |

`role` 只决定协议角色，**和 TCP 谁拨号无关**。监听端可以主动发 LINK，拨号方也可以是被动接受的。

### AssociationParameters：CONNECT 协商内容

| 参数 | 默认值 | 用途 |
| --- | --- | --- |
| `protocol_version` | `0x0010` | 协议版本 |
| `conformance` | `{0xF3, 0x8C, 0x08}` | 一致性位 |
| `client_title` | `{}` | 厂商信息 |
| `apdu_bytes` | 1024 | 协商的最大 APDU 长度 |
| `receive_buffer_bytes` | 1024 | 协商的接收缓冲 |
| `window_size` | 1 | 流控窗口 |
| `max_apdu_function` | 1024 | 单 APDU 上限 |
| `timeout_seconds` | 100 | 应用关联空闲超时，写在线上 |

`apdu_bytes` 直接决定记录响应是否会分块。**响应超过这个值时会自动走 GET Next 分块**，需要多次往返，总耗时可能远超 `request_timeout`。串口或低速设备上建议显式设置一个与设备实际能力匹配的值，而不是盲目接受默认。

`parameters.function` 默认全零。**全零会让协商结果没有业务含义**，应用应显式配置自己实际支持的 C.2 功能位：

```cpp
options.protocol.parameters.function[0] = 0x80;
```

### Limits：资源预算

```cpp
options.protocol.limits.max_data_bytes = 65536;
options.protocol.limits.max_frame_bytes = 16385;
options.protocol.limits.max_elements = 65536;
```

| 参数 | 默认值 | 含义 |
| --- | --- | --- |
| `max_data_bytes` | — | 单个 Data 树和 APDU 的字节上限 |
| `max_frame_bytes` | — | 完整链路帧字节上限，帧长须在 12～16385 |
| `max_elements` | 65536 | 单个 Data 树节点总数、GET 属性数、记录行列乘积 |
| `max_depth` | — | Data 树递归深度，从根的 0 开始 |

这些都是**防御外部输入**的上限。调大之前先确认对端可信。

### ChannelOptions：通道预算

```cpp
transport::ChannelOptions channel;
channel.read_chunk_bytes = 4096;          // 1～1 MiB
channel.max_pending_write_bytes = 1024 * 1024;
channel.max_pending_writes = 128;
```

`read_chunk_bytes` 是接收缓冲大小，**和协议帧长度无关**。一帧可能被拆成多次读取，也可能一次读到多帧。

写入预算是背压。发送快于对端响应时队列满，返回 `resource_limit`。

### SerialOptions / SerialLinkOptions

| 参数 | 默认值 | 用途 |
| --- | --- | --- |
| `SerialOptions::baud_rate` | 9600 | 波特率 |
| `SerialOptions::data_bits` | 8 | 数据位 |
| `SerialOptions::parity` | `even` | 校验位 |
| `SerialOptions::stop_bits` | `one` | 停止位，**不接受 1.5** |
| `SerialOptions::flow_control` | `none` | 流控 |
| `SerialLinkOptions::bits_per_character` | 11 | 含起始/数据/校验/停止，必须与实际一致 |
| `SerialLinkOptions::set_transmit` | 空 | RS-485 方向切换钩子 |
| `SerialLinkOptions::async_drain` | 空 | **真实排空**；配了方向切换就必须提供 |

细节和 RS-485 时序要求见[串口连接](./serial.md)。

### DeviceLayout：本地数组布局

```cpp
dlt698::service::DeviceOptions device_options;
device_options.layout = standard::DeviceLayout{
    standard::Wiring::three_phase,  // 或 single_phase
    4,    // 费率数 0～254
    21,   // 最高谐波次数 2～255
};
```

**这三个值必须与设备实际一致**，它们决定数组长度校验和相别可用性。读参数不会自动修改本地布局，配置和设备不一致时读取会失败。

## 电表内部参数

这些参数存在电表里，通过 GET/SET 读写。**它们由设备解释，本库只做编解码和标准定义校验，不执行业务规则。**

| OI | 名称 | 类型 | 倍率/单位 | 标准写入能力 | 何时需要关心 |
| --- | --- | --- | --- | --- | --- |
| 4000 | 日期时间 | `DateTimeS` | 无 | 可写 | 读取表计时钟 |
| 4000 | 校时模式 | `Enum{0,1,2,255}` | 无 | 可写 | 不允许现场改时钟时设为不调整 |
| 4000 | 精准校时参数 | `Structure<5×UInt8>` | 延时阈值单位 s | 可写 | 对时精度要求高 |
| 4001 | 通信地址 | `OctetString` | — | 可写 | 现场改表地址 |
| 4002 | 表号 | `OctetString` | — | 可写 | 表计台账 |
| 4003 | 客户编号 | `OctetString` | — | 可写 | 用户识别 |
| 4006 | 时钟源 | `Structure{Enum{0～4}, Enum{0,1}}` | 无 | 只读 | 判断时钟来源 |
| 400C | 时区时段数 | `Structure<5×UInt8>` | 年时区≤14、日表≤8、日时段≤14、费率≤63、假日≤254 | 可写 | **费率数在这里，读电能前先读它** |
| 400D | 阶梯数 | `UInt8` | 无 | 可写 | 判断有无阶梯 |
| 400E | 谐波分析次数 | `UInt8` | 无 | 可写 | **决定谐波数组长度，须同步到本地布局** |
| 4010 | 计量元件数 | `UInt8{1,2,3}` | 无 | 只读 | 判断接线方式 |
| 4012 | 周休日特征字 | `BitString(8)` | — | 可写 | 费率时段业务 |
| 4030 | 电压合格率参数 | `Structure<4×UInt16>` | -1 V | 可写 | 电压合格范围统计 |
| 4100 | 最大需量周期 | `UInt8` | 倍率 0，min | 可写 | **需量积算周期，1～n 分钟** |
| 4101 | 滑差时间 | `UInt8` | 倍率 0，min | 可写 | 需量更新步长 |
| 4103 | 资产管理编码 | `VisibleString(32)` | — | 可写 | 资产台账 |
| 4104 | 额定电压 | `VisibleString(6)` | — | 只读 | 铭牌参数 |
| 4105 | 额定电流/基本电流 | `VisibleString(6)` | — | 只读 | 铭牌参数 |
| 4106 | 最大电流 | `VisibleString(6)` | — | 只读 | 铭牌参数 |
| 4107 | 有功准确度等级 | `VisibleString(4)` | — | 只读 | 铭牌参数 |
| 4108 | 无功准确度等级 | `VisibleString(4)` | — | 只读 | 铭牌参数 |
| 410B | 电能表型号 | `VisibleString(32)` | — | 只读 | 台账 |

各参数的读取方法和字段结构见[电表参数读取与设置](./meter-parameters.md)。

### 两个最该先读的参数

现场对接时，**先读这两个参数再决定本地布局**，否则数组长度校验一定失败：

```cpp
// 1. 费率数 → 决定电能数组长度
auto counts = client.get({0x400C, 2, 0});
// Structure<5×UInt8>，第 4 个字段是费率数
const auto& fields = std::get<model::Data>(counts.value()).as<model::Structure>().value;
const std::size_t tariff_count = fields[3].as<model::UInt8>().value;

// 2. 谐波分析次数 → 决定谐波数组长度
auto harmonic = client.get({0x400E, 2, 0});
const std::size_t harmonic_order = std::get<model::Data>(harmonic.value()).as<model::UInt8>().value;

// 然后用实际值构造布局
const standard::DeviceLayout layout{standard::Wiring::three_phase, tariff_count, harmonic_order};
```

**读取参数不会自动修改本地布局。** 必须由你读取、判断并显式配置 `DeviceLayout`，这一点没有捷径。

### 写入权限的两层含义

**标准元数据标注可写**，只表示 DL/T 698.45 允许写这个属性。**运行时是否真能写，取决于设备的权限和配置。** 很多表计出厂时锁定参数，写入返回 DAR=3。

托管服务器本地发布的属性**默认只读**，远端 SET 返回 DAR=3。需要可写属性和方法，仍要通过[高级分层接口](../session/object.md)接入。

## 按场景速查

| 我想… | 调什么 |
| --- | --- |
| 内网快速连接 | `transport_timeout` 调到 2000 |
| 跨公网或链路慢 | `transport_timeout` 10000，`request_timeout` 15000 |
| 串口/低速设备读记录 | `request_timeout` 15000 以上 |
| 记录响应被分块导致超时 | `parameters.apdu_bytes` 与设备能力对齐，放宽 `request_timeout` |
| 丢包严重 | `fragment_timeout` 3000，`fragment_retries` 4 |
| 对端要求时间标签 | `request_time_tag` 配置 |
| 大量并发读 | `max_connections`、`max_pending_writes` 放宽 |
| 处理不可信输入 | 收紧 `max_data_bytes` 和 `max_elements` |
| 表计费率数不是 4 | 读 `400C/2/0`，按实际值设 `DeviceLayout.tariff_count` |
| 表计不是 21 次谐波 | 读 `400E/2/0`，按实际值设 `DeviceLayout.harmonic_order` |
| RS-485 时序不稳 | 提供真实 `async_drain`，或改用自动方向适配器 |
| 单相表 | `Wiring::single_phase`，只查 total 和 A |

## 下一步

- 逐参数读取方法：[电表参数读取与设置](./meter-parameters.md)
- 连接与关联：[TCP 连接](./tcp.md)、[串口连接](./serial.md)
- 完整结构体定义：[托管客户端](../session/client.md)、[Session 会话](../session/session.md)