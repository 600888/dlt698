# 当前 C++ API

2026-10-07 新增普通服务器入口：`app::Server::set/start_tcp/start_serial/stop`，不要求使用者创建执行器、目录、会话或接入回调。`service::Device` 支持严格标准值校验、运行中首次发布、同 OI 多属性扩充、自定义只读声明、元素更新和资源预算；多个服务器可共享同一设备。完整契约见[托管服务器与设备数据](../website/docs/session/server.md)。现有分层 API 保持默认行为。

客户端同步新增 `app::Client::connect_tcp/open_serial/get/set/action/disconnect`，复用已有同步服务与分块事务，自动等待登录并建立关联；拒绝码、DAR 和远端 ERROR 保留原码。完整契约见[托管客户端](../website/docs/session/client.md)。外部运行时与高层 provider 仍待后续阶段。

这是 0.x 开发接口，允许调整。公开头文件使用中文 Doxygen 注释，接口细节以头文件为准。

| CMake 目标 | 当前内容 |
| --- | --- |
| `dlt698::core` | 基础模型、Data/帧/APDU codec、ManualExecutor、IChannel、MemoryChannel、SerialLinkChannel |
| `dlt698::session` | Session，公开依赖 core |
| `dlt698::service` | ObjectRegistry、MemoryObject、ClientService、ServerService、SyncClientService，公开依赖 session |
| `dlt698::transport` | IoRuntime、TCP 通道/监听器、原始 SerialChannel；仅在启用传输构建时提供 |
| `dlt698::app` | Server/Client 托管 TCP/串口、关联、线程、数据发布和同步请求；仅在启用传输构建时提供 |
| `dlt698::dlt698` | 聚合 core/session/service，以及启用时的 transport/app |

`DLT698_BUILD_TRANSPORT=OFF` 不会移除内存通道、会话或对象服务。静态/共享目标均有安装导出；MSVC 的 `/utf-8` 编译选项会传递给安装包调用方。生产方和消费方须使用兼容编译器、标准库和运行库，MSVC 与 MinGW 库不可混用；共享库不承诺跨工具链 C++ ABI。Asio 类型只出现在实现文件，消费方不需要 Asio 头文件。

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

公开完整输入编解码函数返回 `Result<T>`，检查成功后访问 `value()`，失败后访问 `error()`。错误包含类型、偏移和字段上下文；远端 ERROR-Response 转成 `remote_error`，原码保存在 `Error.remote_code`。CONNECT 的远端拒绝保留在响应 `result` 中，GET 的每项 DAR 保留为 `uint8_t`，两者都不能只用外层 Result 判断业务成功。Reader/Writer 及组合解析函数可能抛出 DecodeFailure；完整输入 codec 负责转换。

ByteView 借用内存，禁止从临时 Bytes 构建。codec 在调用结束前完成读取，返回拥有内存的值；异步写接管 Bytes，读完成返回拥有内存的 Bytes。

## APDU 和帧

```cpp
using namespace dlt698;
protocol::apdu::GetRequest request{1, false, {{0x4001, 2, 0}}, {}};
auto apdu = protocol::apdu::encode_apdu(protocol::apdu::Apdu{request});
if (apdu) {
    protocol::link::Frame frame;
    frame.server.bytes = {0x07, 0x09, 0x19, 0x05, 0x16, 0x20};
    frame.payload = std::move(apdu).value();
    auto wire = protocol::link::encode_frame(frame);
}
```

`decode_apdu/encode_apdu` 使用统一 Apdu 变体，当前包含 LINK 登录/心跳/退出与响应、CONNECT 请求/响应、RELEASE 请求/响应/通知、双向 ERROR 和 GET/SET/ACTION 普通、列表形式。也可单独使用 connection、get、mutation 编解码接口。SET 响应直接保存 DAR，ACTION 保存 DAR 与可选 Data，区分无数据与存在 NULL Data。完整输入 codec 拒绝尾随字节及超限输入，then-get 与其他未实现分支返回 unsupported_service。

CONNECT codec 支持 NullSecurity、PasswordSecurity、SymmetrySecurity、SignatureSecurity 及响应认证附加数据的线格式，不执行认证。协议一致性位图按最高位对应序号零；帧尺寸是长度域 L 的上限，不含起止字符，APDU 尺寸不含链路封装。TimeTag 可按原字段编解码，尚未验证日历、延时及有效期语义；非空 FollowReport 返回不支持。PIID-ACD 的 ACD 位可保存，但未实现相应上报流程。

