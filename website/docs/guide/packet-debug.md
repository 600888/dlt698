---
title: 报文调试
description: 用 dlt698_decode 拆解链路帧和 APDU；自写解码程序、校验和排查、抓包与手工构造报文的要点。
---

# 报文调试

## 能做什么

调试 DL/T 698 报文时，你手上通常只有一串十六进制字节。库自带一个解码工具 `dlt698_decode`，不需要写代码就能拆开看。

## 在应用中监控收发

托管入口提供可选 `ClientOptions::traffic` 和 `ServerOptions::traffic`；不配置时不会为发送观察复制缓冲区。分层入口可在 `Session::start()` 前调用 `set_traffic_handler(handler)`，传入空回调可注销。

```cpp
#include <dlt698/app.hpp>
#include <chrono>
#include <iostream>

int main() {
    const auto log = [](const dlt698::session::TrafficEvent& event) {
        const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
            event.timestamp.time_since_epoch()).count();
        const bool tx = event.direction == dlt698::session::TrafficDirection::send;
        std::cout << milliseconds << (tx ? " TX " : " RX ")
                  << dlt698::to_hex(event.bytes);
        if (!event.result) std::cout << " failed: " << event.result.error().context;
        std::cout << '\n';
    };

    dlt698::app::ClientOptions options;
    options.traffic = log;
    dlt698::app::Client client(options);
    auto connected = client.connect_tcp("127.0.0.1", 6980);
    if (!connected) return 1;
    auto value = client.get({0x200F, 2, 0});
    auto disconnected = client.disconnect();
    return value && disconnected ? 0 : 2;
}
```

服务器用相同事件类型，并额外提供连接 ID：

```cpp
dlt698::app::ServerOptions options;
options.traffic = [](std::uint64_t connection_id,
                     const dlt698::session::TrafficEvent& event) {
    std::cout << "connection=" << connection_id
              << (event.direction == dlt698::session::TrafficDirection::send ? " TX " : " RX ")
              << dlt698::to_hex(event.bytes) << '\n';
};
dlt698::app::Server server(options);
```

连接 ID 非零，在一次服务器运行内唯一，与 `diagnostic` 使用同一 ID；服务器重启后重新编号，监听器没有报文事件。

| 字段 | 含义 |
| --- | --- |
| `direction` | 相对于当前会话的 `receive` / `send` |
| `timestamp` | 通道完成回调入口的 `system_clock::time_point`，不代表物理线路时刻，系统校时可能使它回退 |
| `bytes` | 回调期间有效的只读 `ByteView`，保存或跨线程处理须复制为 `Bytes` |
| `result` | RX 恒成功；TX 保留通道写入的成功或完整错误信息 |

RX 在流解析前观察成功读取的字节块，包含半帧、粘帧、噪声、校验失败帧和接收到的 FE 前导字节。读失败没有 RX 字节事件，错误仍由诊断或事务结果报告。需要按完整帧显示时，在自己的观察器中按连接维护 `FrameStreamDecoder`。

TX 每次实际提交给会话通道的写操作完成时通知一次，包括 LINK、CONNECT、业务请求/响应、确认、重发和 RELEASE。字节是提交的完整链路帧，**不含串口适配器随后添加的四个 FE**；编码失败或排队后尚未提交通道就被取消，没有 TX 事件。成功表示通道完成写入，不保证对端收到或处理；失败可能已经发送部分字节，`bytes` 仍保留完整提交内容。

回调在会话串行执行器中运行；托管 Client/Server 使用自己的工作线程。异常被隔离，回调应快速返回；生产应用可复制字节到有容量限制的日志队列。回调内不要同步读取、连接、断开或停止同一入口，可使用 `request_disconnect()` / `request_stop()` 发起关闭，不应强引用对应入口形成引用环。

注册与注销异步生效。RX 使用处理时的观察器，TX 使用提交时的观察器快照；已提交的 TX 即使注销、关闭或销毁 Session，仍向原观察器报告最终结果，须持续驱动执行器。观察器引用的日志资源必须活到回调排空；托管入口的 `disconnect()` / `stop()` 在普通调用线程等待线程收尾。启用观察后，每个在途写入额外保留一份完整帧，库不积累历史日志。

## 用 dlt698_decode 拆帧

```bash
dlt698_decode "68 25 00 C3 05 00 00 00 00 00 00 00 48 85 85 03 00 50 04 02 00 01 00 00 10 02 01 01 01 06 00 01 E2 40 00 00 3A 61 16"
```

输出：

```text
SA (wire order): 00 00 00 00 00 00
CA: 0
APDU: 85 03 00 50 04 02 00 01 00 00 10 02 01 01 01 06 00 01 E2 40 00 00
GET records: 1
```

