---
title: TCP 手动报文调试
description: 调试助手连接后收到 LINK 登录和 5 秒断开的原因、连接模式选择与直接 GET 示例。
---

# TCP 手动报文调试

使用串口调试助手的 TCP 客户端模式、网络调试助手或自写 socket 程序连接示例服务器时，可能一连接就收到 `68 ... 81 ... 01 ... 16`，随后约 5 秒断开。**当前 TCP 示例默认使用远程协议场景：服务器主动发送 LINK 登录请求，等待客户端确认；普通调试助手通常不会自动应答，登录事务超时后服务器关闭该 TCP 连接。**

如果目的是手动发送 GET 验证模拟数据，使用下面的 `local_preset` 示例。若要验证完整远程协议流程，使用配套客户端，或在自己的工具中实现 LINK 和 CONNECT。

## TCP 连接、LINK 和 CONNECT 的区别

TCP 建连只建立字节流通道，不代表 DL/T 698.45 会话已经就绪。LINK 登录由**协议服务器**发起，协议客户端确认；CONNECT 由**协议客户端**发起，协商应用连接。谁主动拨号与协议角色独立，监听端也可以是协议服务器并主动发送 LINK。

公共 CONNECT 不使用密码或加密认证。“等待 LINK 登录”与用户账号登录不同，也不表示普通 GET 一定需要密码。

按 DL/T 698.45—2017 第 6.1.1.2 节，本地 RS-485、红外通道默认具有预连接通道，无需额外 LINK 管理；第 6.1.1.3.1、6.1.1.3.3 节规定，预连接通道具有最低权限的预建立应用连接窗口，在该窗口中无需 CONNECT 即可访问其允许的内容。可访问范围由服务器定义，需要更高权限时仍须建立相应应用连接。

当前库将场景作为显式配置；`local_public` 仍要求 CONNECT，`local_preset` 可以跳过 LINK 和 CONNECT。预设模式使用配置的能力、尺寸及现有会话规则，并未完整实现标准预建立窗口与其他应用连接的权限切换，不能据此宣告所有对象均可直接访问。TCP 透传本地表计时，应按实际对端支持的场景选择，不能仅凭传输是 TCP 就认定对端一定会发 LINK。

## 三种模式怎么选

| `ConnectionProfile` | 协议流程 | 适用情况 |
| --- | --- | --- |
| `remote_public` | TCP → 服务器 LINK 登录 → 客户端确认 → 客户端 CONNECT → GET | 配套客户端、需要远程登录管理的对端；高层 TCP 默认值 |
| `local_public` | TCP/串口 → 客户端 CONNECT → GET | 不使用 LINK、但需要公共应用关联的对端；高层串口默认值 |
| `local_preset` | TCP/串口 → 直接 GET | 已明确预设关联的两端、手动验证本库模拟服务器 |

高层服务器和客户端使用同一个枚举；两端的关联策略和 SA/CA 必须匹配。使用普通调试助手时，需要自己完成对应协议流程，它不会因为 socket 已连接就自动完成关联。

```cpp
// 只做公共 CONNECT：不等待或发送 LINK。
auto started = server.start_tcp("0.0.0.0", 6980, dlt698::app::ConnectionProfile::local_public);

// 预设关联：可以直接发送 GET。
auto started = server.start_tcp("0.0.0.0", 6980, dlt698::app::ConnectionProfile::local_preset);
```

上面两种启动方式二选一；同一服务器实例不能重复启动。只改成 `local_public` 可以取消 LINK，但仍需 CONNECT，不能直接取得 GET 数据。

## 收到的登录报文是什么

以下是服务器发出的一个完整登录帧：

```text
68 1E 00 81 05 00 00 00 00 00 00 00 07 29 01 00 00 00 05 07 EA 0A 07 03 05 3A 0A 01 07 19 3B 16
```

| 字节 | 含义 |
| --- | --- |
| `68` / `16` | 起始符 / 结束符 |
| `1E 00` | 长度域 30，完整帧共 32 字节 |
| `81` | 服务器发出的链路管理请求控制域 |
| `05` + 六个 `00` | 单地址、逻辑地址 0、SA 长度 6；地址字节全零 |
| 后续 `00` | 客户端地址 CA=0 |
| `07 29` | HCS |
| `01 00 00 00 05 ...` | LINK APDU：服务 `01`、PIID-ACD `00`、登录类型 `00`、心跳周期 `00 05`（5 秒），随后是 10 字节请求时间 |
| `19 3B` | FCS |

请求时间由运行中的时钟生成，因此后续连接的时间字段和 FCS 会变化，不能只按这整串固定字节判断是否为登录报文。LINK APDU 也不使用普通 GET 的 TimeTag 尾部格式，详见[连接管理 APDU](../protocol/connection.md)。

远程模式客户端需要返回 LINK Response：匹配 PIID、填写结果码，原样回传请求时间，并提供收到时间和应答时间，再重新计算帧长度、HCS/FCS。发送 GET、回显登录请求或复制其他连接的旧应答，都不能完成本次登录。

