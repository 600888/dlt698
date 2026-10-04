# 当前 C++ API

这是第一批开发接口，0.x 阶段允许调整。`dlt698::core` 和聚合目标 `dlt698::dlt698` 始终可用；`dlt698::transport` 仅在启用传输构建时存在，聚合目标也会随之包含传输库。

## 精确类型与结果

```cpp
#include <dlt698/dlt698.hpp>

dlt698::model::Data voltage = dlt698::model::UInt16{2413};
auto encoded = dlt698::codec::encode_data(voltage);
if (encoded) {
    auto decoded = dlt698::codec::decode_data(encoded.value());
    // decoded.value().as<dlt698::model::UInt16>().value == 2413
}
```

Data 包装类型是协议类型，不会把 UInt16、Enum、array/structure 或字节串混成同一个泛型值。OAD 的 attribute 保存整个线字节，包括特征位；地址 bytes 按线序保存。日期时间保持原始字段及通配值，不隐式附加本机时区。

公开完整输入编解码函数返回 `Result<T>`，检查成功后访问 `value()`，失败后访问 `error()`。对错误状态访问 value 是调用错误。错误包含类型、偏移和字段上下文。`Reader`、`Writer` 及组合解析函数可能抛出 `DecodeFailure`，供 codec 内部组合；完整输入接口负责转换。

`ByteView` 借用内存，禁止从临时 Bytes 构建。codec 在调用结束前完成读取，返回拥有内存的值。异步写接管 Bytes，读完成返回拥有内存的 Bytes。

## GET 和帧

```cpp
using namespace dlt698;
protocol::apdu::GetRequest request{1, false, {{0x4001, 2, 0}}, {}};
auto apdu = protocol::apdu::encode_get(protocol::apdu::GetApdu{request});
if (apdu) {
    protocol::link::Frame frame;
    frame.server.bytes = {0x07, 0x09, 0x19, 0x05, 0x16, 0x20};
    frame.payload = std::move(apdu).value();
    auto wire = protocol::link::encode_frame(frame);
}
```

当前 GET 接口支持 Normal/NormalList 及对应响应，未实现服务见支持矩阵。响应中的 AttributeResult.result 分别用 `uint8_t` 保存 DAR，或用 Data 保存真实数据；DAR 不会自动转换为 bool。

Frame.payload 为未扰码的链路用户数据，encode/decode 负责 SC 的变换和校验。单帧 decode 不接受 FE 前导或尾随多帧；流解析器负责前导、噪声和半帧。流事件分别为 Frame 和 Error。FrameStreamDecoder 是单线程状态对象，调用方在会话线程或 strand 中使用；不完整但具有正确头校验的帧，由调用方设置超时并 reset。

## TCP 通道

```cpp
#include <dlt698/transport/tcp.hpp>

auto runtime = std::make_shared<dlt698::transport::IoRuntime>();
auto channel = dlt698::transport::TcpChannel::connect(
    runtime, "127.0.0.1", 4059,
    [](dlt698::Result<void> result) { /* connection completion */ });
// Application pumps runtime->run() or run_for(...).
// Begin reads/writes after the successful connection completion.
```

`IoRuntime` 没有隐藏线程，应用负责泵送并管理线程结束。多个泵送线程时，每个通道/监听器通过独立 strand 串行处理其状态；不同通道的回调可以并发。对同一个 IChannel 的操作可从不同线程投递，回调在 runtime 中执行。回调抛出的异常被隔离，应用应在自己的回调中记录异常；不应在回调里阻塞等待依赖同一 runtime 的结果。

一个通道只允许一个在途 read，第二个返回 busy。一次 read 的完成是任意字节块，需要送入 FrameStreamDecoder。写操作保序并写完全部字节后完成；大小与条数预算涵盖已投递但尚未进入 strand 的写操作。连接/DNS/读写超时策略将在 Session 阶段统一实现，当前底层通道不会自动重试或重放操作。

`close()` 幂等并投递关闭操作；挂起 read/write/accept 以错误完成，正常关闭期间应继续泵送直到完成。对象销毁会释放 socket，操作不会通过内部强引用永久保活 runtime。停止 runtime 时尚未处理的回调，须 restart/run 才可能继续完成；彻底退出时先关闭通道、处理完成、再 stop，且在销毁 runtime 前结束调用 run 的外部线程。

连接方式与协议客户机/服务器角色没有绑定。TcpListener 接受 TCP 连接，不意味着它是 698 协议服务器。当前协议会话尚未实现，TCP 回环测试中的服务器应答由测试代码完成。

静态/共享 C++ 包的生产方和消费方应使用兼容编译器、标准库和运行库。MSVC 与 MinGW 库不可混用；共享库不承诺跨工具链 C++ ABI。Asio 类型只出现在实现文件，消费方不需要 Asio 头文件。
