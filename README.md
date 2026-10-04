# dlt698
从零构建的现代 C++17 DL/T 698.45 协议库，参考 dlt645 的分层架构，面向集中器、采集终端和电力网关等协议对接场景。

当前已完成第一批核心与 TCP 开发实现，尚未覆盖完整协议。实现基线为 `plan/dlt69845-2017.pdf` 中的 DL/T 698.45—2017。

[整体实现计划](plan/implementation-plan.md) 包含分层架构、标准覆盖范围、阶段任务、验收条件，以及后续 pybind11 绑定和 PyPI 发布方案。

实施顺序：C++ 基础模型与 A-XDR → 链路层 → APDU → 通信与会话 → 分段、记录、上报与代理 → 对象与安全完善 → C++ 稳定 → Python 绑定 → wheel/PyPI 发布。

当前包含精确类型 Data、A-XDR 基础编解码、OI/OAD/OMD/TI/TSA、HCS/FCS、变长地址、扰码、流解析，以及 GET Normal/NormalList 请求与响应。TCP 通道使用 `third/asio/include` 中的 standalone Asio 1.38.2；纯核心可以关闭传输模块单独构建。`third/spdlog` 暂未接入。

[支持矩阵与验证范围](docs/protocol-coverage.md) · [当前 C++ API](docs/cpp-api.md) · [标准测试向量](tests/vectors/README.md)

在仓库根目录构建与测试：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix ./build/stage
```

纯核心构建可加 `-DDLT698_BUILD_TRANSPORT=OFF`；共享库可加 `-DBUILD_SHARED_LIBS=ON`；严格警告检查可加 `-DDLT698_WARNINGS_AS_ERRORS=ON`。也支持 `cmake -S cpp -B build-core`。C++17、CMake 3.20+ 和匹配的 C++ 编译器为必要条件，当前构建不需要 Python。

安装后的调用方：

```cmake
find_package(dlt698 0.1 CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE dlt698::dlt698)
```

只使用编解码时链接 `dlt698::core`；TCP 组件链接 `dlt698::transport`。Windows 共享库运行时需将安装目录的 DLL 放在程序旁边或加入 PATH。

解帧示例程序位于构建目录 `bin/`（Visual Studio 多配置构建为 `bin/Release/`），可使用固定测试帧：

```sh
dlt698_decode "68 17 00 43 05 07 09 19 05 16 20 00 15 60 05 01 01 40 01 02 00 00 C6 07 16"
```

预连接/应用连接、完整 Session、SET/ACTION、记录、上报、代理、链路分帧、串口、安全后端、对象目录及 Python 绑定均在后续阶段实现。