Frame.payload 为未扰码的链路用户数据，encode/decode 负责 SC 的变换和校验。单帧 decode 不接受 FE 前导或尾随多帧；FrameStreamDecoder 处理前导、噪声、半帧和粘帧，输出 Frame/Error 事件。它是单线程状态对象，须在会话执行器或 strand 中使用；独立使用时，合法但不完整的帧需要调用方设置超时并 reset。

## 执行器与内存通道

IExecutor 的 post/schedule 必须延后、串行执行任务并隔离异常，不能在投递函数内直接调用用户任务。now 使用单调时钟；ITimer::cancel 幂等，释放计时器句柄本身不会取消回调。

实现 IExecutor 还须提供 `is_current()`：在本执行环境的任务中返回 true，共享 I/O 循环须覆盖循环内所有 strand 的回调。同步适配用它拒绝会阻塞自己的调用；ManualExecutor 只允许单线程使用，IoRuntime 的执行器支持跨线程投递。

ManualExecutor 仅限单线程使用。`run_ready()` 执行当前可运行任务，`advance(duration)` 推进虚拟时间并执行到期任务；适合确定性模拟，不创建线程也不跟随真实时间。

```cpp
auto executor = std::make_shared<dlt698::ManualExecutor>();
auto channels = dlt698::transport::MemoryChannel::pair(executor);
```

MemoryChannel 的两端共享执行器。单端只允许一个在途读取，每次返回非空字节块；写入成功表示整块已进入对端缓存，不代表远端处理完成。MemoryOptions 限制读取分块、每端缓存与排队写字节、排队写条数，空写也占条数；预算不足返回 resource_limit，不部分写入。关闭、销毁与异步完成仍需驱动执行器。

## 基础 Session

Session 绑定已连接的 IChannel、串行 IExecutor 和 SessionOptions。协议客户机/服务器角色由 Role 配置，与 TCP 拨号方向独立。地址只接受精确单地址，并验证 SA、CA、DIR、PRM 和功能码；不支持组、通配、广播会话。

先注册服务器处理器并调用 `start()`，再由客户机 `async_connect()`。Session 只发起并接受公共 NullSecurity 连接；非公共认证请求被拒绝。声明应用连接、GET 普通/列表/记录/Next、SET/ACTION 普通及列表和链路分帧能力，默认协议位图前三字节 F3 8C 08；功能位图为零，窗口固定为 1。SessionOptions 可缩减能力，协商取交集。未注册 GET 处理器返回 DAR=4，未注册 SET/ACTION 处理器逐项返回拒绝 DAR=3。

`require_login=true` 要求先由协议服务器调用 `async_link(login, ...)` 完成预连接；async_link 也支持单次心跳及退出，async_link 的 heartbeat_seconds 为单次线上声明；SessionOptions.heartbeat_seconds 非零时自动周期心跳，超时关闭通道。`preset_association=true` 显式跳过 CONNECT，按本地配置进入应用连接，调用方负责两端一致性；它不表示远端经过认证。

| 操作/情况 | 当前行为 |
| --- | --- |
| `async_get(attributes, list, handler)` | 已关联客户机发起 Normal/NormalList；检查响应分支、数量及每个 OAD 的原顺序 |
| `async_set(attributes, list, handler)` | 设置属性与精确 Data，响应保持逐项 DAR；不自动回滚或重放 |
| `async_action(methods, list, handler)` | 调用完整 OMD 与参数 Data，检查模式字节、列表顺序与响应分支 |
| 第二个在途请求 | 返回 busy，不进入请求队列 |
| 错地址、方向、服务、PIID 或 OAD/重复响应 | 进入诊断回调，不抢占正确事务 |
| 请求超时或 `cancel()` | 结束事务并关闭物理通道，隔离迟到响应 |
| 成功完成或被释放打断 | PIID 进入 id_reuse_delay 隔离期；64 个序号暂时不可用时返回 resource_limit |
| `async_release()` | 先取消在途事务，再释放应用连接；成功后物理通道可再次 CONNECT |
| 协商空闲时限到期 | 退出应用连接；服务器发送 RELEASE-Notification |
| `close()`/析构 | 关闭通道及计时器，未完成请求以 closed 完成；仍须驱动执行器交付回调 |

默认 request_timeout 为 5 秒、id_reuse_delay 为 120 秒，后者须由应用配置为覆盖对端最大响应寿命且不短于请求超时；线上没有 generation 字段。state() 读取原子发布状态，刚投递的操作尚未立即生效。公共请求与完成回调都在会话执行器内处理，用户回调异常被隔离；服务器同步 GET 处理器和 provider 不得阻塞等待同一执行器。calendar_clock 默认空时使用 UTC；注入函数抛异常会以 invalid_value 关闭会话，不发送伪造时间。

