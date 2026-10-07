---
title: 使用指南(Python)
description: 用简短 Python 示例完成安装、读取、写入、记录查询和异步接入。
comments: false
---

# 使用指南(Python)

Python 包直接调用同一 C++ 协议内核，共用根 `VERSION`。通常从 `Client` / `Server` 开始；需要 asyncio 时使用 `AsyncClient` / `AsyncServer`。连接、关联和协议分块由库处理。

## 从这里开始

| 页面 | 内容 |
| --- | --- |
| [安装与第一个程序](./quick-start.md) | 安装、版本检查，以及不需要真实设备的 TCP 读取程序 |
| [常用操作](./operations.md) | 串口、配置、批量读取、写入/方法、记录、数据与错误 |
| [异步调用](./async.md) | asyncio 客户端和服务器、串口、取消与收尾 |

本章以代码和必要说明为主。[使用指南(C++)](../guide/index.md)已详细介绍协议与点位，Python 使用相同的地址、单位倍率和 DAR 语义。

## 常用入口

| 模块 | 用途 |
| --- | --- |
| `dlt698` / `dlt698.app` | 同步、异步客户端与服务器、配置、共享 Device |
| `dlt698.model` / `dlt698.codec` | 精确 Data 类型、选择器、Data/帧/APDU 编解码 |
| `dlt698.standard` | 标准点位、工程量转换、记录查询模板 |
| `dlt698.protocol` / `dlt698.service` | 完整协议消息、对象目录与 provider |
| `dlt698.session` / `dlt698.transport` / `dlt698.security` | 显式驱动、传输和安全扩展 |

高级接入参见仓库的 [provider 示例](https://github.com/600888/dlt698/blob/main/python/examples/provider.py)、[Python 说明](https://github.com/600888/dlt698/blob/main/python/README.md)和[扩展点约定](https://github.com/600888/dlt698/blob/main/python/feature-gaps.md)。普通读取不需要手动驱动 Session。

## 范围与版本

构建目标为 CPython 3.11–3.14 的标准 GIL 版本；free-threaded、PyPy 和子解释器尚未开放。完整类型声明及 `py.typed` 随包交付，编辑器可直接提示公开接口。

平台和解释器是否已通过验收，以对应版本的[验证记录](https://github.com/600888/dlt698/blob/main/python/verification.md)为准；真实串口、RS-485 时序和 ESAM 需要设备验证。Python 分阶段开放接口，当前导出以公开模块和[接口清单](https://github.com/600888/dlt698/blob/main/python/api-map.json)为准。

两端正式包使用同一个联合 Release。不要混装其他构建的包装层或原生扩展；导入时会检查版本与构建身份。
