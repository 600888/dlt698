---
title: 对象模型与测点寻址
description: OI、属性标识和元素索引如何共同定位一个测点，以及记录型对象的二维间接寻址。
---

# 对象模型与测点寻址

本页解释一个概念问题：**698 协议里，一个"测点"是怎么被确定的。**

这是接入 698 之前必须先想清楚的事。熟悉 DL/T 645 的读者会习惯于"固定地址 + 固定 DI = 固定测点"的模型，直接套到 698 上会处处碰壁——地址域和 DI 换了名字，但寻址的**结构**完全变了。

## 为什么不能照搬 645

DL/T 645 是**面向过程**的。2007 版地址域为 6 字节 BCD，数据标识 DI 为 4 字节。标准定义 DI 的含义，但设备实际支持哪些数据项仍取决于型号和配置。

DL/T 698 是**面向对象**的。一个测点由 4 字节的 OAD 定位，这三个字段的含义分层：

```text
逻辑地址（1~17 字节，链路层地址域）
  └─ OI      对象标识，2 字节，标准分配
       └─ 属性标识 PI    1 字节，含特征位
            └─ 元素索引 index   1 字节
                 └─ OAD → 一个测点
```

和 645 的对应关系：

| DL/T 645 | DL/T 698 |
| --- | --- |
| 从站地址（6 字节 BCD） | 链路层服务地址（长度可变） |
| 数据标识 DI（4 字节） | 对象标识 OI（2 字节） |
| — | 属性标识 PI（1 字节） |
| — | 元素索引 index（1 字节） |
| DI 可表示标量或数据块 | OAD 表示完整属性或一级元素 |

关键差异：**645 的 DI 本身就是测点；698 的 OI 是测点的容器。**

## 三层展开

### 第一层：OI 由标准分配

OI（2 字节）的语义由标准统一分配，主站查标准附录的对象标识符表即可。这一层和 645 的 DI 完全等价，**不需要问设备"你有什么点"**。

例如 `40 01 02 00`：OI = `4001` 是参数变量类的通信地址对象，属性 `02` 是参数，索引 `00` 取整体内容。

### 第二层：属性由接口类定义

同一个 OI 下有哪些属性，是标准按**接口类**（IC）写死的。常见约定是属性 1 为逻辑名、属性 2 为数值，属性 3 及以后视接口类而定。

属性标识占 1 字节，其中**包含特征位**。本库 `Oad::attribute` 保留完整线上字节，不剥离特征位——这也是 `read`/`write` 文档里反复强调"传入完整 OAD"的原因。

### 第三层：索引展开实例

属性值可以是 array 或 structure，这时索引才有意义：

- **索引 0**：取属性整体内容。
- **索引非零（从 1 开始）**：定位 array/structure 内的第 N 个元素。

所以同一个 OAD 的属性 2 能展开成多个实际测点。点位的数量由对象结构和索引范围决定，**不是协议写死的清单**。

:::warning 别自己剥特征位
`Oad::attribute` 保存完整线上字节。ObjectRegistry 用属性低五位查 schema，再把原始 OAD 交给 provider；特征的业务语义由 provider 解释。标准点位绑定本批只支持特征零，非零特征返回 DAR=3。
:::

## 记录型对象：没有名字的实例

冻结数据、负荷曲线、事件记录这类**集合类对象**有一个根本问题：实例天然没有名字。标准不可能给"2026 年 10 月 1 日 00:00 的正向有功电能冻结值"分配一个 OI。

698 的解法是**二维间接寻址**：

```text
集合对象 OAD（唯一）
  + RSD  行选择器 —— 选哪几行（时间区间或序号区间）
  + RCSD 列选择器 —— 选哪几列（要哪些量）
```

行方向可以是时间区间，也可以是序号区间；列方向列出需要的若干列，空列选择器表示全选。这对应本库的 `GetRecord{attribute, rows, columns}` 三元组。

`Rsd` 有 11 个分支（0 至 10），涵盖不选择、值匹配、时间区间、采集/存储/成功时间区间、上第 n 次、最近 n 条等；`Rcsd` 是列描述符的序列，`RecordData` 节点还包含 `Road`、`Region`、`Csd`、`Ms`。逐项实现状态见[协议覆盖范围](../appendix/coverage.md)。

**记录型属性必须显式声明。** `AttributeSchema::record` 为 `false` 时，`read_record` 返回 `DAR=5`；未绑定记录处理器时返回 `DAR=4`。

## 点表从哪来