这个工具做三件事：

| 步骤 | 校验内容 |
| --- | --- |
| 拆链路帧 | 起始符 `68`、长度域、HCS、FCS、结束符 `16` |
| 取出地址 | SA（10 字节）、CA、逻辑地址 |
| 解析 APDU | 服务号、选择子、PIID 及已支持的结构 |

**帧校验不过就不会往下走** —— 这与库的行为一致，避免在损坏的字节上做无意义的解析。

### 它能解什么，不能解什么

| 状态 | 说明 |
| --- | --- |
| GET（含 Record、Next） | 完整解析，显示属性数或记录数 |
| CONNECT / LINK / RELEASE | 解析成功，显示原始 APDU 字节 |
| SET / ACTION | 解析成功，显示原始 APDU 字节 |
| 未实现的变体 | 链路层仍能拆出 APDU，只是结构层不解 |

工具对不支持的 APDU **仍会打印 `APDU:` 那一行** —— 拿不到字节时先确认链路层是不是通的。

## 自写解码程序

需要按对象类型逐字段展开时，自己写一个。关键是**用库自己的接口**，不要手算偏移：

```cpp
#include <dlt698/codec/data_codec.hpp>
#include <dlt698/model/record.hpp>
#include <dlt698/protocol/apdu/get.hpp>
#include <dlt698/protocol/link/frame.hpp>
#include <iostream>

int main() {
    using namespace dlt698;

    const auto bytes = from_hex("68 25 00 C3 05 00 00 00 00 00 00 00 48 85 85 03 00 50 04 02 00 "
                                "01 00 00 10 02 01 01 01 06 00 01 E2 40 00 00 3A 61 16");
    if (!bytes) {
        std::cerr << bytes.error().context << '\n';
        return 1;
    }

    // 1) 先拆链路层：长度、HCS、FCS 都在这里校验
    auto frame = protocol::link::decode_frame(bytes.value());
    if (!frame) {
        std::cerr << "link: " << frame.error().context
                  << " at offset " << frame.error().offset << '\n';
        return 1;
    }

    const auto& f = frame.value();
    std::cout << "SA: " << to_hex(f.server.bytes) << "  CA: " << unsigned(f.client) << '\n';
    std::cout << "control: " << std::hex << unsigned(f.control) << std::dec << '\n';
    std::cout << "APDU: " << to_hex(f.payload) << '\n';

    // 2) 再解 APDU：payload 是解扰码后的干净字节
    auto decoded = protocol::apdu::decode_get(ByteView(f.payload.data(), f.payload.size()));
    if (!decoded) {
        std::cerr << "apdu: " << decoded.error().context << '\n';
        return 1;
    }

    // 3) 按变体分派；GetApdu 是 variant，用 get_if 判断
    if (const auto* response = std::get_if<protocol::apdu::GetRecordResponse>(&decoded.value())) {
        for (const auto& record : response->records) {
            std::cout << "record oi=" << std::hex << record.attribute.oi << std::dec
                      << " attr=" << unsigned(record.attribute.attribute)
                      << " index=" << unsigned(record.attribute.index) << '\n';
            std::cout << "  columns=" << record.columns.size() << '\n';
            for (const auto& column : record.columns)
                std::cout << "    column oi=" << std::hex << std::get<model::Oad>(column).oi
                          << std::dec << '\n';

            if (const auto* rows = std::get_if<std::vector<protocol::apdu::RecordRow>>(&record.result)) {
                std::cout << "  rows=" << rows->size() << '\n';
                for (const auto& row : *rows) {
                    std::cout << "    cells=" << row.size();
                    for (const auto& cell : row)
                        std::cout << " type=" << unsigned(cell.type());
                    std::cout << '\n';
                }
            } else if (const auto dar = std::get_if<std::uint8_t>(&record.result)) {
                std::cout << "  DAR=" << unsigned(*dar) << '\n';
            }
        }
    }
    return 0;
}
```

:::danger 不要手算 payload 偏移
链路帧里有长度域、HCS、FCS 和可选的字节填充/扰码。手算 APDU 在帧内的起始位置几乎一定会错，而且**错得很隐蔽**（能解出东西但值是错的）。

**始终用 `decode_frame` 取出 `payload`，再交给 `decode_get`。**
:::

## 构造报文做验证

反向也用库，不要手拼字节：