SessionOptions.request_time_tag 可为客户机请求自动添加时间标签；服务器验证具体日历与允许延时，过期请求不调用 provider，响应原样回传，客户机严格匹配回显。Release 通知的标签也执行有效性检查。Session 对象需要由应用持有；排队任务不会永久保活它。正常退出时保留执行器并继续驱动直到挂起回调完成，然后结束运行线程及运行时。

## 对象读写与方法服务

ObjectRegistry 按 OI 注册 ObjectSchema 和共享 IObjectProvider，属性编号为互不重复的 1 至 31；属性零的整个对象读取未实现。目录先检查权限，在释放目录锁后调用 provider，再校验完整属性的 Data 类型。DAR 原码为 3（拒绝访问）、4（未定义）、7（类型不匹配）、255（provider 异常）；非零索引的元素类型及属性特征语义由 provider 保证，其实现还须满足并发读取约定。

MemoryObject 是线程安全的本地模拟 provider。`set(attribute, Data)` 设置属性完整值，不是协议 SET；它仅支持属性特征零，索引零读取完整值，非零从 1 开始读取 array/structure 的一级元素，非法索引返回 DAR=8。不隐含单位、倍率或真实计量能力，完整标准对象目录尚未提供。

AttributeSchema 的 writable 默认 false，已有只读 schema 不会自动获得写权限。ObjectRegistry::write 检查权限和完整属性类型；非零索引的元素类型由 provider 保证。MemoryObject::write 只替换已有完整值或一级元素且保持原类型，失败返回 DAR=4/7/8；本地 set 方法仍用于初始化配置。

ObjectSchema.methods 用 MethodSchema 声明非零方法编号、可选参数/返回类型及执行权限。IObjectProvider::write/invoke 默认拒绝；目录在释放锁后调用 provider，并把异常转成 DAR=255。MemoryObject::bind_method 支持模式零的方法回调，复制回调后释放锁，允许重入本对象；应用回调须满足跨会话并发约定。

ClientService 的 async_get/set/action 返回单项 ObjectValue、DAR、ActionValue；对应 list 方法返回逐项完整响应。ServerService 接入 GET、记录、SET、ACTION 四类处理器，持有共享目录，不捕获自身地址。列表按顺序独立执行，部分成功不回滚；超时/取消只说明本地没有得到确定响应，不能证明远端没有执行 SET/ACTION，因此不会自动重试。

[service/memory_get.cpp](../cpp/examples/service/memory_get.cpp) 给出了完整的注册对象、启动两端、CONNECT、读取数据与未知对象、RELEASE 和关闭示例。构建后运行 `dlt698_memory_get` 即可观察精确 Data 字节和 DAR=4。

## 同步客户机

`SyncClientService(session, drive = {})` 提供 connect、get/get_list、get_record/get_record_list、set/set_list、action/action_list、release，在异步核心上等待，不创建线程。应用先启动 Session；不传 drive 时必须由其他线程持续驱动 IoRuntime。传 drive 时调用线程反复调用驱动函数，例如 runtime->run_for(duration) 或 ManualExecutor::advance(duration)，让虚拟时钟也能产生超时。

同一适配器同时第二个调用、同一事件循环回调中的同步调用返回 busy；异步会话仍遵循单在途规则。drive 抛异常返回 io_error 并投递取消，后续完成状态由共享对象持有，不引用已经返回的栈。等待超时由 Session 管理，应用须保持运行时进展，销毁适配器前先结束同步调用。[memory_mutation.cpp](../cpp/examples/memory_mutation.cpp) 演示完整同步读写/方法与串行链路适配。

## TCP 通道

```cpp
#include <dlt698/transport/tcp.hpp>

auto runtime = std::make_shared<dlt698::transport::IoRuntime>();
auto executor = runtime->executor();
auto channel = dlt698::transport::TcpChannel::connect(
    runtime, "127.0.0.1", 4059,
    [](dlt698::Result<void> result) { /* 连接成功后创建并启动 Session */ });
```

IoRuntime 没有隐藏线程，应用负责 run/run_for 并管理线程结束。executor() 返回独立 strand 上的会话执行器及真实单调计时器，须由同一 runtime 驱动；可以从不同线程投递。多个泵送线程时，每个通道/监听器及会话执行器分别串行处理自身状态，不同实例的回调可以并发。

一个通道只允许一个在途 read，第二个返回 busy；一次完成是任意字节块，需要送入流解析器。写操作保序并写完全部字节后完成；大小与条数预算涵盖已投递但尚未进入 strand 的写操作。底层通道不会自动重试、重放或设置 DNS/建连/读写超时；Session 的请求超时不等同于建连超时。

