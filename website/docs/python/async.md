---
title: 异步调用
description: 用 asyncio 客户端与服务器完成读取，并确定性关闭资源。
comments: false
---

# 异步调用

`AsyncClient` 直接驱动原生异步会话。连接、读写、记录方法使用 `await`，结果结构与同步接口一致。请在正在运行的事件循环中创建并使用客户端。

## 完整 TCP 示例

安装后保存为 `async_read.py`，运行 `python async_read.py`，输出 `5000`。程序使用本机服务器，无需真实设备。

```python
import asyncio

from dlt698 import AsyncClient, AsyncServer, Data, Oad


async def main() -> None:
    frequency = Oad(oi=0x200F, attribute=2)

    async with AsyncServer() as server:
        server.set(frequency, Data.uint16(5000))
        await server.start_tcp("127.0.0.1", 0)

        async with AsyncClient() as client:
            await client.connect_tcp("127.0.0.1", server.local_port)
            result = await client.get(frequency)
            print(result.require_data().as_uint16())


if __name__ == "__main__":
    asyncio.run(main())
```

`server.set(...)` 是本地更新，保持同步调用；`start_tcp()`、`stop()` 和客户端 I/O 使用 `await`。已有同步服务器也可搭配 AsyncClient。完整服务端示例见 [async_server.py](https://github.com/600888/dlt698/blob/main/python/examples/async_server.py)。

## 串口与其他操作

下面程序需要真实串口和设备：

```python
import asyncio

from dlt698 import AsyncClient, Oad


async def main() -> None:
    async with AsyncClient() as client:
        await client.open_serial("COM3", 9600)
        value = (await client.get(Oad(oi=0x200F, attribute=2))).require_data()
        print(value.as_uint16())


if __name__ == "__main__":
    asyncio.run(main())
```

完整参数使用 `await client.open_serial_configured(path, serial, link, profile)`；参数与[同步串口示例](./operations.md#串口与超时配置)一致。批量 GET、SET/ACTION、记录也只需在相应调用前加 `await`，结果仍须检查逐项 DAR。查询和结果用法见[常用操作](./operations.md)。

## 超时、取消与关闭

下面片段中的 `client` 已关联：

```python
import asyncio

from dlt698 import DarError, Dlt698Error, Oad

try:
    async with asyncio.timeout(3.0):
        result = await client.get(Oad(oi=0x200F, attribute=2))
        print(result.require_data().as_uint16())
except TimeoutError:
    print("读取超时；重新建立关联后再发起请求")
except DarError as error:
    print("业务拒绝", error.dar)
except Dlt698Error as error:
    print(error.code, error.context)
```

task 取消抛 `asyncio.CancelledError`，应让它向上传播。取消在途请求会取消原生会话，需要重新关联；不会自动重试操作，写入/方法的远端执行结果可能未知。不要对同一 AsyncClient 并发发起事务。

优先使用 `async with`；手动管理时，在事件循环关闭前 `await client.aclose()`。`await client.disconnect()` 释放当前关联并保留客户端供重连；`close()` 强制终止。AsyncServer 用 `await server.stop()` 或退出 `async with` 收尾，仍需遵循设备与连接的生命周期约定。

异步 `on_event` 由所属事件循环分发，客户端回调接收 Completion，服务器回调接收 Event；不要在回调中阻塞事件循环。详细扩展规则见[回调与生命周期约定](https://github.com/600888/dlt698/blob/main/python/feature-gaps.md#回调与生命周期约定)。