## 为什么约 5 秒就断开

当前示例的过程是：

1. 接受 TCP 连接，创建服务器 Session。
2. 调用 `async_link(login, ...)`，发送登录帧并创建在途事务。
3. 在 `SessionOptions::request_timeout` 内等待匹配的 LINK Response，默认时限为 5000 毫秒。
4. 未收到合法应答，触发 `session transaction timeout`，关闭会话及 TCP 通道。

本机复现中，不发送应答的连接约在 5.015 秒关闭，底层 TCP 示例打印：

```text
Login: session transaction timeout
```

这个时间是一次软件复现结果，实际关闭时间会受调度影响，并非精确的 5.015 秒协议要求。关闭只影响该连接，托管 TCP 服务器继续监听其他客户端。

几个时间参数容易混淆：

| 参数 | 作用 |
| --- | --- |
| 服务器 `protocol.request_timeout`，默认 5 秒 | 等待本端发起事务的应答，包含主动 LINK 登录；超时关闭会话 |
| 服务器 `heartbeat_seconds`，默认 5 秒 | 登录完成后的心跳间隔；登录帧中的 `00 05` 声明该周期 |
| 高层客户端 `login_timeout`，默认 5 秒 | 等待远程服务器发起 LINK 的阶段时限 |
| 高层客户端 `transport_timeout`，默认 5 秒 | DNS/TCP 建连的合计时限 |
| `parameters.timeout_seconds`，默认 100 秒 | 应用关联的空闲超时；当前预设模式仍沿用现有会话超时处理 |
| TCP 示例的 `lifetime-seconds`，默认 60 秒 | 整个示例程序的运行时限，与单次登录等待分开 |

**将心跳周期设为 0 不会取消初始登录。** 高层 `remote_public` 仍会主动发送登录请求；底层显式调用的 `async_link()` 也仍会建立事务。只延长 `request_timeout` 会延后断开，不能让未完成的登录变成已完成。

## 可直接发送 GET 的完整服务器

以下程序发布频率 `200F/2/0=5000`（50.00 Hz），配置六字节全零 SA、逻辑地址 0 和 CA=0，以 `local_preset` 监听。它与下一节的固定 GET 报文匹配。需要开启 transport，包含 `<dlt698/app.hpp>` 并链接 `dlt698::app`。

```cpp
#include <dlt698/app.hpp>
#include <iostream>

int main() {
    dlt698::app::ServerOptions options;
    // 地址长度也是匹配条件；默认库地址 {0} 与六字节全零 SA 不同。
    options.protocol.server.bytes = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    options.protocol.server.logical = 0;
    options.protocol.client_address = 0;
    options.diagnostic = [](std::uint64_t id, const dlt698::Error& error) {
        std::cerr << "Connection " << id << ": " << error.context << '\n';
    };
    dlt698::app::Server server(options);
    auto published = server.set({0x200F, 2, 0}, dlt698::model::UInt16{5000});
    if (!published) {
        std::cerr << published.error().context << '\n';
        return 1;
    }
    // 显式预设关联，让调试助手无需实现 LINK/CONNECT 即可查询已发布值。
    auto started = server.start_tcp("0.0.0.0", 6980, dlt698::app::ConnectionProfile::local_preset);
    if (!started) {
        std::cerr << started.error().context << '\n';
        return 1;
    }
    std::cout << "Listening on TCP 6980. Press Enter to stop.\n";
    std::cin.get();
    auto stopped = server.stop();
    if (!stopped) {
        std::cerr << stopped.error().context << '\n';
        return 1;
    }
    return 0;
}
```

诊断回调在工作线程执行，不要在其中阻塞等待、同步读取或调用阻塞的 `stop()`；需要从回调停止时使用 `request_stop()`。

若使用本库高层客户端连接上述程序，也要显式选择相同模式，并配置六字节 SA：

```cpp
dlt698::app::ClientOptions options;
options.protocol.server.bytes = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
dlt698::app::Client client(options);
auto connected =
    client.connect_tcp("127.0.0.1", 6980, dlt698::app::ConnectionProfile::local_preset);
// 先检查 connected 成功，再调用 get；结果仍需区分 Data 和 DAR。
```

## 调试助手操作与 GET 报文

1. 启动上面的预设模式服务器，保持其进程运行。
2. 在调试助手中选择 **TCP 客户端**，连接 `127.0.0.1:6980`；跨机器时使用服务器实际 IP。
3. 选择**十六进制发送**，关闭自动附加换行，将下面报文作为二进制字节发送。不要以 ASCII 文本发送字符 `68 17 ...`。
4. 收到响应后检查 GET 结果中的 Data/DAR。此示例应返回频率 UInt16 `5000`，线上 Data 字节为 `12 13 88`。