```cpp
#include <dlt698/protocol/link/frame.hpp>

protocol::link::Frame frame;
frame.control = 0xC3;
frame.server.bytes = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
frame.server.logical = 0;
frame.client = 0;
frame.payload = payload_bytes;

auto wire = protocol::link::encode_frame(frame);
if (!wire) {
    std::cerr << wire.error().context << '\n';
    return 1;
}
std::cout << to_hex(wire.value()) << '\n';

// 关键：立刻回读自检
if (auto back = protocol::link::decode_frame(wire.value()); !back)
    std::cerr << "round-trip FAILED: " << back.error().context << '\n';
```

**encode 之后必须 decode 回读。** 这能立刻发现长度域或校验和算错 —— 本项目的文档报文全部是这样生成并核对的。

控制域的取值：

| 方向 | 控制域 |
| --- | --- |
| 请求 | `0x43` |
| 响应 | `0xC3` |

## 校验和的两个坑

链路帧用 **CRC-16**，反射多项式 `0x8408`，初值和最终异或均为 `0xFFFF`，**低字节在前**。

| 容易搞错的地方 | 正确做法 |
| --- | --- |
| 字节序 | FCS 低字节在前 |
| 参与范围 | HCS 只覆盖 SA 到控制域；FCS 覆盖 SA 到 APDU 末尾 |
| 字节填充 | 地址字节里的 `0x68`、`0x11`、`0x13` 等需要转义（FE 机制） |

**不要手算 HCS/FCS。** 用 `encode_frame` 生成，或用 `decode_frame` 验证。

## 从设备抓包

### 串口

用带十六进制显示的串口工具（如 `picocom -b 9600 -H`、SecureCRT、Windows 串口助手）记录原始字节，再喂给 `dlt698_decode`。

抓包时注意：

- 四个 `FE` 前导字节属于链路层，`decode_frame` 能处理
- **两个帧之间至少要有 33 位时间的间隔**，抓包工具可能看不到这个间隔，但设备依赖它
- RS-485 半双工要确认方向切换时机，否则会截断

### TCP

用 `tcpdump`/`Wireshark` 抓 6980 端口。TCP 是字节流，抓到的数据可能**跨包或粘包**：

```
一次 recv 可能拿到半帧，也可能拿到两帧加半帧
```

**必须交给流解析器处理**，不能按包解析。库的做法是：

| 配置 | 作用 |
| --- | --- |
| `Limits::max_stream_bytes` | 流解析器缓存上限，默认 32770 |
| `Limits::max_frame_bytes` | 单帧上限，默认 16385 |

库会自动缓存并按帧边界切分。你在抓包里看到的"半个 APDU"不需要手工拼。

## 排查思路

| 现象 | 检查顺序 |
| --- | --- |
| `decode_frame` 失败，报 checksum | 抓包丢字节或多了字节；核对长度域 |
| 报错但提示 offset 在中间 | 该位置前后有转义字节（FE 机制） |
| 长度域看起来对但解不出 APDU | 可能有字节填充，检查控制域 |
| 能拆出 APDU 但服务号不认识 | 该服务未实现，看 `APDU:` 那行字节 |
| 解出的值差一个数量级 | Check 的是工程值还是原始值，见各数据类别页的倍率表 |
| 响应与请求对不上 | 看 SA 和 CA 是否一致，PIID 是否匹配 |

## 相关配置

| 配置 | 位置 | 说明 |
| --- | --- | --- |
| `Limits::max_frame_bytes` | 库连接参数 | 单帧上限，超出返回 `resource_limit` |
| `Limits::max_stream_bytes` | 库连接参数 | TCP 流缓存上限 |
| `Limits::max_data_bytes` | 库连接参数 | 单个 Data 树上限，默认 1 MB |

## 常见失败

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| `dlt698_decode` 找不到 | 未构建示例程序 | 编译 `cpp/examples/codec/decode.cpp` |
| 提示用法错误 | 没传参数或传了多个 | 只传一个十六进制串，整体加引号 |
| `from_hex` 失败 | 串里有非十六进制字符 | 去掉 `0x` 前缀，用空格分隔 |
| `checksum_header` | HCS 不匹配 | 抓包丢字节，或地址字节转义错误 |
| `checksum_frame` | FCS 不匹配 | 抓包丢字节；确认串口参数（波特率、校验位、停止位） |
| `trailing_data` | 帧后有多余字节 | 一次喂给工具的串包含了两帧 |
| `need_more_data` | 帧不完整 | 抓包只拿到部分数据 |
| `unsupported_service` | 服务号未实现 | 用 `APDU:` 那行看原始字节 |
| `resource_limit` | 超出资源上限 | 调大对应 `Limits`，或检查是否有异常长帧 |
| 解出的 PIID 与请求不符 | 响应错配或有并发请求 | 库按 PIID 匹配；确认没有多个在途事务 |
