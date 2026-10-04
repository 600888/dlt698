#include <chrono>
#include <dlt698/dlt698.hpp>
#include <dlt698/transport/tcp.hpp>
#include <functional>

#include "test.hpp"

using namespace dlt698;
using namespace dlt698::transport;
using namespace dlt698::protocol;
using namespace std::chrono_literals;

template <class F>
void until(const std::shared_ptr<IoRuntime>& rt, F done) {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!done() && std::chrono::steady_clock::now() < deadline) rt->run_for(5ms);
    CHECK(done());
}

void exchange() {
    auto runtime = std::make_shared<IoRuntime>();
    auto listener = TcpListener::listen(runtime, "127.0.0.1", 0, {3, 1024});
    CHECK(listener);
    std::shared_ptr<TcpChannel> server;
    std::shared_ptr<TcpChannel> client;
    link::FrameStreamDecoder server_decoder, client_decoder;
    bool complete = false;
    bool connected = false;
    bool sent = false;
    bool server_sent = false;
    bool callback_failed = false;
    // 传输层隔离用户回调异常，须在回调外检查失败标记，否则断言异常会被吞掉。
    auto fail = [&] { callback_failed = true; };
    std::function<void()> server_read;
    server_read = [&] {
        server->async_read([&](Result<Bytes> chunk) {
            if (!chunk) {
                fail();
                return;
            }
            for (auto& event : server_decoder.feed(chunk.value())) {
                if (!std::holds_alternative<link::Frame>(event)) {
                    fail();
                    return;
                }
                auto frame = std::get<link::Frame>(event);
                auto request = apdu::decode_get(frame.payload);
                if (!request || !std::holds_alternative<apdu::GetRequest>(request.value())) {
                    fail();
                    return;
                }
                const auto& req = std::get<apdu::GetRequest>(request.value());
                apdu::GetResponse response{req.piid,
                                           req.list,
                                           {{req.attributes[0], model::Data{model::UInt16{2413}}}},
                                           {}};
                auto payload = apdu::encode_get(apdu::GetApdu{response});
                if (!payload) {
                    fail();
                    return;
                }
                frame.control = 0xc3;
                frame.payload = std::move(payload).value();
                auto reply = link::encode_frame(frame);
                if (!reply) {
                    fail();
                    return;
                }
                server->async_write(std::move(reply).value(), [&](Result<void> result) {
                    server_sent = bool(result);
                    if (!result) fail();
                });
                return;
            }
            server_read();
        });
    };
    std::function<void()> client_read;
    client_read = [&] {
        client->async_read([&](Result<Bytes> chunk) {
            if (!chunk) {
                fail();
                return;
            }
            for (auto& event : client_decoder.feed(chunk.value())) {
                if (!std::holds_alternative<link::Frame>(event)) {
                    fail();
                    return;
                }
                auto apdu = apdu::decode_get(std::get<link::Frame>(event).payload);
                if (!apdu || !std::holds_alternative<apdu::GetResponse>(apdu.value())) {
                    fail();
                    return;
                }
                const auto& reply = std::get<apdu::GetResponse>(apdu.value());
                if (reply.piid_acd != 1 || reply.attributes.size() != 1 ||
                    !std::holds_alternative<model::Data>(reply.attributes[0].result) ||
                    !(std::get<model::Data>(reply.attributes[0].result) ==
                      model::Data{model::UInt16{2413}})) {
                    fail();
                    return;
                }
                complete = true;
                return;
            }
            client_read();
        });
    };
    listener.value()->async_accept([&](Result<std::shared_ptr<TcpChannel>> accepted) {
        if (!accepted) {
            fail();
            return;
        }
        server = std::move(accepted).value();
        server_read();
    });
    client =
        TcpChannel::connect(runtime, "127.0.0.1", listener.value()->local_port(),
                            [&](Result<void> result) {
                                if (!result) {
                                    fail();
                                    return;
                                }
                                connected = true;
                                link::Frame request;
                                request.client = 1;
                                request.payload = hex("05 01 01 20 00 02 00 00");
                                auto frame = link::encode_frame(request);
                                if (!frame) {
                                    fail();
                                    return;
                                }
                                client->async_write(std::move(frame).value(), [&](Result<void> r) {
                                    sent = bool(r);
                                    if (!r) fail();
                                });
                                client_read();
                            },
                            {2, 1024});
    until(runtime, [&] { return (complete && server_sent && sent) || callback_failed; });
    CHECK(!callback_failed);
    CHECK(connected);
    CHECK(complete);
    // 验证只允许一个在途读取，关闭仅完成一次，并且重复关闭具有幂等性。
    int closed_reads = 0, busy_reads = 0;
    bool wrong_error = false;
    client->async_read([&](Result<Bytes> r) {
        ++closed_reads;
        if (r || r.error().code != ErrorCode::closed) wrong_error = true;
    });
    client->async_read([&](Result<Bytes> r) {
        ++busy_reads;
        if (r || r.error().code != ErrorCode::busy) wrong_error = true;
    });
    until(runtime, [&] { return busy_reads == 1; });
    client->close();
    client->close();
    server->close();
    listener.value()->close();
    until(runtime, [&] { return closed_reads == 1; });
    runtime->run_for(10ms);
    CHECK(!wrong_error);
    CHECK(closed_reads == 1 && busy_reads == 1);
    bool write_closed = false;
    client->async_write(
        {1}, [&](Result<void> r) { write_closed = !r && r.error().code == ErrorCode::closed; });
    until(runtime, [&] { return write_closed; });
    client.reset();
    server.reset();
    listener.value().reset();
    runtime->run_for(5ms);
}

