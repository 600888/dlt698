---
title: 安装与第一个程序
description: 安装 Python 包，并用本机 TCP 完成一次读取。
comments: false
---

# 安装与第一个程序

使用 CPython 3.11–3.14，建议先创建虚拟环境。下面命令中的 `python` 指该环境的解释器。

## 安装

从对应的[联合 Release](https://github.com/600888/dlt698/releases)下载匹配 Python minor 和平台的 wheel，再指定文件安装。例如 Windows x64、CPython 3.11 的命令：

```shell
python -m pip install ./dlt698-0.1.0-cp311-cp311-win_amd64.whl
```

示例文件名中的版本替换为实际下载版本；只有该 Release 已提供对应 wheel 时才可采用此方式。首期分发入口是联合 Release。

从源码安装时，在仓库根目录运行：

```shell
python -m venv .venv
# Windows 激活：.venv\Scripts\Activate.ps1
# Linux/macOS 激活：source .venv/bin/activate
python -m pip install .
```

源码构建需要 C++17 编译器及 CMake 3.20+；Windows 使用 x64 MSVC。pybind11 和协议内核已随仓库提供，普通 wheel 使用者无需安装 C++ SDK。修改绑定后需重新构建安装。

```python
import dlt698

print(dlt698.__version__)
print(dlt698.build_info())
```

版本来自共享 `VERSION`；`build_info()` 可查看提交、源码/API 摘要及构建模式。身份不一致时重新安装完整包。

## 第一个程序：本机读取频率

安装后保存为 `read_frequency.py`，运行 `python read_frequency.py`。程序自己启动服务器，无需电表或外部服务。

```python
from dlt698 import Client, Data, Oad, Server

frequency = Oad(oi=0x200F, attribute=2)

with Server() as server:
    server.set(frequency, Data.uint16(5000))
    server.start_tcp("127.0.0.1", 0)

    with Client() as client:
        client.connect_tcp("127.0.0.1", server.local_port)
        value = client.get(frequency).require_data()
        print(value.as_uint16())

        server.set(frequency, Data.uint16(4998))
        print(client.get(frequency).require_data().as_uint16())
```

输出：

```text
5000
4998
```

`port=0` 自动分配端口；`connect_tcp()` 返回后已完成协议关联。`with` 在退出时关闭连接和服务器。频率 `5000` 是原始值，表示 `50.00 Hz`，点位和倍率见[频率与温度读取](../guide/frequency-temperature.md)。

## 连接真实设备

替换为实际主机、端口及点位：

```python
from dlt698 import Client, ConnectionProfile, Oad

with Client() as client:
    client.connect_tcp("192.168.1.100", 6980, ConnectionProfile.remote_public)
    value = client.get(Oad(oi=0x200F, attribute=2)).require_data()
    print(value.as_uint16())
```

主机和端口为示例，设备必须支持该点位。TCP 默认 `remote_public`，串口默认 `local_public`；预设关联需要双方配置一致。场景、地址及安全要求见 [TCP 连接](../guide/tcp.md)和[配置速查](../guide/configuration.md)。

下一步看[常用操作](./operations.md)或[异步调用](./async.md)。
