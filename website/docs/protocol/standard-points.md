---
title: 标准固定点位
description: 118 个常用 OI，覆盖电能、最大需量、状态字、谐波、变量和参数，以及只读 provider 绑定。
---

# 标准固定点位

`dlt698::standard` 内置 DL/T 698.45—2017 的 118 个常用 OI：41 个电能、40 个最大需量、17 个变量、20 个参数，提供名称、接口类、属性、精确 Data 标签、数组/结构顺序、单位倍率和标准来源。目录位于 `dlt698::dlt698`，客户端查表无需创建会话；服务端绑定辅助接口位于 `dlt698::dlt698`。

**标准目录表示库知道对象定义。设备是否支持、提供哪些属性，由设备配置、厂家资料及逐项响应确定。** 本地注册仅声明本服务端提供对象，不是主站使用固定点位的前置条件。

## 基础 14 个对象

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

上述 OI 均收录属性 1（OctetString 逻辑名）。电能类还收录属性 3（ScalerUnit）、属性 4（扩展精度数组，有符号 Int64 或无符号 UInt64，倍率 -4）、属性 5（扩展精度 ScalerUnit）。分相量、功率和频率收录属性 3；2001 还收录属性 4（零线电流 Int32，倍率 -3 A）。4000 另收录属性 3（校时模式 Enum）、属性 4（五个 UInt8 字段的精准校时参数），4001 收录属性 1、2。

定义依据为第 7 章接口类及附录 E.1/E.2/E.3/E.5，单位代码来自附录 B，状态位格式来自附录 F。元数据中的 `version`、`source` 可查询到版本、章节及表号。下表列出 118 个普通点位；第三阶段另外加入 4 个记录入口及 5 个记录列，完整目录共 127 个 OI，详见[标准记录与能力筛选](./standard-records.md)。

## 扩充电能和最大需量

下表每行均收录总、A、B、C 四个对象；逗号分隔的 OI 按此顺序排列。组合有功 `0000` 单独保留，不生成标准未定义的 `0001～0003`。合计电能 41 个、最大需量 40 个。

| 家族 | 电能 OI（总、A、B、C） | 最大需量 OI（总、A、B、C） | 数值符号 | 电能单位 / 需量单位 |
| --- | --- | --- | --- | --- |
| 正向有功 | 0010, 0011, 0012, 0013 | 1010, 1011, 1012, 1013 | 无符号 | kWh / kW |
| 反向有功 | 0020, 0021, 0022, 0023 | 1020, 1021, 1022, 1023 | 无符号 | kWh / kW |
| 组合无功 1 | 0030, 0031, 0032, 0033 | 1030, 1031, 1032, 1033 | 有符号 | kvarh / kvar |
| 组合无功 2 | 0040, 0041, 0042, 0043 | 1040, 1041, 1042, 1043 | 有符号 | kvarh / kvar |
| 第一象限 | 0050, 0051, 0052, 0053 | 1050, 1051, 1052, 1053 | 无符号 | kvarh / kvar |
| 第二象限 | 0060, 0061, 0062, 0063 | 1060, 1061, 1062, 1063 | 无符号 | kvarh / kvar |
| 第三象限 | 0070, 0071, 0072, 0073 | 1070, 1071, 1072, 1073 | 无符号 | kvarh / kvar |
| 第四象限 | 0080, 0081, 0082, 0083 | 1080, 1081, 1082, 1083 | 无符号 | kvarh / kvar |
| 正向视在 | 0090, 0091, 0092, 0093 | 1090, 1091, 1092, 1093 | 无符号 | kVAh / kVA |
| 反向视在 | 00A0, 00A1, 00A2, 00A3 | 10A0, 10A1, 10A2, 10A3 | 无符号 | kVAh / kVA |

电能沿用属性 1～5；属性 2 的整数倍率 -2，属性 4 扩展精度倍率 -4。最大需量接口类为 2，收录属性 1（逻辑名）、2（总及费率数组）、3（ScalerUnit）。需量数组每个元素为 `Structure{Int32/UInt32, DateTimeS}`，数值倍率 -4，发生时间无倍率。**需量使用 kW/kvar/kVA，实时功率使用 W/var/VA。**

```cpp
#include <dlt698/standard/catalog.hpp>
using namespace dlt698;
namespace oi = dlt698::standard::oi;
const standard::DeviceLayout extended{standard::Wiring::three_phase, 4, 21};
auto b_energy = standard::energy_oad(oi::forward_active_energy,
                                    standard::Phase::b, 1, extended); // 00 12 02 02
auto a_demand = standard::demand_oad(oi::combination_reactive_1_maximum_demand,
                                    standard::Phase::a, 0, extended); // 10 31 02 01
model::Data raw_demand = model::Structure{{
    model::Int32{-123456}, model::DateTimeS{{0x07, 0xea, 10, 5, 12, 30, 0}}}};
if (a_demand) {
    auto readings = standard::demand_values(a_demand.value(), raw_demand, extended);
    if (readings) {
        auto value = standard::decimal_text(readings.value()[0].number); // "-12.3456"
        auto occurred = readings.value()[0].occurred_at; // 原始发生时间，无附加时区
    }
}
```

