---
title: 构建选项
description: CMake 选项、导出目标和兼容性约束。
---

# 构建选项

## 项目选项

| 选项 | 默认 | 作用 |
| --- | --- | --- |
| `DLT698_BUILD_TRANSPORT` | `ON` | 构建 standalone Asio 传输层（TCP、原始串口） |
| `DLT698_BUILD_EXAMPLES` | `ON` | 构建 `bin/` 下的示例程序 |
| `DLT698_WARNINGS_AS_ERRORS` | `OFF` | 把项目警告视为错误 |
| `BUILD_SHARED_LIBS` | `ON` | 构建单一共享库；设为 OFF 则构建单一静态库 |
| `BUILD_TESTING` | `ON` | 构建 CTest 测试 |

关闭传输构建：

```sh
cmake -S . -B build/core -DDLT698_BUILD_TRANSPORT=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build/core --config Release --parallel
ctest --test-dir build/core -C Release --output-on-failure
```

`DLT698_BUILD_TRANSPORT=OFF` **不会**移除内存通道、`SerialLinkChannel`、会话、对象服务或 Device——它们都在统一的 dlt698 库中。IoRuntime、TCP/原始串口以及 app 托管入口不构建，也不安装对应头文件。

严格警告检查：

```sh
cmake -S . -B build-werror -DDLT698_WARNINGS_AS_ERRORS=ON -DCMAKE_BUILD_TYPE=Release
```

## 导出的 CMake 目标

| CMake 目标 | 当前内容 |
| --- | --- |
| `dlt698::dlt698` | 单一库，包含编解码、内存通道、会话、对象服务，以及启用时的 TCP/串口和托管 Server/Client |

旧组件名称 `dlt698::core`、`dlt698::session`、`dlt698::service` 保留为统一库的兼容别名；启用传输时也提供 `dlt698::transport` 和 `dlt698::app` 别名，均不生成独立库。

`dlt698::dlt698` 是实际的静态/共享库目标，所有公开函数统一使用 `DLT698_API` 导出宏。Asio 和系统网络库由实现私有依赖。

## 兼容性约束

:::warning 工具链不可混用
静态/共享目标都有安装导出，但生产方和消费方必须使用兼容的编译器、标准库和运行库。MSVC 与 MinGW 库不可混用；共享库不承诺跨工具链的 C++ ABI。
:::

MSVC 目标会以 `PUBLIC` 方式传递 `/permissive-` 之外的 `/utf-8`，因为公开头文件包含 UTF-8 中文注释。调用方不要用自己的 `/utf-8` 覆盖它。

静态构建会以 `PUBLIC` 方式定义 `DLT698_STATIC`，消费方自动继承。

## 安装布局

```text
<prefix>/
  include/dlt698/                公开头文件（关闭传输时排除 TCP/原始串口和 app）
  include/dlt698/export.hpp      构建期生成的导出宏
  bin/dlt698.dll                Windows 动态库
  lib/                          dlt698 库或 Windows 导入库
  lib/cmake/dlt698/              dlt698Config.cmake 与导出目标
  share/dlt698/LICENSE
```

安装包提供了 `installed_consumer` 测试，用独立工程验证 `find_package` 之后的同步 SET、串行适配和原始串口导出符号。
