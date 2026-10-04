---
title: Data 精确类型
description: Data、DataType、描述符类型与精确类型不混用规则。
---

# Data 精确类型

头文件：`<dlt698/model/data.hpp>`，命名空间 `dlt698::model`。

## 为什么是 variant 而不是泛型值

`Data` 用 `std::variant` 保存协议数据，每个标签对应一个**独立的 C++ 类型**。这样做的直接后果是：`UInt16`、`Enum`、array/structure 和字节串在类型层面就不可能混淆，编译器会拦住把一个当成另一个用的代码。

```cpp
struct Data {
    using Payload = std::variant<Null, Array, Structure, Boolean, BitString, Int8, Int16, Int32,
                                 Int64, UInt8, UInt16, UInt32, UInt64, Enum, Float32, Float64,
                                 OctetString, VisibleString, Utf8String, DateTime, Date, Time,
                                 DateTimeS, Oi, Oad, Omd, Ti, Tsa, ScalerUnit, RecordData>;
    Payload payload = Null{};

    Data() = default;
    template <class T, /* 排除 Data 自身 */> Data(T&& value);

    DataType type() const;
    template <class T> const T& as() const;   ///< 抛 std::bad_variant_access
    friend bool operator==(const Data& a, const Data& b);
};
```

构造：

```cpp
dlt698::model::Data voltage = dlt698::model::UInt16{2413};
dlt698::model::Data values{
    dlt698::model::Array{{dlt698::model::UInt8{1}, dlt698::model::UInt8{2}}}};
```

访问：

```cpp
auto data = dlt698::model::UInt16{2413};
if (data.type() == dlt698::model::DataType::uint16) {
    std::uint16_t v = data.as<dlt698::model::UInt16>().value;
}
```

`as<T>()` 在 payload 不是 `T` 时抛 `std::bad_variant_access`。跨异步边界或需要长期保存时，请按值取出再持有，不要长期保存 `as<T>()` 返回的引用。

## DataType

枚举值与线上标签一致。

| 标签 | 类型 | 标签 | 类型 |
| --- | --- | --- | --- |
| 0 | `null` | 22 | `enumeration` |
| 1 | `array` | 23 | `float32` |
| 2 | `structure` | 24 | `float64` |
| 3 | `boolean` | 25 | `date_time` |
| 4 | `bit_string` | 26 | `date` |
| 5 | `int32` | 27 | `time` |
| 6 | `uint32` | 28 | `date_time_s` |
| 9 | `octet_string` | 80 | `oi` |
| 10 | `visible_string` | 81 | `oad` |
| 12 | `utf8_string` | 82 | `road` |
| 15 | `int8` | 83 | `omd` |
| 16 | `int16` | 84 | `ti` |
| 17 | `uint8` | 85 | `tsa` |
| 18 | `uint16` | 88 | `region` |
| 20 | `int64` | 89 | `scaler_unit` |
| 21 | `uint64` | 90–96 | `rsd`/`csd`/`ms`/`rcsd` |

已实现的标签覆盖和逐项测试状态见[协议覆盖范围](../appendix/coverage.md)。保留或未知标签返回 `unsupported_tag`，**不会**猜测长度后跳过。

## 基本包装类型

```cpp
template <DataType Tag, class T>
struct Value {
    static constexpr DataType type = Tag;
    T value{};
};
```

| 别名 | 存储类型 | 别名 | 存储类型 |
| --- | --- | --- | --- |
| `Boolean` | `bool` | `Int16` | `std::int16_t` |
| `Int8` / `UInt8` | `std::int8_t` / `std::uint8_t` | `Int32` / `UInt32` | 32 位整型 |
| `Int64` / `UInt64` | 64 位整型 | `Enum` | `std::uint8_t` |
| `Float32` / `Float64` | `float` / `double` | `OctetString` | `Bytes` |
| `VisibleString` / `Utf8String` | `std::string` | `Oi` | `std::uint16_t` |
| `DateTime` | `std::array<std::uint8_t, 10>` | `Date` | `std::array<std::uint8_t, 5>` |
| `Time` | `std::array<std::uint8_t, 3>` | `DateTimeS` | `std::array<std::uint8_t, 7>` |
| `Tsa` | `Bytes` | | |

`Value` 的 `operator==` 只比较存储值；标签由类型系统保证，不需要运行时再比一次。

## 容器类型

```cpp
struct Array { std::vector<Data> value; };       ///< 协议 array
struct Structure { std::vector<Data> value; };   ///< 协议 structure
struct BitString {
    std::size_t bit_count = 0;   ///< 有效位数，不是字节数
    Bytes value;                 ///< 长度须为 ceil(bit_count / 8)
};
```

`Array` 和 `Structure` 存储形式相同但**标签独立**，比较时递归比较子节点。`BitString` 的有效位从每个字节的高位开始，末字节未使用的低位必须为零，解码时会检查填充位。

## 描述符类型

```cpp
struct Oad {
    std::uint16_t oi = 0;
    std::uint8_t attribute = 0;   ///< 含特征位，保留完整线上字节
    std::uint8_t index = 0;
};

struct Omd {
    std::uint16_t oi = 0;
    std::uint8_t method = 0;
    std::uint8_t mode = 0;
};

struct Ti {
    std::uint8_t unit = 0;        ///< 单位编码须在 0 至 5
    std::uint16_t interval = 0;
};

struct ScalerUnit {
    std::int8_t scaler = 0;       ///< 十进制倍率
    std::uint8_t unit = 0;        ///< 物理单位
};
```

:::warning 描述符的表示约定
- `Oad::attribute` 保存**整个线字节，包括特征位**。不要自己把特征位剥掉再拼回去。
- `ServerAddress::bytes` 和 `Tsa` 按**线序**保存，地址的低有效字节在前。
- `ScalerUnit` 只保存倍率和单位，**工程值换算由调用方完成**，库不隐含单位语义。
- `Ti` 不做单位换算，比较只比字段本身。

`Tsa` 的内容包含地址描述字节和地址字节，**不包含** A-XDR 外层长度。
:::

## RecordData

记录类查询的描述符（ROAD、Region、RSD、CSD、MS、RCSD）用 `RecordData` 保存：

```cpp
class RecordData {
  public:
    template <class T> explicit RecordData(T value);
    DataType type() const;
    template <class T> const T& as() const;
    friend bool operator==(const RecordData& a, const RecordData& b);
};
```

它是不可变、拥有内存的节点，用于打断 `Data` 与 `Rsd`/`Region` 之间的递归定义循环。复制会共享不可变内容，**不能**经别名修改快照。`as<T>()` 在类型不符或对象已被移动时抛 `std::bad_variant_access`。

## 相等比较

`operator==` 比较协议类型和内容，容器类型递归比较子节点。浮点值遵循 C++ 浮点比较语义——`NaN != NaN`，`Float32` 的比较不会做特殊处理。需要业务上的"数值相等"时，请自己先取出数值再比。
