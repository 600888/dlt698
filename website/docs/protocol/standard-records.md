---
title: 标准记录与能力筛选
description: 日/月冻结、常用事件的行列查询、内存记录模拟及显式点位验证。
---

# 标准记录与能力筛选

第三阶段加入日冻结、月冻结和两类常用事件的只读查询模板，复用 GET Record、GET Next 和标准 provider 绑定。记录对象的 OI 是固定的，时间、序号、关联量使用 `RSD` 与 `RCSD` 定位。

包含 `<dlt698/standard/records.hpp>` 使用查询模板；包含 `<dlt698/service/memory_records.hpp>` 使用模拟后端。模板在 `dlt698::core`，后端在 `dlt698::service`。

## 本批记录定义

| OI 常量 | 编号 | 接口类 | 配置必含的基本列 |
| --- | --- | --- | --- |
| `oi::daily_freeze` | `5004` | 9 冻结 | 冻结序号、冻结时间 |
| `oi::monthly_freeze` | `5006` | 9 冻结 | 冻结序号、冻结时间 |
| `oi::meter_power_down_event` | `3011` | 7 事件 | 事件序号、发生时间、结束时间、来源 |
| `oi::terminal_initialization_event` | `3100` | 7 事件 | 事件序号、发生时间、结束时间、来源 |

来源：2017 标准表 143、149 及附录 E.4、E.6。记录属性为 `02`，索引为 `00`，schema 声明 `record=true`。本批保存配置列的投影，没有把投影定义成事件表的完整普通 GET 值。普通 GET 属性 2 返回 DAR=5，实际记录由 GET Record 读取。

附录 E.3 的 `201E`（发生时间）、`2020`（结束时间）、`2021`（冻结时间）、`2022`（事件序号）、`2023`（冻结序号）加入普通列定义。`2024`（事件来源）提供命名常量，其类型取决于事件实例；这里两类事件的来源均为 NULL，不把所有事件来源统一定义为 NULL。

目录总计 **127 个 OI 定义**：原有 118 个普通点位，加上 4 个记录入口、5 个记录列。公开 OI 常量共 128 个，另一个是上下文相关的事件来源。

## 构造查询

```cpp
#include <dlt698/standard/records.hpp>
using namespace dlt698;
namespace oi = standard::oi;

model::DateTimeS begin{{0x07, 0xea, 10, 1, 0, 0, 0}};
model::DateTimeS end{{0x07, 0xea, 10, 3, 0, 0, 0}};
auto query = standard::record_between(
    oi::daily_freeze, begin, end,
    {{oi::freeze_time, 2, 0}, {oi::forward_active_energy, 2, 1}});
if (!query) return 1;
// query.value() 交给 ClientService::async_get_record 或同步 get_record。
```

`record_at` 生成 RSD 1 的精确时间查询；`record_between` 生成 RSD 2 的时间区间；`record_sequences` 生成 RSD 2 的序号区间。区间按表 30 **前闭后开 `[begin,end)`**，间隔为 NULL，读取区间内全部记录。日期由调用方提供，不附加本机时区；定位时间须为完整有效日期，拒绝通配、无效闰日及倒置区间。

`make_record_query` 接受原始 RSD。模板校验记录 OAD 和平面 OAD 列、重复列及编码资源限制；具体 RSD 语义由 provider 解释。当前标准列校验不支持 ROAD，通用协议 codec 仍保留 ROAD 线格式。

## 内存记录后端

```cpp
auto objects = std::make_shared<service::ObjectRegistry>();
std::vector<model::Oad> columns{
    {oi::freeze_sequence, 2, 0}, {oi::freeze_time, 2, 0},
    {oi::forward_active_energy, 2, 1}};
auto backend = service::MemoryRecords::create(oi::daily_freeze, columns);
if (!backend) return 2;
if (!backend.value()->replace_rows({
        {model::UInt32{1}, begin, model::UInt32{123456}}})) return 3;
if (!service::register_standard_object(
        *objects, oi::daily_freeze, {2}, backend.value())) return 4;
```

每行按配置列顺序保存，必须含模板基本列；序号严格递增，定位时间须完整有效。事件结束时间保留标准允许的未指定字段。关联量采用普通点位的精确类型和配置校验。`replace_rows` 在锁外校验后原子替换，失败保留旧数据；读取在锁内取得不可变快照，筛选在锁外完成。

