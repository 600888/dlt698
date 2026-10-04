---
title: 对象目录与 Provider
description: ObjectSchema、ObjectRegistry、IObjectProvider 与 MemoryObject。
---

# 对象目录与 Provider

头文件：`<dlt698/service/object.hpp>`，命名空间 `dlt698::service`。

## 值类型

```cpp
using ObjectValue = std::variant<std::uint8_t, model::Data>;   ///< DAR 或精确类型 Data

struct ActionValue {
    std::uint8_t dar = 0;
    std::optional<model::Data> data;
};
```

`ObjectValue` 是 variant：拿到 `uint8_t` 就是 DAR，拿到 `model::Data` 才是值。判断方式：

```cpp
if (auto* dar = std::get_if<std::uint8_t>(&value))
    handle_error(unsigned(*dar));
else
    handle_value(std::get<model::Data>(value));
```

## Schema

```cpp
struct AttributeSchema {
    std::uint8_t number = 2;        ///< 1 至 31
    model::DataType type = model::DataType::null;
    bool readable = true;
    bool writable = false;          ///< 写入须显式授权
    bool record = false;            ///< 记录型属性须显式声明
};

struct MethodSchema {
    std::uint8_t number = 1;              ///< 1 至 255
    std::optional<model::DataType> parameter_type;
    std::optional<model::DataType> return_type;
    bool executable = true;
};

struct ObjectSchema {
    std::uint16_t oi = 0;
    std::string name;
    std::vector<AttributeSchema> attributes;
    std::vector<MethodSchema> methods;
};
```

:::warning writable 默认关闭
`AttributeSchema::writable` 默认是 `false`。已经注册为只读的 schema **不会**因为需要写入而自动获得权限，必须显式打开。
:::

属性编号在 1 至 31 之间互不重复，方法编号在 1 至 255 之间互不重复。属性零的整个对象读取**未实现**。

## ObjectRegistry

```cpp
class ObjectRegistry {
  public:
    Result<void> register_object(ObjectSchema schema, std::shared_ptr<IObjectProvider> provider);

    ObjectValue read(const model::Oad& attribute) const;
    std::uint8_t write(const model::Oad& attribute, const model::Data& value) const;
    ActionValue invoke(const model::Omd& method, const model::Data& parameter) const;
    protocol::apdu::RecordResult read_record(const protocol::apdu::GetRecord& query) const;
};
```

`register_object` 要求至少定义一个属性或方法，OI 不得重复。成功返回 `Result<void>`，schema 非法或 OI 重复返回错误。

### 目录做什么、不做什么

目录负责**校验**，provider 负责**取值**：

| 检查项 | 由谁负责 |
| --- | --- |
| schema 是否存在 | 目录 |
| 读/写/执行权限 | 目录 |
| 完整属性的 `Data` 类型是否匹配 | 目录 |
| 非零索引的元素类型 | provider |
| 属性特征位语义 | provider |
| 实际取值、写入、方法执行 | provider |

### 锁约定

目录**先检查权限，再释放目录锁后调用 provider**，最后校验完整属性的 `Data` 类型。这样 provider 可以重入本对象，不会死锁。

provider 抛出的异常会被目录转成 `DAR=255`。

### DAR 取值

| DAR | 目录返回时机 |
| --- | --- |
| 3 | 权限拒绝 |
| 4 | 属性未定义 |
| 5 | 请求记录查询但属性不是记录型 |
| 7 | 完整属性类型不匹配 |
| 8 | 索引非法（由 `MemoryObject` 返回） |
| 255 | provider 抛出异常 |

## IObjectProvider

```cpp
class IObjectProvider {
  public:
    virtual ~IObjectProvider() = default;

    virtual ObjectValue read(const model::Oad& attribute) = 0;
    virtual std::uint8_t write(const model::Oad& attribute, const model::Data& value);
    virtual ActionValue invoke(const model::Omd& method, const model::Data& parameter);
    virtual protocol::apdu::RecordResult read_record(const protocol::apdu::GetRecord& query);
};
```

只有 `read` 是纯虚函数。`write`、`invoke`、`read_record` 都有默认实现并**返回拒绝**：

