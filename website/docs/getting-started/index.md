---
title: 快速开始
description: 安装、构建，并把第一个程序跑起来。
comments: false
---

# 快速开始

这一部分带你从零开始：装好库、编译、在内存通道上跑通一次完整的应用连接。

## 三步跑通

```text
1. cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
2. ctest --test-dir build --output-on-failure
3. ./build/bin/dlt698_memory_mutation
```

第三条命令不需要网络和真实设备。它在内存链路上完成同步 CONNECT → SET → ACTION → GET → RELEASE，输出：

```text
SET DAR=0 ACTION DAR=0 GET UInt16=42
```

## 这一部分的页面

| 页面 | 解决的问题 |
| --- | --- |
| [安装与构建](./installation.md) | 怎样编译、测试、安装，以及调用方如何 `find_package` |
| [第一个程序](./quick-start.md) | 怎样用内存通道搭出客户机和服务器各一端 |
| [构建选项](./build-options.md) | 怎样裁剪传输层、切换静态/共享库、开启严格警告 |

## 选择合适的起点

| 你的场景 | 建议阅读路径 |
| --- | --- |
| 只要解析报文字节 | [核心类型](../core/result.md) → [APDU 编解码](../protocol/apdu.md) |
| 做集中器/终端主站 | [Session](../session/session.md) → [TCP 通道](../transport/tcp.md) |
| 做本地仿真、单元测试 | [执行器](../transport/executor.md) → [通道与内存通道](../transport/channel.md) |
| 提供设备侧数据 | [对象目录与 Provider](../session/object.md) → [ServerService](../session/service.md) |
| 现场串口接入 | [串口与串行链路](../transport/serial.md) |

## 必要条件

C++17、CMake 3.20 以上和匹配的 C++ 编译器是必要条件，当前构建不需要 Python。传输层使用仓库内置的 standalone Asio，仅作为传输模块的私有编译依赖，调用方不需要 Asio 头文件。
