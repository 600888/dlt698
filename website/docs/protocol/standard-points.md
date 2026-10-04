---
title: 标准固定点位
description: 14 个常用 OI 的内置目录、OAD 构造、精确类型与倍率换算，以及只读 provider 绑定。
---

# 标准固定点位

`dlt698::standard` 内置 DL/T 698.45—2017 的 14 个常用 OI，提供名称、接口类、属性、精确 Data 标签、数组顺序、单位倍率和标准来源。目录位于 `dlt698::core`，客户端查表无需创建会话；服务端绑定辅助接口位于 `dlt698::service`。

**标准目录表示库知道对象定义。设备是否支持、提供哪些属性，由设备配置、厂家资料及逐项响应确定。** 本地注册仅声明本服务端提供对象，不是主站使用固定点位的前置条件。

## 首批目录

以下类型是属性 2 的完整返回值。索引非零时返回数组的一个元素，不能把完整电压对象注册成 UInt16 标量。

| OI | 名称 | 属性 2 类型 | 倍率 | 单位与顺序 |
| --- | --- | --- | --- | --- |
| 0000 | 组合有功电能 | Array<Int32> | -2 | kWh；总、费率 1～n |
| 0010 | 正向有功电能 | Array<UInt32> | -2 | kWh；总、费率 1～n |
| 0020 | 反向有功电能 | Array<UInt32> | -2 | kWh；总、费率 1～n |
| 0030 | 组合无功 1 电能 | Array<Int32> | -2 | kvarh；总、费率 1～n |
| 0040 | 组合无功 2 电能 | Array<Int32> | -2 | kvarh；总、费率 1～n |
| 2000 | 电压 | Array<UInt16> | -1 | V；A、B、C |
| 2001 | 电流 | Array<Int32> | -3 | A；A、B、C |
| 2004 | 有功功率 | Array<Int32> | -1 | W；总、A、B、C |
| 2005 | 无功功率 | Array<Int32> | -1 | var；总、A、B、C |
| 2006 | 视在功率 | Array<Int32> | -1 | VA；总、A、B、C |
| 200A | 功率因数 | Array<Int16> | -3 | 无量纲；总、A、B、C |
| 200F | 电网频率 | UInt16 | -2 | Hz |
| 4000 | 日期时间 | DateTimeS | — | 保留原始日历字段 |
| 4001 | 通信地址 | OctetString | — | 保留原始字节 |

上述 OI 均收录属性 1（OctetString 逻辑名）。电能类还收录属性 3（ScalerUnit）、属性 4（扩展精度数组，有符号 Int64 或无符号 UInt64，倍率 -4）、属性 5（扩展精度 ScalerUnit）。分相量、功率和频率收录属性 3；2001 还收录属性 4（零线电流 Int32，倍率 -3 A）。4000/4001 本批只收录属性 1、2。

定义依据为第 7 章接口类及附录 E.1/E.3/E.5，单位代码来自附录 B。元数据中的 `version`、`source` 可查询到版本、章节及表号。本批没有内置对象方法、最大需量、谐波、状态字或冻结/事件目录；通用记录服务仍可通过应用 provider 使用。

## 查询与构造 OAD

```cpp
#include <dlt698/standard/catalog.hpp>
using namespace dlt698;

const standard::DeviceLayout layout{standard::Wiring::three_phase, 4};
const auto* energy = standard::find_object(0x0010);
const auto* definition = standard::find_attribute({0x2000, 2, 0});
const auto& directory = standard::objects();

auto all_voltage = standard::make_oad(0x2000, 2, 0, layout);
auto a_voltage = standard::phase_oad(0x2000, standard::Phase::a, layout);
auto total_power = standard::phase_oad(0x2004, standard::Phase::total, layout);
auto total_energy = standard::tariff_oad(0x0010, 0, layout);
auto tariff1 = standard::tariff_oad(0x0010, 1, layout);
auto high_precision = standard::tariff_oad(0x0010, 0, layout, true);
// 使用 .value() 前检查 Result；未知 OI/属性、非法相别及索引不会静默截断。
```

| 需求 | OAD |
| --- | --- |
| 全部三相电压 | `20 00 02 00` |
| A/B/C 相电压 | `20 00 02 01` / `20 00 02 02` / `20 00 02 03` |
| 总有功功率 / A 相有功功率 | `20 04 02 01` / `20 04 02 02` |
| 正向有功总电能 / 费率 1 电能 | `00 10 02 01` / `00 10 02 02` |
| 扩展精度正向有功总电能 | `00 10 04 01` |
| 零线电流 | `20 01 04 00` |
| 日期时间 / 通信地址 | `40 00 02 00` / `40 01 02 00` |

`DeviceLayout` 默认三相、四费率，**请按设备修改**。单相电压/电流数组只有 A（某一相）一项；单相功率数组有总、A 两项。费率数允许 0～254，总量另外占索引 1；这是本批一级 OAD 索引的表示上限。索引 0 始终取整体，不表示“总量”。

