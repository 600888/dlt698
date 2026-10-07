# Python 与 C++ 功能差异审计

2026-10-07。对照仓库 C++ 公开头文件、Python 实际绑定、包装层与测试核实。
本次先完成目录分层和 AsyncServer；以下是后续待补项，未在本次顺带实现。
“未绑定”不等于底层协议不支持；原生 Session 已自动处理的能力单独说明。

## 本次完成

- Python 包与绑定按 app/common/codec/model/protocol/security/service/session/standard/transport 分目录。
  旧的根入口和模块导入保留兼容，生成工具与构建路径同步更新。
- AsyncServer 直接复用 C++ app::Server 的 TCP/串口、共享 Device 和多会话处理。
  C++ ServerRunner 执行启停等待并发布完成结果；Python 只通过 asyncio Future 等待，
  不创建线程或使用线程池。支持关闭、停止后重启、启动取消收尾和 loop 线程事件分发。

## 已确认的缺口

| 优先级 | 缺口 | C++ 已有能力和代码依据 | Python 现状与影响 |
| --- | --- | --- | --- |
| 高 | 异步客户端串口 | [app/client.hpp](../cpp/include/dlt698/app/client.hpp)、SerialChannel、SerialLinkChannel | AsyncClient 仅有 connect_tcp，Engine 也仅开放 TCP；同步 Client 支持简单与完整参数串口。本次 AsyncServer 已支持异步串口启停，但尚未补客户端对称接口。 |
| 高 | 协商能力判断、读取规划、候选点探测 | [standard/capabilities.hpp](../cpp/include/dlt698/standard/capabilities.hpp) 的 Capabilities、service_support、point_support、candidate_points、plan_reads、require_record_service；[service/point_probe.hpp](../cpp/include/dlt698/service/point_probe.hpp) 的 async_probe_points、ProbeOptions、PointResult | 上述类型和函数均未绑定。Python 需手工解释 CONNECT 位图、安排批次、处理不支持 List 时的 Normal 回退与逐项校验；现有 GET 并不能替代完整探测流程。 |
| 中 | 记录独立校验工具 | [standard/records.hpp](../cpp/include/dlt698/standard/records.hpp) 的 validate_record_time、validate_record_query、validate_record_cell、validate_record_result | Python 暴露了记录模板和查询构造器，缺少这四个直接校验入口。原生服务处理仍有校验；缺口影响应用离线校验自定义记录、保留错误分类。 |
| 中 | 注入协议日历时钟 | [session/session.hpp](../cpp/include/dlt698/session/session.hpp) 的 SessionOptions.calendar_clock | 绑定 options 未开放该字段，Python 不能配置统一业务时钟或确定性时钟。默认 UTC 和已有时间标签能力仍可使用。 |
| 中 | 协议角色与 TCP 拨号方向独立配置 | [session/session.hpp](../cpp/include/dlt698/session/session.hpp) 明确二者独立；[绑定 Engine](bindings/session/engine.cpp) 的 connect_tcp/listen | Engine 在 connect_tcp 中强制 role=client，在 listen 中强制 role=server。不能通过 Python 专家入口实现“协议服务器主动拨号”或“协议客户端接收入站连接”。C++ 的底层通道与 Session 可以自行组合；C++ 高层 app 入口也有场景限制。 |
| 中 | 专家会话显式 LINK/RELEASE、收发观察 | [session/session.hpp](../cpp/include/dlt698/session/session.hpp) 的 async_link、async_release、set_traffic_handler | Engine 的自动登录与 close/cancel 可用，但没有显式 LINK/RELEASE 操作和原始 traffic 完成通知。同步 Client.disconnect 会执行原生释放；AsyncClient.close 直接关闭 Engine，缺少异步优雅释放接口。普通 Client/Server/AsyncServer 已有收发事件，不能据此认为所有入口都缺少观测。 |
| 低 | MemoryObject 方法和记录绑定便利接口 | [service/object.hpp](../cpp/include/dlt698/service/object.hpp) 的 MemoryObject.bind_method、bind_record | Python MemoryObject 仅额外绑定 set；仍可自定义 ObjectProvider.invoke/read_record，或使用 MemoryRecords。属于现成便利接口缺失，ACTION/记录协议本身已经支持。 |
| 低 | 低层通道、执行器和手动 RS-485 hooks | [transport/channel.hpp](../cpp/include/dlt698/transport/channel.hpp)、[common/executor.hpp](../cpp/include/dlt698/common/executor.hpp)、[transport/serial_link.hpp](../cpp/include/dlt698/transport/serial_link.hpp) | Python 未开放自定义 IChannel/IExecutor、MemoryChannel、SerialLinkOptions.set_transmit/async_drain。自动方向串口与帧间预算已可用；需要真实方向控制和驱动排空的硬件接入无法直接配置。 |
| 低 | 独立链路分片/重组工具 | [protocol/link/fragment.hpp](../cpp/include/dlt698/protocol/link/fragment.hpp) 的 Fragment、LinkFragmenter、LinkReassembler、encode_fragment/decode_fragment；[protocol/apdu/get_block.hpp](../cpp/include/dlt698/protocol/apdu/get_block.hpp) 的 GetBlockTransfer | Python 未绑定独立工具。原生 Session 已处理链路分片及 GET 分块，普通大报文通信和记录分块并未因此缺失；影响脱离 Session 的离线分析、工具开发和自定义链路调试。 |

## 高层服务范围与验证限制

AsyncServer 复用的是 C++ 高层 app::Server，所以与同步 Server 一样以共享 Device 数据发布为主。
动态 provider、高级 PROXY/REPORT、透明桥和 Python 安全后端已有 Engine/Endpoint 专家路径，
需由应用持续 poll；尚无统一 asyncio 专家服务端入口。不能把这些能力都标成“未实现”，
也不能把 AsyncServer 当成所有专家扩展点的自动封装。

所有业务 I/O 及新增启停等待线程均在 C++ 创建。Python 事件回调仍在所属事件循环执行，
耗时回调会阻塞 loop；这不代表 Python 语言只能创建一个线程。

真实串口、RS-485 和 ESAM 仍需硬件验收。测试中的串口失败收尾、TCP 回环及安全 mock
不能替代硬件验证；非 CPython、free-threaded、子解释器与发布平台矩阵是兼容范围限制，
不应与上述 C++ API 绑定缺口混为一谈。

建议下一批先补 AsyncClient 串口，再开放能力判断/读取规划/点位探测，随后补时钟和记录校验。