void queue_limit() {
    auto runtime = std::make_shared<IoRuntime>();
    auto listener = TcpListener::listen(runtime, "127.0.0.1", 0);
    CHECK(listener);
    std::shared_ptr<TcpChannel> server;
    bool connected = false;
    listener.value()->async_accept([&](Result<std::shared_ptr<TcpChannel>> r) {
        if (r) server = r.value();
    });
    auto client = TcpChannel::connect(runtime, "127.0.0.1", listener.value()->local_port(),
                                      [&](Result<void> r) { connected = bool(r); }, {4096, 4});
    until(runtime, [&] { return connected && bool(server); });
    bool rejected = false, callback_alive = false;
    client->async_write(Bytes(5), [&](Result<void> r) {
        rejected = !r && r.error().code == ErrorCode::resource_limit;
    });
    client->async_write({}, [](Result<void>) { throw std::runtime_error("callback failure"); });
    client->async_write({}, [&](Result<void> r) { callback_alive = bool(r); });
    until(runtime, [&] { return rejected && callback_alive; });
    // 预算须涵盖已投递到执行器但尚未进入 strand 队列的写操作。
    bool first_sent = false, second_limited = false;
    client->async_write(Bytes(3), [&](Result<void> r) { first_sent = bool(r); });
    client->async_write(Bytes(2), [&](Result<void> r) {
        second_limited = !r && r.error().code == ErrorCode::resource_limit;
    });
    until(runtime, [&] { return first_sent && second_limited; });
    client->close();
    server->close();
    listener.value()->close();
    runtime->run_for(10ms);
    client.reset();
    server.reset();
    listener.value().reset();
    runtime->run_for(5ms);
}

void destruction() {
    auto runtime = std::make_shared<IoRuntime>();
    std::weak_ptr<IoRuntime> weak_runtime = runtime;
    auto listener = TcpListener::listen(runtime, "127.0.0.1", 0);
    CHECK(listener);
    std::shared_ptr<TcpChannel> server;
    bool connected = false;
    listener.value()->async_accept([&](Result<std::shared_ptr<TcpChannel>> r) {
        if (r) server = r.value();
    });
    auto client = TcpChannel::connect(runtime, "127.0.0.1", listener.value()->local_port(),
                                      [&](Result<void> r) { connected = bool(r); });
    until(runtime, [&] { return connected && bool(server); });
    int read_closed = 0;
    client->async_read([&](Result<Bytes> r) {
        if (!r && r.error().code == ErrorCode::closed) ++read_closed;
    });
    runtime->run_for(5ms);
    // 销毁仍有挂起操作的通道时，内部回调不得通过引用环保活运行时。
    client.reset();
    until(runtime, [&] { return read_closed == 1; });
    int accept_closed = 0;
    listener.value()->async_accept([&](Result<std::shared_ptr<TcpChannel>> r) {
        if (!r && r.error().code == ErrorCode::closed) ++accept_closed;
    });
    runtime->run_for(5ms);
    listener.value().reset();
    until(runtime, [&] { return accept_closed == 1; });
    server.reset();
    runtime->run_for(5ms);
    runtime.reset();
    CHECK(weak_runtime.expired());
}

int main() {
    return tests([] {
        exchange();
        queue_limit();
        destruction();
    });
}