`tariff_oad()` 可直接对具体的分相 OI 选总量/费率；最大需量不支持 `high_precision=true`。`phase_oad()` 仍用于电压、电流、功率数组的相别索引；分相电能/需量使用上述家族接口。单相配置只受理总、A 相电能/需量，B/C 对象查询返回配置错误。

## 状态字、谐波和其他变量

| OI | 名称 | 属性 2 类型 | 倍率与单位 / 布局 |
| --- | --- | --- | --- |
| 200B / 200C | 电压 / 电流波形失真度 | Array<Int16> | -2 %；A、B、C，单相为一项 |
| 200D / 200E | 电压 / 电流谐波含有量 | Array<Int16> | -2 %；属性 2/3/4 分别是 A/B/C 相 |
| 2010 | 表内温度 | Int16 | -1 ℃ |
| 2014 | 电能表运行状态字 | Array<BitString(16)> | 固定 7 项，对应附录 F.1～F.7 |
| 2015 | 跟随上报状态字 | BitString(32) | 无倍率；属性 4 为 32 位跟随上报模式字 |
| 2017 / 2018 / 2019 | 当前有功 / 无功 / 视在需量 | Int32 | -4 kW / kvar / kVA |

普通分相/数据变量收录属性 1～3。谐波接口类 5 收录属性 1～6：属性 5 是最高次数 UInt8，属性 6 是 ScalerUnit。谐波数组包含总含有量及 2～n 次，**长度为 n，索引 1 是总含有量，索引 2 是 2 次，没有 1 次基波项**。`DeviceLayout.harmonic_order` 默认 21，允许 2～255，须按设备属性 5 配置；读取次数参数不会自动修改本地配置。单相配置不受理 B/C 相谐波属性。

```cpp
auto b_second = standard::harmonic_oad(oi::voltage_harmonics,
                                      standard::Phase::b, 2, extended); // 20 0D 03 02
auto a_total = standard::harmonic_oad(oi::current_harmonics,
                                     standard::Phase::a, 0, extended); // 20 0E 02 01
auto status1 = standard::make_oad(oi::operating_status, 2, 1); // 20 14 02 01
```

状态字保持 BitString 标签及原始位串，校验位宽和数组长度，不转换为工程数值、不清除保留位，也不自动解释故障业务。跟随上报确认方法、谐波分析算法、需量积算和复位操作须由应用实现。

## 常用参数

参数接口类为 8，均收录属性 1、2；日期时间另有属性 3、4。下表覆盖全部 20 个参数对象。标注“可写”的仅指标准元数据，绑定辅助仍生成只读 schema。

| OI | 名称 | 值定义 | 标准值写入能力 |
| --- | --- | --- | --- |
| 4000 | 日期时间 | DateTimeS；校时模式 Enum{0,1,2,255}；精准校时参数 Structure<5×UInt8> | 可写 |
| 4001 / 4002 / 4003 | 通信地址 / 表号 / 客户编号 | OctetString | 可写 |
| 4006 | 时钟源 | Structure{Enum{0～4}, Enum{0,1}} | 只读 |
| 400C | 时区时段数 | Structure<5×UInt8>；依次为年时区、日时段表、日时段、费率、假日数量，上限 14/8/14/63/254 | 可写 |
| 400D / 400E | 阶梯数 / 谐波分析次数 | UInt8 | 可写 |
| 4010 | 计量元件数 | UInt8{1,2,3} | 只读 |
| 4012 | 周休日特征字 | BitString(8) | 可写 |
| 4030 | 电压合格率参数 | Structure<4×UInt16>；考核上/下限、合格上/下限，-1 V | 可写 |
| 4100 / 4101 | 最大需量周期 / 滑差时间 | UInt8，倍率 0，min | 可写 |
| 4103 | 资产管理编码 | VisibleString(32 字节) | 可写 |
| 4104 / 4105 / 4106 | 额定电压 / 额定电流与基本电流 / 最大电流 | VisibleString(6 字节) | 只读 |
| 4107 / 4108 | 有功 / 无功准确度等级 | VisibleString(4 字节) | 只读 |
| 410B | 电能表型号 | VisibleString(32 字节) | 只读 |

字符串的 SIZE 严格按协议字节数检查，不把额定参数字符串解析为数值。结构支持一级 OAD 索引，例如 `40 30 02 03` 读取电压合格上限；数组索引仍选择整个数组元素，不能借一个索引同时选择需量结构内的字段。目录的 `value_definition` / `element_definition` 可查询结构字段、位宽/长度、字段倍率及标准取值约束。业务关系（如电压上下限关系、滑差与需量周期关系）由应用校验，读取参数不会自动调整设备布局。