- `write` 默认拒绝，保持既有只读 provider 的合约不变。
- `invoke` 默认拒绝。
- `read_record` 默认返回 `DAR=3`。

实现约定：

- `attribute` 是**完整 OAD**，目录不会隐式丢弃属性特征位。
- `value`/`parameter` 在调用期间有效，**不得保留借用引用**。
- **不得阻塞等待调用它的会话执行器。**
- 调用期间不得保留 `ByteView` 之类的借用引用；需要长期持有就复制数据。
- 并发读取的线程安全由你自己保证。
- 返回值必须拥有全部内存——不能返回指向内部临时缓冲区的视图。

## MemoryObject

`MemoryObject` 是线程安全的本地模拟 provider，用于测试和联调。

```cpp
class MemoryObject final : public IObjectProvider {
  public:
    void set(std::uint8_t attribute, model::Data value);   ///< 本地配置，不是协议 SET
    ObjectValue read(const model::Oad& attribute) override;
    std::uint8_t write(const model::Oad& attribute, const model::Data& value) override;
    void bind_method(std::uint8_t number,
                     std::function<ActionValue(const model::Omd&, const model::Data&)> handler);
    void bind_record(std::uint8_t attribute,
                     std::function<protocol::apdu::RecordResult(const protocol::apdu::GetRecord&)> handler);
    ActionValue invoke(const model::Omd& method, const model::Data& parameter) override;
    protocol::apdu::RecordResult read_record(const protocol::apdu::GetRecord& query) override;
};
```

### set 不是 SET

:::warning `set` 是本地配置方法
`MemoryObject::set(attribute, Data)` 设置属性的**完整值**，它是初始化模拟数据用的**本地方法**，与协议 SET 服务无关。协议 SET 走的是 `write`。
:::

`set` 只支持属性特征零，属性编号非法抛 `std::invalid_argument`。

### 索引规则

- **索引零**读取完整值。
- **非零索引**从 1 开始，读取 array/structure 的**一级元素**。
- 非法索引返回 `DAR=8`。

`write` 只替换**已有**的完整值或一级元素，**不会创建**未知属性，并且保持原类型：类型不匹配返回 `DAR=7`，非法索引返回 `DAR=8`，属性不存在返回 `DAR=4`。

### 方法与重入

`bind_method` 注册的模式只能是**模式零**，方法编号不能为零或回调为空（抛 `std::invalid_argument`）。回调**复制**一份后释放对象锁，因此允许在回调内重入本对象的读写。

未定义的方法返回 `DAR=4`，非零模式返回 `DAR=3`。

`bind_record` 的处理器在对象锁**外**调用，必须返回拥有内存且列宽一致的快照，并解释全部所需选择器。属性编号非法或回调为空抛 `std::invalid_argument`。

## 完整示例

```cpp
auto objects = std::make_shared<dlt698::service::ObjectRegistry>();
auto object = std::make_shared<dlt698::service::MemoryObject>();
object->set(2, dlt698::model::UInt16{10});

// 用 weak_ptr 打破引用环：目录持有 provider，provider 的回调不能再捕获 shared_ptr。
object->bind_method(1,
                    [weak = std::weak_ptr<dlt698::service::MemoryObject>(object)](
                        const dlt698::model::Omd&, const dlt698::model::Data& parameter)
                        -> dlt698::service::ActionValue {
                        weak.lock()->set(2, parameter);
                        return {0, parameter};
                    });

if (!objects->register_object(
        {0x2000,
         "可写值及模拟方法",
         {{2, dlt698::model::DataType::uint16, true, true}},
         {{1, dlt698::model::DataType::uint16, dlt698::model::DataType::uint16, true}}},
        object))
    return 1;
```

:::danger 注意引用环
`register_object` 的目录持有 provider 的 `shared_ptr`。如果 provider 的方法回调又捕获这个 `shared_ptr`，就形成引用环，对象永远不会被释放。用 `std::weak_ptr` 并在回调内 `lock()`。
:::

## 局限

- 不隐含单位、倍率或真实计量能力——`ScalerUnit` 只保存倍率和单位，换算由调用方做。
- 完整标准对象目录尚未提供，需要自己按设备点表注册。
- 记录查询的选择器业务语义由 provider 解释，库只校验声明和权限。