`objects()` 返回不可变目录引用，查找结果指向该目录，进程生命周期内有效；未知项返回 nullptr。`find_attribute()` 仅以 OI 和属性低五位定位元数据，不验证特征/索引；需要请求校验时调用 `validate_oad()`。本批辅助构造及严格绑定只支持特征零，通用 `Oad` 仍保留完整线上字节。

## 类型校验与精确工程值

```cpp
model::Data raw = model::Array{{model::UInt16{2413},
                                model::UInt16{2414},
                                model::UInt16{2415}}};
auto valid = standard::validate_value({0x2000, 2, 0}, raw, layout);
auto engineering = standard::engineering_values({0x2000, 2, 0}, raw, layout);
if (engineering) {
    for (const auto& number : engineering.value()) {
        auto text = standard::decimal_text(number);             // "241.3" 等
        auto unit = standard::unit_symbol(number.scaling.unit); // "V"
        auto approximate = standard::approximate_value(number); // 显式 double 近似转换
    }
}
```

校验包括顶层标签、每个数组元素的精确标签、按 DeviceLayout 配置的数组长度及 `Limits` 的节点/深度/字节限制。索引非零时校验元素类型。不对 DateTimeS 附加时区或日历验证，也不推断通信地址字符串含义。

`engineering_values()` 返回拥有原始整数及 ScalerUnit 的 `ScaledNumber` 列表，数组按原顺序展开；工程值公式为原始整数 × 10^scaler。原始 Data 不改变。Int64/UInt64 保留符号和全部位，`decimal_text()` 直接插入十进制小数点，不经过浮点数。例如 UInt64 最大值在倍率 -4 时输出 `1844674407370955.1615`。

`approximate_value()` 是明确选择的浮点便利接口，可能损失整数及小数精度。地址、日期、逻辑名及 ScalerUnit 属性没有数值倍率，工程值转换返回 `unsupported_tag`。目录使用 2017 标准默认倍率，若设备返回的单位属性不同，调用方须处理差异。功率的默认单位是 W/var/VA，不能照搬 645 的 kW/kvar/kVA。

## 服务端只读绑定

```cpp
#include <dlt698/service/standard_object.hpp>

service::ObjectRegistry objects;
auto voltage = std::make_shared<service::MemoryObject>();
voltage->set(2, model::Array{{model::UInt16{2413},
                             model::UInt16{2414},
                             model::UInt16{2415}}});
auto registered = service::register_standard_object(
    objects, 0x2000, {2}, voltage, layout);
if (!registered) { /* 处理定义、配置或重复注册错误 */ }
// 只提供属性 2，未提供的逻辑名和单位属性不会被自动补值。
```

调用方明确选择提供哪些属性，目录通过只读校验适配器持有 provider 的 shared_ptr。适配器原样转发完整 OAD，检查返回 Data；应用仍负责硬件读取及原 `IObjectProvider` 的并发和执行器合约。目录锁外调用 provider，允许重入查询。重复 OI 拒绝，不覆盖已有对象；未提供的属性为 DAR=4。

| 情况 | 结果 |
| --- | --- |
| provider 返回 Data 且校验通过 | 保留精确 Data |
| provider 返回 DAR | 原码返回 |
| 非零属性特征 | DAR=3 |
| 索引越界或给标量加索引 | DAR=8 |
| 错误类型、错误数组长度或返回值超限 | DAR=7 |
| provider 抛异常 | ObjectRegistry 隔离为 DAR=255 |
| 对只读绑定执行 SET | DAR=3 |

元数据的 `writable` 表示标准属性允许的写入能力，与运行 schema 权限分开。4000/4001 的值不会自动变为可写；本批绑定接口始终只读且不注册方法。需要自定义读写业务时，`make_object_schema()` 可生成独立的只读 schema，再由应用明确实现权限及 provider；直接注册原 provider 的路径不附带严格标准适配器。厂家扩展继续使用原来的手写 ObjectSchema 和通用 API。

## 运行示例与能力边界

构建后运行 `dlt698_standard_points`（无需真实设备），通过内存通道完成 CONNECT、批量 GET 和 RELEASE：

```text
电压: 241.3 V
电流: 1.000 A
正向有功电能: 1234.56 kWh
通信地址: 09 06 12 34 56 78 90 12
电网频率: DAR=4
```

最后一项展示“库知道频率定义，但这台模拟设备没有提供频率”。GET 外层成功后仍检查每项的 Data/DAR，再判断标准值结构。客户端可以使用同样的 OAD 辅助接口调用现有 `async_get` / `async_get_list`。

CONNECT 的协议位图描述服务支持，功能位图描述计量/事件等业务能力，不是逐 OI 点表。当前 Session 会清零功能位，目录不会据此删除候选测点；冻结/事件模板及能力筛选留到后续阶段。更多概念见[对象模型与测点寻址](./point-model.md)，provider 合约见[对象目录与 Provider](../session/object.md)。