## 查询与构造 OAD

OI 命名常量定义在公开头文件 `<dlt698/standard/oi.hpp>`，命名空间为 `dlt698::standard::oi`，类型均为 `inline constexpr std::uint16_t`，可用于编译期常量表达式。当前共 128 个常量，包含 118 个原有普通点位、4 个记录入口、5 个记录列及上下文相关的事件来源。只需编号时可单独包含该轻量头文件；`catalog.hpp` 和聚合头 `dlt698.hpp` 也会包含它。完整的名称、属性、类型、倍率及来源表保留在 CPP 中，通过目录 API 查询。

```cpp
#include <dlt698/standard/catalog.hpp>
using namespace dlt698;

const standard::DeviceLayout layout{standard::Wiring::three_phase, 4};
const auto* energy = standard::find_object(standard::oi::forward_active_energy);
const auto* definition = standard::find_attribute({standard::oi::voltage, 2, 0});
const auto& directory = standard::objects();

auto all_voltage = standard::make_oad(standard::oi::voltage, 2, 0, layout);
auto a_voltage = standard::phase_oad(standard::oi::voltage, standard::Phase::a, layout);
auto total_power = standard::phase_oad(standard::oi::active_power, standard::Phase::total, layout);
auto total_energy = standard::tariff_oad(standard::oi::forward_active_energy, 0, layout);
auto tariff1 = standard::tariff_oad(standard::oi::forward_active_energy, 1, layout);
auto high_precision = standard::tariff_oad(standard::oi::forward_active_energy, 0, layout, true);
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
auto valid = standard::validate_value({standard::oi::voltage, 2, 0}, raw, layout);
auto engineering = standard::engineering_values({standard::oi::voltage, 2, 0}, raw, layout);
if (engineering) {
    for (const auto& number : engineering.value()) {
        auto text = standard::decimal_text(number);             // "241.3" 等
        auto unit = standard::unit_symbol(number.scaling.unit); // "V"
        auto approximate = standard::approximate_value(number); // 显式 double 近似转换
    }
}
```

校验包括顶层标签、每个数组元素及结构字段的精确标签和顺序、按 DeviceLayout 配置的数组长度、固定位宽/字符串长度、已定义的枚举/数量上限及 `Limits` 的节点/深度/字节限制。索引非零时校验选中元素或结构字段。不对 DateTimeS 附加时区或日历业务验证，也不推断通信地址字符串含义。

`engineering_values()` 返回拥有原始整数及 ScalerUnit 的 `ScaledNumber` 列表，数组和带倍率的结构字段按原顺序展开；工程值公式为原始整数 × 10^scaler。结构中无倍率的字段不会进入数值列表，需量时间请从 `demand_values()` 或原始 Data 获取。原始 Data 不改变。Int64/UInt64 保留符号和全部位，`decimal_text()` 直接插入十进制小数点，不经过浮点数。例如 UInt64 最大值在倍率 -4 时输出 `1844674407370955.1615`。

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
    objects, standard::oi::voltage, {2}, voltage, layout);
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
| 错误类型、结构字段、数组/位串/字符串长度、标准取值或返回值超限 | DAR=7 |
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

扩充示例 `dlt698_standard_points_extended` 使用三相、一费率、最高 3 次谐波配置，通过 CONNECT、批量 GET、RELEASE 读取分相电能、带时间的需量、B 相 2 次谐波、状态字、表号及周期参数：

```text
A相正向有功电能: 1234.56 kWh
A相组合无功1最大需量: -12.3456 kvar @ 2026-10-05 12:30:00
电压谐波含有量: 1.25 %
电能表运行状态字: 04 10 80 04
表号: 09 02 12 34
最大需量周期: 15 min
```

本页覆盖上述 118 个普通对象及列明属性的元数据、读取结构校验和只读模拟绑定；不包含真实计量算法、写入/校时/复位方法、时段表业务、基波/谐波电能、冻结周期内最大需量。日/月冻结及常用事件模板见[标准记录与能力筛选](./standard-records.md)。新增结构字段会改变公开元数据结构的 C++ 布局，升级后库及调用方须一起重新编译；原有函数和两字段 `DeviceLayout` 聚合初始化仍可使用，最高谐波次数采用默认 21。

CONNECT 的协议位图描述服务支持，功能位图描述计量/事件等业务能力，不是逐 OI 点表。Session 保留应用显式配置的功能位，协商取交集；全零信息按未知处理，默认不删除候选测点。可选提示、读取计划和显式逐项验证见[标准记录与能力筛选](./standard-records.md)。更多概念见[对象模型与测点寻址](./point-model.md)，provider 合约见[对象目录与 Provider](../session/object.md)。
