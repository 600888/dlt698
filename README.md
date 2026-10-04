# dlt698
从零构建的现代 C++17 DL/T 698.45 协议库，参考 dlt645 的分层架构，面向集中器、采集终端和电力网关等协议对接场景。

当前已完成 M4 软件功能与 M5 两类分段、记录查询：周期心跳、TimeTag、同步/异步服务、TCP/串口适配、GET Next 和完整记录选择器，尚未覆盖完整协议。实现基线为 `plan/dlt69845-2017.pdf` 中的 DL/T 698.45—2017。

[整体实现计划](plan/implementation-plan.md) 包含分层架构、标准覆盖范围、阶段任务、验收条件，以及后续 pybind11 绑定和 PyPI 发布方案。

实施顺序：C++ 基础模型与 A-XDR → 链路层 → APDU → 通信与会话 → 分段、记录、上报与代理 → 对象与安全完善 → C++ 稳定 → Python 绑定 → wheel/PyPI 发布。

当前包含精确类型 Data、A-XDR、OI/OAD/OMD/TI/TSA、ROAD/Region/RSD/MS/CSD/RCSD、HCS/FCS、变长地址、扰码、流解析，以及 LINK/CONNECT/RELEASE/ERROR、GET 普通/列表/记录/Next 和 SET/ACTION 普通、列表编解码。Session 处理协商、单在途、PIID/OAD/OMD 匹配与隔离、超时、取消和释放；ObjectRegistry、MemoryObject 与 ClientService/ServerService 提供读写和方法分发，SyncClientService 在统一异步核心上同步等待。TCP/原始串口使用 standalone Asio 1.38.2；SerialLinkChannel 添加 FE 前导、串行间隔及可选 RS-485 驱动 hooks。`third/spdlog` 暂未接入。

[在线接口文档](https://600888.github.io/dlt698/) · [支持矩阵与验证范围](docs/protocol-coverage.md) · [当前 C++ API](docs/cpp-api.md) · [标准测试向量](tests/vectors/README.md)

在线文档位于 `website/`，使用 Rspress 2 构建，覆盖全部对外 C++ 接口：

在仓库根目录构建与测试：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix ./build/stage
```

关闭 Asio TCP/串口构建可加 `-DDLT698_BUILD_TRANSPORT=OFF`，内存通道、SerialLinkChannel、会话和同步/异步服务仍可用；共享库可加 `-DBUILD_SHARED_LIBS=ON`；严格警告检查可加 `-DDLT698_WARNINGS_AS_ERRORS=ON`。也支持 `cmake -S cpp -B build-core`。C++17、CMake 3.20+ 和匹配的 C++ 编译器为必要条件，当前构建不需要 Python。

安装后的调用方：

```cmake
find_package(dlt698 0.1 CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE dlt698::dlt698)
```

只使用编解码、虚拟执行器和内存通道时链接 `dlt698::core`；会话链接 `dlt698::session`，对象服务链接 `dlt698::service`，TCP 组件链接 `dlt698::transport`。各目标自动传递其依赖。Windows 共享库运行时需将安装目录的 DLL 放在程序旁边或加入 PATH。

解帧示例程序位于构建目录 `bin/`（Visual Studio 多配置构建为 `bin/Release/`），可使用固定测试帧：

```sh
dlt698_decode "68 17 00 43 05 07 09 19 05 16 20 00 15 60 05 01 01 40 01 02 00 00 C6 07 16"
```

`dlt698_memory_get` 示例演示内存通道上的 CONNECT → GET NormalList（数据与 DAR 部分成功）→ RELEASE，无需网络或设备；源码见 [memory_get.cpp](cpp/examples/memory_get.cpp)。TCP 集成测试同时覆盖主站拨号和终端拨号，连接发起方与协议角色分别配置。

`dlt698_memory_mutation` 演示经过 FE/串行间隔适配的内存链路：同步 CONNECT → SET → ACTION → GET → RELEASE，输出 `SET DAR=0 ACTION DAR=0 GET UInt16=42`；见 [memory_mutation.cpp](cpp/examples/memory_mutation.cpp)。原始串口与驱动接入方式见 API 文档，真实串口及 RS-485 硬件时序尚未验证。

当前 Session 只开放公共认证；其他认证机制仅支持报文编解码。then-get、ACD/FollowReport、上报、代理、安全后端、完整标准对象目录及 Python 绑定仍待实现。记录选择器的具体采集/数据库语义由应用 provider 实现。真实串口收发与 RS-485 硬件时序尚未验证。

启用传输模块后生成独立的 `dlt698_master`、`dlt698_terminal`，支持 TCP 双向拨号与串口。两个终端分别运行：

```sh
dlt698_terminal tcp-listen 127.0.0.1 6980 60
dlt698_master tcp-connect 127.0.0.1 6980 get
```

主站支持 `get`、`set 25`、`action 25`、`record`；终端提供 OI=2000 的模拟值/方法/记录。完整用法、资源预算及设备验收范围见 [M4/M5 使用说明](docs/m4-m5.md)。

## 文档站点

`website/` 是 Rspress 2 文档站点，样式与配置参照个人博客，内容为本库的对外用户接口文档。

```sh
cd website
npm ci
npm run dev      # 本地预览
npm run build    # 构建到 doc_build/
```

需要 Node.js 22.12 或以上。构建前会运行 `npm run typecheck`、`npm run check:sidebar`（检查侧边栏链接唯一且目标页面存在）和 `npm run test:sidebar`。

推送 `main` 后 GitHub Actions 构建并发布到 `gh-pages` 分支，访问 <https://600888.github.io/dlt698/>。首次部署需在仓库 **Settings → Pages** 中选择 **Deploy from a branch** 的 `gh-pages / (root)`。