close() 幂等并投递关闭操作，挂起 read/write/accept 以错误完成。停止 runtime 不会关闭通道，未处理回调须 restart/run 才可能继续完成；彻底退出时先关闭通道和监听器、处理完成，再 stop，且在销毁 runtime 前结束调用 run 的外部线程。回调抛出的异常被隔离，应由应用记录；不得在回调中阻塞等待同一 runtime 的结果。

TcpListener 接受 TCP 连接不限定协议角色。两种拨号方向均测试 LINK、CONNECT、GET/列表 GET、同步 SET/ACTION、RELEASE；同步等待同时验证显式 run_for 和应用运行线程两种方式。

## 串口与串行链路

`SerialChannel::open(runtime, device, options)` 同步打开并配置原始串口，默认 9600/8E1；可选数据位、校验、停止位、流控与 ChannelOptions。Windows COMn 自动使用设备命名空间兼容高编号，POSIX 使用设备路径。它继承 IChannel，读写保序、单在途读、投递前预算及关闭语义与 TCP 相同；写完成表示全部交给驱动，尚不能证明物理线路排空。

```cpp
#include <dlt698/transport/serial.hpp>
#include <dlt698/transport/serial_link.hpp>

auto runtime = std::make_shared<dlt698::transport::IoRuntime>();
auto executor = runtime->executor();
auto raw = dlt698::transport::SerialChannel::open(runtime, "COM3");
std::shared_ptr<dlt698::session::Session> session;
if (raw) {
    auto channel = dlt698::transport::SerialLinkChannel::wrap(raw.value(), executor);
    session = std::make_shared<dlt698::session::Session>(channel, executor);
    session->start();
}
// 应用保留 session，继续发起连接并驱动 runtime。
```

SerialLinkChannel 只接受恰好一个含校验的完整帧，添加四个 FE，再按真实排空或软件估算输出时间保留至少 33 位间隔；最近一次接收后开始发送也延后 33 位。原始读取保留 FE，由 FrameStreamDecoder 处理。SerialLinkOptions 的波特率和每字符位数须与端口一致，非整数停止位按向上取整配置位数；队列预算包括 FE。

手动 RS-485 切换配置幂等 set_transmit 和 async_drain；后者须由应用驱动确认最后停止位已经发出，可从其他线程完成。没有真实排空接口时禁止手动方向配置；仅用估算时不能保证 USB 缓冲、硬件流控或特定适配器时序。带流控或需严格排空的设备应接入 async_drain。关闭时取消队列并尽力恢复接收方向；驱动排空失败不重放数据。

当前证据为虚拟时间/内存链路模拟及本机串口参数、打开失败路径，真实串口收发与 RS-485 硬件互操作未验证。

## 记录与分段

`ClientService::async_get_record/async_get_record_list` 和同步对应方法接收 GetRecord（OAD、RSD、RCSD），返回精确的 Data/DAR 表格。AttributeSchema.record 必须显式为 true，ObjectRegistry 检查读权限后在目录锁外调用 IObjectProvider::read_record；MemoryObject::bind_record 提供模拟回调。响应表头、列顺序与行宽必须一致，空请求 RCSD 表示全选，provider 返回实际表头。业务选择条件由 provider 解释，库不猜测表计档案或数据库的排序规则。

全部 RSD 0–10、MS 0–7 和 CSD/ROAD 已建模。带标签 Data 使用 `Data{RecordData{Rsd{Selector9{1}}}}` 等形式；`data.as<RecordData>().as<Rsd>()` 取得常量内容。RecordData 复制共享不可变节点，构造时复制/移动完整值，之后修改原选择器不会改变快照；节点中的 Data 仍遵守全树节点和深度预算。

Session 自动处理链路分帧及 GET Next，两类状态机也可独立使用。每个 GET 查询先生成拥有内存的快照，后续 GetRequestNext 不再次读取 provider；完整属性/记录行为不可分割单元，重复 OAD 查询保留整个记录结果边界。超过单帧目标可继续链路分帧，超过 APDU 硬上限返回异常。客户机合并相同 OAD/RCSD 的连续记录行，累计行列乘积和实际传输字节受限。单会话仅一份服务端快照、一个在途客户机事务、一个发送与一个接收重组过程；新 GET、释放、超时或关闭回收快照。跨记录/跨查询的一致性由应用后端保证。

第三阶段标准记录模板、MemoryRecords 模拟后端、CONNECT 能力提示/读取计划及显式逐项探测见[标准记录与能力筛选](../website/docs/protocol/standard-records.md)。功能位默认零，应用可明确配置实际支持位，协商交集；全零按未知处理，业务提示不证明 OAD 存在。

详见 [M4/M5 资源配置与示例](m4-m5.md)，其中列出分段序号、重复/乱序处理、生命周期及命令行用法。
