# dlt698

从零构建的现代 C++17 DL/T 698.45 协议库，面向集中器、采集终端和电力网关等协议对接场景。

**在线接口文档：<https://600888.github.io/dlt698/>**

实现基线：DL/T 698.45—2017。

## 能力一览

### APDU 服务

| 服务 | 编解码 | 会话 / 服务层 | 备注 |
| --- | :---: | :---: | --- |
| LINK | ✅ | ✅ | 自动周期心跳、TimeTag |
| CONNECT | ✅ | ✅ | 仅公共认证机制 |
| RELEASE | ✅ | ✅ | 含 Notification |
| GET Normal / NormalList | ✅ | ✅ | 逐项 Data / DAR |
| GET Record / RecordList | ✅ | ✅ | 完整记录选择器 |
| GET Next | ✅ | ✅ | 按行拆分收齐 |
| SET Normal / NormalList | ✅ | ✅ | 精确 OAD 匹配 |
| ACTION 普通 / 列表 | ✅ | ✅ | 完整 OMD 模式匹配 |
| ERROR | ✅ | ✅ | 远端原码与 TimeTag 回显 |
| REPORT | ❌ | ❌ | |
| PROXY | ❌ | ❌ | |
| SECURITY | ❌ | ❌ | |
| FollowReport / ACD | ❌ | ❌ | |
| ThenGetNormalList | ❌ | ❌ | |
| MD5 | ❌ | ❌ | |

### 编解码与链路

| 能力 | 状态 |
| --- | :---: |
| A-XDR 长度（短 / 长形式） | ✅ |
| 单帧长度、控制域、HCS / FCS | ✅ |
| 扰码 SC（单帧与分帧头/片段 ±33H） | ✅ |
| 流解析（半帧、粘帧、噪声、校验失败恢复） | ✅ |
| 链路分帧与逐帧确认（12 位回绕、有界重组、有限重发） | ✅ |
| 变长 SA / 逻辑地址 / CA | ✅ |
| OI / OAD / OMD / TI / TSA | ✅ |
| ROAD / Region / RSD / MS / CSD / RCSD | ✅ |
| Data 标签 0–96（35 类） | ✅ |
| Data 标签 MAC / RN / SID / SID_MAC / COMDCB | ❌ |
| 广播地址的会话行为（无需应答） | ❌ |

### 传输与运行时

| 能力 | 状态 |
| --- | :---: |
| TCP 客户端 / 监听 / 双向拨号 | ✅ |
| 原始串口（速率、字格式、流控、串行队列） | ✅ |
| 串行时序适配（FE 前导、33 位间隔、RS-485 方向 hooks） | ✅ |
| 内存通道 MemoryChannel | ✅ |
| 异步执行器 ManualExecutor / IoRuntime | ✅ |
| 同步服务 SyncClientService | ✅ |
| 单在途事务、PIID / OAD / OMD 匹配与隔离 | ✅ |
| 自动重拨号 / 自动重登录 | ❌ 由应用负责 |
| 真实串口与 RS-485 硬件时序 | ⚠️ 未验证 |

### 对象与数据

| 能力 | 状态 |
| --- | :---: |
| ObjectRegistry / MemoryObject 读写与 schema | ✅ |
| ClientService / ServerService 方法分发 | ✅ |
| 118 个常用固定 OI（电能、需量、状态、谐波、参数） | ✅ |
| 记录模板 4 个入口 / 5 个记录列，共 127 个 OI | ✅ |
| 记录选择器 RSD 0–10、MS 0–7、CSD、Region 0–3 | ✅ |
| 有界行列筛选、不可变快照、能力筛选、点位探测 | ✅ |
| 非公共认证机制、SECURITY 封装 | ❌ |

完整矩阵与验证证据见 [支持矩阵与验证范围](docs/protocol-coverage.md)。

## 构建与测试

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix ./build/stage
```

| 选项 | 说明 |
| --- | --- |
| `-DDLT698_BUILD_TRANSPORT=OFF` | 关闭 Asio TCP/串口；内存通道、SerialLinkChannel、会话、同步/异步服务仍可用 |
| `-DBUILD_SHARED_LIBS=ON` | 构建共享库 |
| `-DDLT698_WARNINGS_AS_ERRORS=ON` | 严格警告检查 |

也支持 `cmake -S cpp -B build-core` 只构建核心。需要 C++17、CMake 3.20+ 和匹配的 C++ 编译器，不依赖 Python。

## 集成

```cmake
find_package(dlt698 0.1 CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE dlt698::dlt698)
```

| 目标 | 用途 |
| --- | --- |
| `dlt698::core` | 编解码、执行器、内存通道、SerialLinkChannel |
| `dlt698::session` | 会话、同步 / 异步服务 |
| `dlt698::service` | 对象服务与 provider 分发 |
| `dlt698::transport` | TCP 与原始串口（需开启传输构建） |

各目标自动传递其依赖。Windows 共享库运行时需将安装目录的 DLL 放在程序旁边或加入 PATH。

## 示例程序

示例位于构建目录 `bin/`（Visual Studio 多配置构建为 `bin/Release/`）。

| 程序 | 说明 |
| --- | --- |
| `dlt698_decode` | 十六进制帧解码，可用固定测试帧验证 |
| `dlt698_memory_get` | 内存通道 CONNECT → GET NormalList（部分成功）→ RELEASE |
| `dlt698_memory_mutation` | 同步 CONNECT → SET → ACTION → GET → RELEASE，输出 `SET DAR=0 ACTION DAR=0 GET UInt16=42` |
| `dlt698_standard_points` | 常用固定 OI 的内存通信示例 |
| `dlt698_standard_points_extended` | 分相电能、带发生时间的需量、谐波、状态字与参数 |
| `dlt698_standard_records` | 记录模板、有界行列筛选、GET Next 按行收齐 |
| `dlt698_master` / `dlt698_terminal` | TCP 双向拨号与串口，支持 `get`、`set 25`、`action 25`、`record` |

```sh
dlt698_decode "68 17 00 43 05 07 09 19 05 16 20 00 15 60 05 01 01 40 01 02 00 00 C6 07 16"

dlt698_terminal tcp-listen 127.0.0.1 6980 60
dlt698_master tcp-connect 127.0.0.1 6980 get
```

源码：[memory_get.cpp](cpp/examples/memory_get.cpp)、[memory_mutation.cpp](cpp/examples/memory_mutation.cpp)。完整用法与资源预算见 [使用说明](docs/m4-m5.md)、[C++ API 摘要](docs/cpp-api.md)、[标准测试向量](tests/vectors/README.md)。

## 已知限制

- Session 只接受公共认证，其他机制仅有线格式编解码。
- 记录选择器的采集与数据库语义由应用 provider 实现。
- 原始串口与 RS-485 硬件时序需实测验证；手动方向切换必须提供真实排空驱动。
- Data 标签 86、87、93、94、95 尚未实现，遇到时返回 `unsupported_tag`。