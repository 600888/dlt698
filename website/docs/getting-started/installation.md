---
title: 安装与构建
description: 编译、测试、安装，以及在调用方 CMake 中引用。
---

# 安装与构建

## 编译与测试

在仓库根目录执行：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix ./build/stage
```

Visual Studio 等多配置生成器必须带 `--config Release`，`-DCMAKE_BUILD_TYPE` 不生效。示例程序输出到 `bin/`（多配置构建为 `bin/Release/`）。

只编译库本身、不需要测试时可以直接用 `cpp/` 独立入口：

```sh
cmake -S cpp -B build/core -DCMAKE_BUILD_TYPE=Release
cmake --build build/core --config Release --parallel
```

## 安装后的引用

```cmake
find_package(dlt698 1.0 CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE dlt698::dlt698)
```

| CMake 目标 | 当前内容 |
| --- | --- |
| `dlt698::dlt698` | 单一库，包含编解码、内存通道、会话、对象服务，以及启用时的 TCP/串口和托管 Server/Client |

旧组件名称 `dlt698::core`、`dlt698::session`、`dlt698::service` 保留为统一库的兼容别名；启用传输时也提供 `dlt698::transport` 和 `dlt698::app` 别名，均不生成独立库。

默认构建单一动态库；设置 `BUILD_SHARED_LIBS=OFF` 可生成单一静态库。

## 调用方注意事项

- **源文件编码。** 公开头文件包含 UTF-8 中文注释。MSVC 目标会把 `/utf-8` 作为 `PUBLIC` 编译选项传递给安装包调用方，不需要额外设置；自行添加编译选项时不要覆盖它。
- **工具链一致性。** 生产方和消费方须使用兼容的编译器、标准库和运行库，MSVC 与 MinGW 库不可混用。共享库不承诺跨工具链的 C++ ABI。
- **Windows DLL。** 共享库运行时需把安装目录的 DLL 放在程序旁边或加入 `PATH`。
- **头文件范围。** 默认安装会排除 `transport` 目录，只安装 `channel.hpp`、`memory.hpp`、`serial_link.hpp`；`tcp.hpp`、`serial.hpp` 属于需要传输构建的接口。

## 解帧示例

仓库附带 `dlt698_decode`，可以直接解一帧固定测试数据：

```sh
dlt698_decode "68 17 00 43 05 07 09 19 05 16 20 00 15 60 05 01 01 40 01 02 00 00 C6 07 16"
```

更多示例见[示例程序](../appendix/examples.md)。
