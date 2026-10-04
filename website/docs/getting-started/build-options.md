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
| `BUILD_SHARED_LIBS` | `OFF` | 构建共享库而不是静态库 |
| `BUILD_TESTING` | `ON` | 构建 CTest 测试 |

关闭传输构建：

```sh
cmake -S . -B build-core -DDLT698_BUILD_TRANSPORT=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-core --config Release --parallel
ctest --test-dir build-core -C Release --output-on-failure
```

`DLT698_BUILD_TRANSPORT=OFF` **不会**移除内存通道、`SerialLinkChannel`、会话和对象服务——它们都在 core/session/service 里。只有 `IoRuntime`、TCP 通道与监听器、原始 `SerialChannel` 会消失。

严格警告检查：

```sh
cmake -S . -B build-werror -DDLT698_WARNINGS_AS_ERRORS=ON -DCMAKE_BUILD_TYPE=Release
```

## 导出的 CMake 目标

| 目标 | 类型 | 依赖 |
| --- | --- | --- |
| `dlt698::dlt698` | INTERFACE | 聚合下列全部目标 |
| `dlt698::core` | 静态/共享 | Threads（私有） |
| `dlt698::session` | 静态/共享 | `core`（公开） |
| `dlt698::service` | 静态/共享 | `session`（公开）、Threads（私有） |
| `dlt698::transport` | 静态/共享 | 仅 `DLT698_BUILD_TRANSPORT=ON` 时存在 |

`dlt698::dlt698` 是接口目标，不产生库文件，只聚合依赖。它会按顺序链接 `core`、`service`，以及启用时的 `transport`。

## 兼容性约束

:::warning 工具链不可混用
静态/共享目标都有安装导出，但生产方和消费方必须使用兼容的编译器、标准库和运行库。MSVC 与 MinGW 库不可混用；共享库不承诺跨工具链的 C++ ABI。
:::

MSVC 目标会以 `PUBLIC` 方式传递 `/permissive-` 之外的 `/utf-8`，因为公开头文件包含 UTF-8 中文注释。调用方不要用自己的 `/utf-8` 覆盖它。

静态构建会定义 `DLT698_STATIC`、`DLT698_SESSION_STATIC`、`DLT698_SERVICE_STATIC`（以及传输层的对应宏），这些定义同样是 `PUBLIC` 的。

## 安装布局

```text
<prefix>/
  include/dlt698/                公开头文件（transport 只装 channel/memory/serial_link）
  include/dlt698/export.hpp      构建期生成的导出宏
  include/dlt698/session_export.hpp
  include/dlt698/service_export.hpp
  lib/                           库文件
  lib/cmake/dlt698/              dlt698Config.cmake 与导出目标
  share/dlt698/LICENSE
```

安装包提供了 `installed_consumer` 测试，用独立工程验证 `find_package` 之后的同步 SET、串行适配和原始串口导出符号。
