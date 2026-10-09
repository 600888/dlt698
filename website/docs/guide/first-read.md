---
title: 第一次读取
description: 从安装到读到第一个数值的完整流程，含可编译程序和常见卡点。
---

# 第一次读取

这一页带你走完一遍：安装库、写一个客户端、连上、读到第一个数值并打印出带单位的工程值。

## 安装与链接

在仓库根目录构建并安装：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix ./build/stage
```

Visual Studio 等多配置生成器必须带 `--config Release`，`-DCMAKE_BUILD_TYPE` 不生效。

在你的项目里引用：

```cmake
find_package(dlt698 1.0 CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE dlt698::dlt698)
```

`dlt698::dlt698` 包含托管客户端和服务器，**需要构建时启用 `DLT698_BUILD_TRANSPORT`**（默认开启）。只做编解码或内存中的离线验证时，可设置 `DLT698_BUILD_TRANSPORT=OFF`，仍链接同一个库。安装目标见[安装与构建](../getting-started/installation.md)。

## 先用配套示例验证环境

仓库自带两个可以直接配对运行的程序，先确认库工作正常，再对接真实设备：

```sh
# 终端一：启动模拟电表，监听 6980
dlt698_server

# 终端二：连接并读取
dlt698_client
```

`dlt698_client` 输出：

```text
电网频率 [index=0]=50.00 Hz
电压 [index=0]=230.0 V, 231.0 V, 229.0 V
电流 [index=0]=5.000 A, 10.000 A, 15.000 A
有功功率 [index=0]=6000.0 W, 1000.0 W, 2000.0 W, 3000.0 W
无功功率 [index=0]=3000.0 var, 500.0 var, 1000.0 var, 1500.0 var
功率因数 [index=0]=0.894, 0.894, 0.894, 0.894
正向有功电能 [index=0]=1234.56 kWh, 100.00 kWh, 200.00 kWh, 300.00 kWh, 634.56 kWh
电能表通信地址 [index=0]=00 00 00 00 00 00
电压 [index=1]=230.0 V
正向有功电能 [index=2]=100.00 kWh
```

如果这两个程序能跑通，说明构建、安装和运行链路都正常。**注意它们跑在环回地址上，走的是模拟设备，不是真实电表。**

## 写一个最小客户端

```cpp
#include <dlt698/app.hpp>
#include <dlt698/standard/catalog.hpp>
#include <iostream>

int main(int argc, char** argv) {
    using namespace dlt698;
    if (argc != 3) {
        std::cerr << "Usage: meter_read <host> <port>\n";
        return 2;
    }

    app::ClientOptions options;
    // SA 必须与设备实际地址一致；示例用六字节全零。
    // 按线序填写，低有效字节在前。
    options.protocol.server.bytes = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    options.protocol.server.logical = 0;  // 逻辑地址 0～3
    options.protocol.client_address = 0;  // CA，两端必须一致
    app::Client client(options);

    // 三种关联模式：远程（默认，等服务器 LINK）、本地公共、本地预设。
    // 连接真实采集终端通常用默认的 remote_public。
    auto connected = client.connect_tcp(argv[1], std::stoi(argv[2]));
    if (!connected) {
        std::cerr << "connect failed: " << connected.error().context << '\n';
        return 1;
    }

    // 读取电网频率：OI=200F，属性 2，索引 0（整体）
    auto value = client.get({0x200F, 2, 0});
    if (!value) {
        std::cerr << "get failed: " << value.error().context << '\n';
        client.disconnect();
        return 1;
    }
    // 外层成功不代表设备返回了数据，必须区分 DAR 和 Data。
    if (const auto dar = std::get_if<std::uint8_t>(&value.value())) {
        std::cerr << "meter rejected attribute, DAR=" << unsigned(*dar) << '\n';
        client.disconnect();
        return 1;
    }

    const auto& data = std::get<model::Data>(value.value());
    // 校验精确类型并换算倍率：线上 5000 是原始整数，50.00 Hz 才是工程值。
    auto numbers = standard::engineering_values({0x200F, 2, 0}, data);
    if (!numbers || numbers.value().empty()) {
        std::cerr << "unexpected value\n";
        client.disconnect();
        return 1;
    }
    std::cout << "frequency = " << standard::decimal_text(numbers.value()[0]) << ' '
              << standard::unit_symbol(numbers.value()[0].scaling.unit) << '\n';

    auto released = client.disconnect();
    return released ? 0 : 1;
}
```

不需要写事件循环、不需要创建 `Session`、不需要手动处理分帧。`Client` 内部持有工作线程，`disconnect()` 会发送 RELEASE、等待传输收尾并回收线程。

运行：

```sh
meter_read 127.0.0.1 6980
```

## 这段程序里容易出错的地方

**`engineering_values` 不是可选的装饰。** `model::UInt16{5000}` 的原始值是 5000，按 `200F` 的倍率 -2 换算才是 50.00 Hz。不同对象的倍率不同：电压是 -1、电流是 -3、电能是 -2、需量是 -4。写错倍率的读数看起来"差不多"，但电量计费会出错。

**DAR 必须检查。** 设备不支持某个属性时回 DAR=4，`Result` 依然是成功的。`dlt698_client` 示例里电网频率那一行如果显示 `DAR=4`，意思是这个模拟设备没有提供频率，不是程序出错。

**SA/CA 不匹配会被静默丢弃。** 地址字节、地址长度、逻辑地址或 CA 任一不符，帧在分发前就被拒绝，你会看到超时而不是明确的地址错误。核对两端配置。

**单在途事务。** 多个线程同时调用 `get()`，第二个返回 `busy`。批量读取用 `get_list()` 而不是并发调用。

## 读多个数据

单次读取改成列表，一次往返拿多个属性：

```cpp
auto result = client.get_list({
    {0x200F, 2, 0},  // 频率
    {0x2000, 2, 0},  // 三相电压
    {0x0010, 2, 0},  // 正向有功电能（总量+费率数组）
});
if (!result) {
    std::cerr << result.error().context << '\n';
} else {
    // 列表保留请求顺序，允许部分成功：逐项检查，不要因为一项失败就丢弃整批。
    for (const auto& entry : result.value().attributes) {
        if (const auto dar = std::get_if<std::uint8_t>(&entry.result)) {
            std::cerr << "OI=" << std::hex << entry.attribute.oi << std::dec
                      << " DAR=" << unsigned(*dar) << '\n';
            continue;
        }
        const auto& data = std::get<model::Data>(entry.result);
        auto numbers = standard::engineering_values(entry.attribute, data);
        if (!numbers) continue;
        const auto* object = standard::find_object(entry.attribute.oi);
        std::cout << (object ? object->name : "unknown") << ": "
                  << standard::decimal_text(numbers.value()[0]) << '\n';
    }
}
```

细节见[一次读取多个数据](./batch-read.md)。

## 串口连接

串口只需换掉连接那一行，其余代码不变：

```cpp
// 默认 9600/8E1，库自动加四个 FE 和 33 位帧间隔。
auto connected = client.open_serial("COM3", 9600);
```

字格式、波特率、RS-485 方向切换见[串口连接](./serial.md)。

## 下一步

- 想读电能、需量、电压等具体数据：从[使用指南首页](./index.md)的数据类别表进入
- 连接不稳定或超时：看[常见问题排查](./troubleshooting.md)
- 需要理解 OAD 三个字段的含义：看[看懂数据地址](./addressing.md)
- 需要改参数或调方法：看[写参数与执行方法](./write-action.md)
