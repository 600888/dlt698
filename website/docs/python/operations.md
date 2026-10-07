---
title: 常用操作
description: 串口、批量读取、写入、记录查询及结果处理的 Python 示例。
comments: false
---

# 常用操作

除完整程序外，下面片段中的 `client` 均指已完成 TCP 或串口关联的 `Client`。点位和参数须按实际设备选择。

## 串口与超时配置

```python
from dlt698 import Client, ClientOptions, Oad, SessionOptions
from dlt698.transport import SerialLinkOptions, SerialOptions

options = ClientOptions(protocol=SessionOptions(request_timeout=2.0))
serial = SerialOptions(baud_rate=9600)
link = SerialLinkOptions(baud_rate=9600, bits_per_character=11)

with Client(options) as client:
    client.open_serial_configured("COM3", serial, link)
    print(client.get(Oad(oi=0x200F, attribute=2)).require_data().as_uint16())
```

需连接真实设备；Linux 端口可改为 `/dev/ttyUSB0`。默认字格式为 8E1；保持字格式、波特率和 `bits_per_character` 一致。简单配置也可使用 `client.open_serial("COM3", 9600)`。现场方向控制、排空及间隔要求见[串口连接](../guide/serial.md)。

超时单位为秒，要求有限、非负且为毫秒精度。嵌套配置读取返回副本，修改后需赋回：

```python
protocol = options.protocol
protocol.request_timeout = 3.0
options.protocol = protocol  # 在创建 Client 前修改配置
```

## Data 与编解码

```python
from dlt698 import Data, decode_data, encode_data

value = Data.uint16(5000)
wire = encode_data(value)
print(wire.hex())                         # 121388
print(decode_data(wire).as_uint16())       # 5000

values = Data.array([Data.uint16(1), Data.uint16(2)])
print([item.as_uint16() for item in values.value])  # [1, 2]
```

用工厂明确标签：`Data.uint16(...)`、`Data.int32(...)`、`Data.octet_string(b"...")` 等；整数工厂拒绝 bool 和溢出，访问器要求标签匹配。字节输入接受 bytes、bytearray 或连续一维单字节 memoryview，并复制拥有。详细类型与编码见 [Data 精确类型](../core/data.md)和[Data 编解码](../core/codec.md)。

## OAD、工程量与批量读取

```python
from dlt698 import Data, Oad
from dlt698.standard import decimal_text, engineering_values, unit_symbol

frequency = Oad(oi=0x200F, attribute=2, index=0)
value = client.get(frequency).require_data()
for number in engineering_values(frequency, value):
    print(decimal_text(number), unit_symbol(number.scaling.unit))

response = client.get_list([frequency, Oad(oi=0xF001, attribute=2)])
for item in response.attributes:
    if isinstance(item.result, Data):
        print(item.attribute.oi, item.result.type, item.result.value)
    else:
        print(item.attribute.oi, "DAR", item.result)
```

`0xF001` 用来展示可能缺失的点位，替换为实际所需对象。列表保留请求顺序和逐项成功/失败；不要假设所有返回值都是 Data。工程量转换根据标准目录和布局进行校验，详细地址及倍率见[看懂数据地址](../guide/addressing.md)、[标准固定点位](../protocol/standard-points.md)和[一次读取多个数据](../guide/batch-read.md)。

## 写参数与执行方法

以下函数用于已确认可写的属性或方法；由调用方传入设备支持的 `Oad` / `Omd` 和精确 Data 参数。

```python
from dlt698 import Client, Data, Oad, Omd


def write_parameter(client: Client, attribute: Oad, value: Data) -> None:
    dar = client.set(attribute, value)
    print("SET DAR", dar)


def invoke_method(client: Client, method: Omd, parameter: Data) -> None:
    result = client.action(method, parameter)
    print("ACTION DAR", result.dar)
    if result.data is not None:
        print(result.data.type, result.data.value)
```

`Omd(oi=..., method=..., mode=...)` 描述方法，编号和参数按设备约定填写。DAR=0 表示业务成功；非零 DAR 保留原码。库不自动重试 SET/ACTION；超时或取消后的远端执行结果可能未知。权限与方法参数见[写参数与执行方法](../guide/write-action.md)。

## 查询记录

```python
from dlt698.standard import record_sequences

query = record_sequences(0x5004, 1, 3)
record = client.get_record(query)
if isinstance(record.result, int):
    print("记录 DAR", record.result)
else:
    print("实际列", record.columns)
    for row in record.result:
        print([(cell.type, cell.value) for cell in row])
```

示例查询日冻结序号区间 `[1, 3)`，设备必须提供相应记录。读取使用响应中的实际列，数据保持精确标签；查询模板和选择器见[标准记录与能力筛选](../protocol/standard-records.md)，业务场景见[日冻结](../guide/daily-freeze.md)、[月冻结](../guide/monthly-freeze.md)和[事件记录](../guide/events.md)。

## 发布服务器数据

```python
from dlt698 import Data, Device, Oad, Server

device = Device()
frequency = Oad(oi=0x200F, attribute=2)
device.set(frequency, Data.uint16(5000))

with Server(device) as server:
    server.start_tcp("127.0.0.1", 6980)
    input("服务器已启动，按 Enter 停止\n")
```

其他客户端可读取该端口；运行中调用 `device.set(...)` 或 `server.set(...)` 更新数据。共享 Device 独立于连接保存数据。本地 `set` 不授予远端 SET 权限，目录和可写 schema 见[对外提供设备数据](../guide/server.md)。

## 错误与事件

```python
from dlt698 import DarError, Dlt698Error, Oad

try:
    value = client.get(Oad(oi=0x200F, attribute=2)).require_data()
except DarError as error:
    print("业务拒绝", error.dar)
except Dlt698Error as error:
    print(error.code, error.offset, error.context, error.remote_code)
```

单次 GET 返回 `ReadResult`，只有显式 `require_data()` 才将业务 DAR 转为 `DarError`。连接、协议等本地失败抛 `Dlt698Error` 子类；超时也属于 `TimeoutError`。原始诊断字节保存在 `context_bytes`，错误类别和 DAR 见[错误码参考](../appendix/error-codes.md)。

同步诊断事件通过 `Client(on_event=...)` / `Server(on_event=...)` 接收，再由业务线程调用 `dispatch_events()` 分发。事件队列有界，超限计入 `dropped_events`；关闭后停止分发。回调用于观察，不替代业务结果；一个连接同时只允许一个在途请求。