这是工程上最实际的问题。答案分三种情况：

**1. OI 查标准表。** 标准附录的对象标识符表给出 OI 语义，主站侧维护一张点表模板即可，键从 `DI` 换成 `OAD`。

**2. 属性和索引规则查接口类定义。** 数组顺序由标准决定，但实际长度还取决于单相/三相接线、费率数等设备配置。

**3. 记录型对象的行数由设备运行时决定。** 冻结数据存了多少天、曲线存了多少点，只有设备知道，必须靠 `RSD` 查询，不能预先枚举。

2017 版没有通用的自描述测点枚举服务。工程点表来自标准 OI 表、本地设备模板和厂家文档；对象配置属性及属性 1 的逻辑名可作为校验依据。本库已提供[首批标准固定点位](./standard-points.md)，不自动认定所有设备支持这些对象。

:::danger 不要把 GET 成功当成测点存在
`GetResponse` 外层 `Result` 成功但逐项 `AttributeResult::result` 是 DAR 的情况非常常见。加载点表时**必须按项判断**，不能只看外层返回。同样的规则适用于 CONNECT——`Result` 成功但 `ConnectResponse::result` 非零仍然是拒绝。
:::

## CONNECT 的一致性协商

建链时双方交换协议服务与业务功能位图，具体内容见标准附录 C。

```cpp
struct AssociationParameters {
    std::uint16_t version = 0x0010;
    std::array<std::uint8_t, 8> protocol{};   ///< 一致性位图按最高位对应序号零
    std::array<std::uint8_t, 16> function{};
    // ...
};
```

`protocol` 描述 Normal/List/Record 等服务能力；`function` 描述电能计量、复费率、事件等业务功能。二者均不是逐 OI 支持清单，不能直接证明某个点位存在。Session 保留应用显式配置的功能位并协商交集，默认全零按信息未知处理，不删除候选点。读取计划、可选提示和逐项验证见[标准记录与能力筛选](./standard-records.md)。

注意本库现状：**`Session` 只接受 NullSecurity**，非公共认证机制虽然有线格式 codec，但真实认证尚未实现。详见[连接管理 APDU](./connection.md)。

## 在本库里怎么落地

主站通过标准目录和设备模板选择请求 OAD；服务端通过应用侧注册声明实际提供的对象。只有服务端需要 schema/provider 注册，主站查表不需要绑定 provider。声明侧：

```cpp
struct AttributeSchema {
    std::uint8_t number = 2;        ///< 1 至 31
    model::DataType type = model::DataType::null;
    bool readable = true;
    bool writable = false;          ///< 写入须显式授权
    bool record = false;            ///< 记录型属性须显式声明
};
```

`register_object` 注册 OI 的 schema 和 provider，宣告本地服务端提供这些属性；GET 时按 OI 和属性低五位查 schema，查不到返回 DAR=4，随后将完整 OAD 交给 provider。标准点位可使用 `register_standard_object` 生成只读 schema 并校验返回值；完整 API 见[标准固定点位](./standard-points.md)与[对象目录与 Provider](../session/object.md)。

对照关系：

| 645 | 本库的 698 对应 |
| --- | --- |
| DI 表（协议内置） | `ObjectSchema.oi` + `AttributeSchema.number` |
| DI 的数据类型 | `AttributeSchema.type` |
| DI 的读写权限 | `AttributeSchema.readable` / `writable` |
| — | `AttributeSchema.record`：声明记录型，`read_record` 才受理 |
| 数组/结构的元素选择 | `Oad.index`，0 取整体，非零从 1 起 |

## 已验证字节

附录 D.3.1 的固定向量可以直接核对第 1 层的编码：

```text
05 01 01 40 01 02 00 00
│  │  │  └──────┘  │  └─ TimeTag 标记
│  │  │  OAD = 40 01 02 00
│  │  └─ PIID = 01
│  └─ GetRequestNormal
└─ Client-APDU = 05（GET-Request）
```

响应 `85 01 01 40 01 02 00 01 09 06 12 34 56 78 90 12`：PIID 回显为 01（ACD 位为 0），`09` 是 octet-string 标签，`06` 是长度，`123456789012` 是通信地址。

## 下一步

- 描述符的完整字段和表示约定见 [Data 精确类型](../core/data.md#描述符类型)
- 属性读取与记录查询的 APDU 结构见 [GET 与记录查询](./get.md)
- 目录校验与 provider 职责划分见 [对象目录与 Provider](../session/object.md)
