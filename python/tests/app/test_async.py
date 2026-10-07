"""异步完成、取消、同一原生会话与 loop 生命周期。"""

import asyncio
import socket

import pytest

import dlt698 as d


@pytest.mark.asyncio
async def test_async_read_write_partial_and_close():
    attribute = d.Oad(oi=0x200F, attribute=2)
    with d.Server() as server:
        server.set(attribute, d.Data.uint16(5000))
        server.start_tcp("127.0.0.1", 0)
        async with d.AsyncClient() as client:
            await client.connect_tcp("127.0.0.1", server.local_port)
            assert (await client.get(attribute)).require_data().as_uint16() == 5000
            result = await client.get_list([attribute, d.Oad(oi=0xF002, attribute=2)])
            assert result.attributes[1].result == 4
            assert await client.set(attribute, d.Data.uint16(4000)) == 3
        await client.aclose()


@pytest.mark.asyncio
async def test_async_preset_profile_and_loop_keeps_running():
    with d.Server() as server:
        server.start_tcp("127.0.0.1", 0, d.ConnectionProfile.local_preset)
        async with d.AsyncClient() as client:
            await client.connect_tcp(
                "127.0.0.1", server.local_port, d.ConnectionProfile.local_preset
            )
            result = await client.get(d.Oad(oi=0xF003, attribute=2))
            assert result.dar == 4
            event = asyncio.Event()
            asyncio.get_running_loop().call_soon(event.set)
            await asyncio.wait_for(event.wait(), timeout=1)


@pytest.mark.asyncio
async def test_failed_connection_can_be_retried():
    with socket.socket() as reserved:
        reserved.bind(("127.0.0.1", 0))
        unused = reserved.getsockname()[1]
        async with d.AsyncClient() as client:
            with pytest.raises(d.Dlt698Error):
                await client.connect_tcp("127.0.0.1", unused)
            with d.Server() as server:
                server.start_tcp("127.0.0.1", 0)
                await client.connect_tcp("127.0.0.1", server.local_port)
                assert (await client.get(d.Oad(oi=0xFFFF, attribute=2))).dar == 4


@pytest.mark.asyncio
async def test_task_cancel_and_loop_close_leave_no_callbacks():
    received = asyncio.Event()
    finished = asyncio.Event()
    events = []

    async def peer(reader, writer):
        try:
            assert await reader.read(65536)
            received.set()
            await reader.read()
        finally:
            writer.close()
            await writer.wait_closed()
            finished.set()

    listener = await asyncio.start_server(peer, "127.0.0.1", 0)
    async with listener, d.AsyncClient(on_event=events.append) as client:
        port = listener.sockets[0].getsockname()[1]
        await client.connect_tcp("127.0.0.1", port, d.ConnectionProfile.local_preset)
        task = asyncio.create_task(client.get(d.Oad(oi=0x200F, attribute=2)))
        await asyncio.wait_for(received.wait(), 2)
        task.cancel()
        with pytest.raises(asyncio.CancelledError):
            await task
        await client.aclose()
        before = len(events)
        await asyncio.wait_for(finished.wait(), 2)
        await asyncio.sleep(0)
        assert len(events) == before
    await client.aclose()
