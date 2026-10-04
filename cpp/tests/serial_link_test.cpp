#include <dlt698/protocol/link/frame.hpp>
#include <dlt698/transport/memory.hpp>
#include <dlt698/transport/serial_link.hpp>

#include "test.hpp"
using namespace dlt698;
using namespace dlt698::transport;
using namespace std::chrono_literals;

Bytes frame() { return protocol::link::encode_frame(protocol::link::Frame{}).value(); }

void direction_drain_and_gap() {
    auto ex = std::make_shared<ManualExecutor>();
    auto pair = MemoryChannel::pair(ex);
    SerialLinkOptions options;
    options.baud_rate = 1000;  // 33 位间隔正好 33ms，避免测试依赖浮点或真实时间。
    std::vector<bool> directions;
    IChannel::WriteHandler drain;
    options.set_transmit = [&](bool tx) -> Result<void> {
        directions.push_back(tx);
        return {};
    };
    options.async_drain = [&](auto callback) { drain = std::move(callback); };
    auto channel = SerialLinkChannel::wrap(pair.first, ex, options);
    const auto bytes = frame();
    std::optional<Result<Bytes>> received;
    pair.second->async_read([&](auto r) { received = std::move(r); });
    int completed = 0;
    channel->async_write(bytes, [&](auto r) {
        if (r) ++completed;
    });
    ex->run_ready();
    CHECK(received && *received);
    Bytes expected(4, 0xfe);
    expected.insert(expected.end(), bytes.begin(), bytes.end());
    CHECK(received->value() == expected);
    CHECK(directions == std::vector<bool>{true});
    CHECK(drain && completed == 0);
    ex->advance(500ms);  // OS 写完成之后，未排空仍不能完成或恢复接收。
    CHECK(directions.size() == 1 && completed == 0);
    drain({});
    drain({});
    ex->run_ready();
    CHECK(directions == std::vector<bool>({true, false}));
    ex->advance(32ms);
    CHECK(completed == 0);
    ex->advance(1ms);
    CHECK(completed == 1);

    // 最近接收字节后的转发也需要间隔，不能收到请求后立即切换发送方向。
    received.reset();
    channel->async_read([&](auto r) { received = std::move(r); });
    pair.second->async_write({1}, [](auto) {});
    ex->run_ready();
    CHECK(received && *received);
    channel->async_write(bytes, [](auto) {});
    ex->run_ready();
    CHECK(directions.size() == 2);
    ex->advance(32ms);
    CHECK(directions.size() == 2);
    ex->advance(1ms);
    CHECK(directions.back());
    channel->close();
    ex->run_ready();
    CHECK(!directions.back());
}

void budgets_failure_and_destruction() {
    auto ex = std::make_shared<ManualExecutor>();
    auto pair = MemoryChannel::pair(ex);
    SerialLinkOptions options;
    options.max_pending_writes = 1;
    IChannel::WriteHandler drain;
    options.async_drain = [&](auto cb) { drain = std::move(cb); };
    auto channel = SerialLinkChannel::wrap(pair.first, ex, options);
    std::optional<Result<void>> first, second;
    int completed = 0;
    channel->async_write(frame(), [&](auto r) {
        first = std::move(r);
        ++completed;
    });
    channel->async_write(frame(), [&](auto r) { second = std::move(r); });
    ex->run_ready();
    CHECK(!first);
    CHECK(second->error().code == ErrorCode::resource_limit);
    drain(Error{ErrorCode::io_error, 0, "physical drain failed"});
    ex->run_ready();
    CHECK(first->error().code == ErrorCode::io_error && completed == 1);
    drain({});
    ex->advance(1s);
    CHECK(completed == 1);
    auto other = MemoryChannel::pair(ex);
    channel = SerialLinkChannel::wrap(other.first, ex);
    channel->async_write({0x68}, [&](auto r) { second = std::move(r); });
    ex->run_ready();
    CHECK(!*second);
    channel->async_write(frame(), [&](auto r) { first = std::move(r); });
    ex->run_ready();
    channel.reset();
    ex->run_ready();
    CHECK(first->error().code == ErrorCode::closed);
    bool invalid = false;
    options.set_transmit = [](bool) -> Result<void> { return {}; };
    options.async_drain = {};
    try {
        (void)SerialLinkChannel::wrap(other.first, ex, options);
    } catch (const std::invalid_argument&) {
        invalid = true;
    }
    CHECK(invalid);
    std::weak_ptr<ManualExecutor> observer;
    {
        auto unpumped = std::make_shared<ManualExecutor>();
        observer = unpumped;
        auto ends = MemoryChannel::pair(unpumped);
        auto unpumped_link = SerialLinkChannel::wrap(ends.first, unpumped);
        unpumped_link->async_write(frame(), [](auto) {});
    }
    CHECK(observer.expired());
}

int main() {
    return tests([] {
        direction_drain_and_gap();
        budgets_failure_and_destruction();
    });
}