读取频率 `200F/2/0`：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 20 0F 02 00 00 1C 0C 16
```

APDU 为 `05 01 00 20 0F 02 00 00`：GET Normal、PIID=0、OAD=`20 0F 02 00`、TimeTag 不存在。默认成功响应为：

```text
68 1C 00 C3 05 00 00 00 00 00 00 00 BC FF 85 01 00 20 0F 02 00 01 12 13 88 00 00 30 D7 16
```

响应 OAD 后的 `01` 表示 Data，`12` 是 UInt16 标签，`13 88` 是原始值 5000；最后两个 `00` 分别表示 FollowReport 和 TimeTag 不存在。修改 PIID、地址或数据后，应重新计算校验，不能继续使用原帧 CRC。

### 电能值与逻辑名不要混淆

以下报文合法，但读取的是 `0010/1/0`（逻辑名）：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 00 10 01 00 00 B1 F2 16
```

读取正向有功电能值应使用属性 2：

```text
68 17 00 43 05 00 00 00 00 00 00 00 72 79 05 01 00 00 10 02 00 00 D5 1D 16
```

`0010/2/0` 读取总量与分费率数组，`0010/2/1` 才是只读总量。上面的最小频率服务器没有发布电能，预设模式下对此会返回 DAR=4；如需电能数据，先按设备费率布局发布完整属性，再查询。现有 `dlt698_server` 发布电能属性 2，但不会自动补出属性 1 的逻辑名。

## 已有示例怎么调整

`dlt698_server` 与 `dlt698_tcp_server` 的 TCP 模式目前没有选择 profile 的命令行参数；要手动直接 GET，使用本页完整程序，或修改对应源码后重新构建。调试助手本身的设置不能改变服务器的关联策略。

高层示例 `cpp/examples/app/server.cpp`：在 TCP 的 `start_tcp(...)` 调用中加入 `ConnectionProfile::local_preset`。示例已配置六字节 SA，可继续使用其发布的 8 组属性。配套 `dlt698_client` 也要在 TCP 的 `connect_tcp(...)` 调用中选择同一模式。

底层示例 `cpp/examples/transport/tcp_server.cpp`：同时调整 Session 配置，并删除原来的主动登录调用：

```cpp
options.require_login = false;
options.preset_association = true;
options.heartbeat_seconds = 0;
// 保留会话启动和服务绑定；删除原来的 session->async_link(login, ...) 调用。
```

底层 `require_login` 只控制初始状态，**不会阻止应用显式调用 `async_link()`**。若保留该调用，仍会发送登录并因缺少应答超时。底层客户端改为预设模式时也应设置 `preset_association=true`，跳过 CONNECT；当前示例默认还有 CONNECT/RELEASE 流程，应同步调整，不能仅修改服务器后仍原样运行远程配套示例。

保留远程模式时，普通用户优先使用未修改的 `dlt698_server` / `dlt698_client` 配对，库会处理登录应答和 CONNECT；底层配对为 `dlt698_tcp_server` / `dlt698_tcp_client`。不要用 GET 代替 LINK Response。

## 排查“发送后没有数据”

| 现象或日志 | 检查方向 |
| --- | --- |
| 连接后收到 LINK，约 5 秒关闭；`session transaction timeout` | 远程模式未取得合法 LINK 应答；选择合适 profile 或实现应答流程 |
| 高层客户端 `LINK login timeout` | 客户端选了远程模式，但对端未发 LINK；核对对端是否使用本地/预设场景 |
| GET 收到 `EE ... 02 ...` 异常响应 | 当前普通 GET 分支在未关联或能力未允许时返回异常类型 2；先核对 LINK/CONNECT 或预设配置 |
| `session SA/CA` | SA 字节、地址长度、逻辑地址或 CA 与服务器配置不一致；帧在分发前被拒绝 |
| `session DIR` / `session PRM/function` | 控制域方向、启动标志或功能码不符 |
| HCS/FCS、长度、APDU 解码诊断 | 修改报文后校验未更新、发送不完整、发成了 ASCII、缺少 TimeTag 存在标记等 |
| 收到 GET 响应但 DAR=4 | 属性未发布/未定义；核对 OI、属性号与当前模拟目录 |
| 收到 GET 响应但 DAR=3 | 访问被拒绝；本地发布值不授予远端 SET 权限 |

当前服务器收到地址匹配、格式正确的普通 GET，但尚未关联时，会尝试返回异常响应；单纯“未登录”不能解释所有静默无应答。若完全收不到字节，先启用诊断并检查收发记录。接收可能半帧或多帧粘连，响应也可能分帧/分块，应按长度和校验解析，而不是把一次 TCP 接收固定当成一帧。

## 验证范围

本页登录帧已通过现有 `dlt698_decode` 的长度、HCS/FCS 检查。无应答登录的 5 秒关闭来自 Windows 本机 TCP 实测；预设模式 GET 示例的编译和收发验证使用安装导出的 `dlt698::app`。这些证据用于解释本库当前行为，真实电表的窗口权限、串口/RS-485 时序和厂家策略仍应按实际设备核对。