| RSD | 内存后端行为 |
| --- | --- |
| 0 | 所有行 |
| 1 | 模板时间列或序号列精确匹配 |
| 2 | 同类时间或序号区间，间隔仅支持 NULL |
| 9 | 上第 n 次，1 为最新，超过数量返回空 |
| 其他、非 NULL 间隔、ROAD | 明确拒绝，不猜测采样或表计集合业务 |

空 RCSD 返回全部配置列；非空 RCSD 按请求顺序投影。空命中返回成功空行并保留表头。缺少存储列返回 DAR=4，非法选择为 8，不支持或超过查询预算为 3。`RecordLimits` 默认存储 1024 行、每次最多返回 256 行、32 列、行编码内容总计 1 MiB；达到上限返回错误或 DAR，绝不静默截断。

`register_standard_object` 会转发 `read_record` 并校验表头、列宽、单元类型和编码预算。应用也可用 `MemoryObject::bind_record` 或真实数据库 provider，不必使用本后端。未声明记录属性返回 DAR=5，MemoryObject 未绑定记录回调返回 4，provider 异常由目录隔离为 255。

## GET Next 和快照

单个记录表过大时，分块按完整行拆分，每块重复 OAD/RCSD；客户机验证连续块号并合并相同表头的行，累计行数和实际传输字节仍受限。Session 只查询 provider 一次，后续取块使用首次结果，替换后端不会混入正在传输的数据。

记录列表含重复 OAD 时，线上缺少区分同一 OAD 不同查询的结束分隔符，因此该次快照保留完整记录结果边界，不拆行；Session 自动禁用行合并。这种查询的单个结果须满足协商 APDU 上限。

## 能力提示与读取计划

包含 `<dlt698/standard/capabilities.hpp>`。`capabilities_from_connect` 仅接受成功的 CONNECT 响应；非零结果返回 `association_failed` 并保存原码。会话仍负责协商尺寸、安全机制和能力子集检查。

C.1 是服务位：Normal/List/Record/Next 分别为 1/2/3/6，序号零在首字节最高位。`plan_reads` 在支持 List 时分批发送 List；不支持 List 时拆成单项 Normal；能力未知也采用 Normal；明确均不支持时返回错误。`require_record_service` 对已知不支持 Record 明确拒绝，不转成普通读取记录表。

C.2 是业务提示，不能证明某个 OAD 存在。`SessionOptions.parameters.function` 默认全零，应用可明确配置自身实际业务位；CONNECT 取双方交集，Session 不再无条件清零。全零或只有保留位时按信息未知处理，候选点全部保留。`candidate_points` 默认只附提示，只有显式设置 `discard_negative=true` 才去掉 `no`，始终保留 `unknown`。电能、需量、谐波、状态等已建立业务映射；冻结没有专用功能位，不伪造支持结论。

## 显式逐项验证

```cpp
#include <dlt698/service/point_probe.hpp>
service::async_probe_points(
    session, capabilities, {{oi::frequency, 2, 0}, {oi::voltage, 2, 1}}, {},
    [](Result<std::vector<service::PointResult>> result) {
        if (!result) return; // 配置或规划错误。
        for (const auto& point : result.value()) {
            // outcome 分别为原始 Data、uint8_t DAR、事务 Error。
            // schema_checked / validation_error 保存本地类型校验结果。
        }
    });
```

只有显式调用才产生 GET。默认每批最多 16 项，按协商服务能力顺序发送，一次仅一批在途；保留输入顺序和重复点。超时沿用会话 `request_timeout`，资源预算由 `ProbeOptions.limits` 控制。未知厂家点保留原值，标准类型不匹配也保留 Data 并附校验错误，不能当作测点不存在。事务失败逐项保存 Error；关闭或超时后的批次继续接收会话错误，不重连。

前置错误同步回调；已发起操作的完成回调在 Session 执行器中调用，恰好一次。操作保活 Session 和内部状态，应用须持续驱动执行器。该接口用于普通属性验证，记录存在性仍需 GET Record 逐项确认。

完整示例为 `dlt698_standard_records`：CONNECT → 日冻结序号区间 → 80 字节 APDU 下的 GET Next → 显式普通点探测 → RELEASE。本批未实现周期冻结调度、事件检测、表计集合选择、ROAD 列业务、上报状态或专用事件字段及通用历史数据库。
