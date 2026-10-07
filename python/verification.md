# Python 实施与验证记录

执行环境：Windows x64、CPython 3.11.6、MSVC 19.39、CMake 3.30.5；2026-10-07。

## 目录分层与异步服务端增量验证

本次按业务整理源码、原生绑定与测试目录；AsyncServer 复用 C++ app::Server，
启停等待线程由 C++ ServerRunner 管理，Python 不创建工作线程。

- 原生扩展重编译与 editable 安装通过；修复 editable 身份记录的定位。
- 完整 Python 回归与独立 C++/Python 双向互操作共 115 项通过。
  新增覆盖三种关联场景、多客户端、连接预算、启动失败重试、停止后重启、取消启动、
  取消关闭、跨 loop 拒绝、回调异常、回调内关闭及启动未完成时请求停止/关闭。
- 禁止 Python Thread.start/to_thread/run_in_executor 的测试下，服务器仍可启停和通信。
- 严格 mypy、Ruff lint/format、43 个公开头文件/API 契约和 stub 一致性检查通过。
- 155 个项目 C++ 源文件的 clang-format dry-run 通过，不处理 third 或构建目录。

异步服务端串口入口验证了不存在端口的错误传播与失败后资源收尾；成功收发仍需硬件。
功能差异及后续优先级见 [功能差异审计](feature-gaps.md)。下表保留此前验证记录。

| 项目 | 本地结果 |
| --- | --- |
| Python 实际 wheel 安装测试 | 98 项通过：规范向量、数据边界、普通/列表读写、provider、MD5/ThenGet、记录分块/投影、REPORT、PROXY、透明转发及迟到完成隔离、mock 安全、错误身份、GIL 并发取消与 asyncio task/loop 收尾 |
| 干净环境安装 | 无系统 site-packages 的新虚拟环境、空工作目录：全部公开模块导出、原生 TCP 读写及资源收尾通过；最终 wheel 再次通过全部 98 项测试 |
| 独立 C++ 动态根构建 | `build/traffic-msvc`：11/11 CTest 通过，含 TCP/app 和重定位安装消费方 |
| 独立 C++ 静态直接 cpp 构建 | `build/python-cpp-core` transport ON：11/11 通过，含重定位安装消费方 |
| 独立 C++ 无 transport 核心 | 同一直接 cpp 构建先以 transport OFF 配置：8/8 通过，含安装消费方；随后开启 transport 验证静态完整栈 |
| 双向跨语言互操作 | 独立 SDK 消费方 `build/python-interop/Release/dlt698_interop.exe`：Python client→C++ server、C++ client→Python server 通过 |
| 公开契约 | 43 个头文件、1298 个公开 AST 声明；对应清单及原生导出核对通过 |
| 类型、风格 | 严格 mypy、Ruff lint/format、项目 C++ clang-format dry-run 通过；不处理 third/build |
| sdist | 实际白名单压缩包检查通过；无 Git 解压目录可成功重建 wheel；归档与 wheel 版本/源码/API 身份一致 |
| Windows 动态依赖 | dumpbin 仅发现 Python、Winsock、Windows 系统及 MSVC/CRT 运行时，无 dlt698 DLL/开发机 SDK 依赖 |
| 构建缓存 | 捕获固定 sdist 时间戳导致旧对象复用的问题；Python 专属私有编译参数加入源码摘要，输入变化同时重编核心和绑定，独立 C++ 配置不变 |
| 联合门槛 | 完整矩阵、缺少 wheel、混合提交、C++ 制品篡改的测试通过；配置正式发布前必须通过完整 CI |

源码实现覆盖 P0–P3，并提供 P4 的原生 asyncio TCP 入口。低层自定义 executor/channel、手动 RS-485 hooks、异步客户端串口、新 ABI 和 PyPI 推广按计划登记为待开放；普通 API、版本及内核不另起实现。

本地可安装制品位于 `build/python-final-dist/`：`dlt698-0.1.0-cp311-cp311-win_amd64.whl` 和 `dlt698-0.1.0.tar.gz`。两者为 development 制品，版本、提交、源码摘要和 API 摘要相同；最终 wheel 从无 Git 的 sdist 目录重建，并在曾触发旧对象复用的同一缓存中验证修复。

尚未在本机验证：Linux x86_64/aarch64、macOS x86_64/arm64、CPython 3.12–3.14、真实串口/RS-485、真实 ESAM。平台及解释器已有 CI 矩阵，硬件和安全需要实际设备。尚未创建标签、公开 Release 或推广至 PyPI。

最初复用的旧 Ninja 缓存缺少正确 VS SDK 环境，导致安装消费方构建失败；补实际 VS 环境后动态构建通过。旧静态缓存存在陈旧链接对象，因此以新建的独立 VS 直接 cpp 构建完成静态与无 transport 回归。最终通过结果以上表为准。
