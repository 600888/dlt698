#include <dlt698/service/service.hpp>
#include <dlt698/transport/memory.hpp>

#include "test.hpp"

using namespace dlt698;
using namespace dlt698::session;
using namespace dlt698::service;
using namespace dlt698::protocol;
using namespace dlt698::transport;
using namespace std::chrono_literals;

struct Harness {
    std::shared_ptr<ManualExecutor> executor = std::make_shared<ManualExecutor>();
    std::pair<std::shared_ptr<MemoryChannel>, std::shared_ptr<MemoryChannel>> channels =
        MemoryChannel::pair(executor, {1, 65536});
    std::shared_ptr<Session> client;
    std::shared_ptr<Session> server;
    std::shared_ptr<ObjectRegistry> objects = std::make_shared<ObjectRegistry>();
    std::shared_ptr<MemoryObject> voltage = std::make_shared<MemoryObject>();

    explicit Harness(SessionOptions client_options = {}, SessionOptions server_options = {}) {
        client_options.role = Role::client;
        server_options.role = Role::server;
        client = std::make_shared<Session>(channels.first, executor, client_options);
        server = std::make_shared<Session>(channels.second, executor, server_options);
        voltage->set(2,
                     model::Array{{model::UInt16{2413}, model::UInt16{2414}, model::UInt16{2415}}});
        CHECK(objects->register_object({0x2000,
                                        "电压",
                                        {{2, model::DataType::array, true},
                                         {3, model::DataType::uint16, false},
                                         {4, model::DataType::uint16, true}}},
                                       voltage));
        ServerService service(server, objects);
        client->start();
        server->start();
        executor->run_ready();
    }

    ~Harness() {
        if (client) client->close();
        if (server) server->close();
        executor->run_ready();
    }

    apdu::ConnectResponse connect() {
        std::optional<Result<apdu::ConnectResponse>> result;
        client->async_connect([&](auto value) { result = std::move(value); });
        executor->run_ready();
        CHECK(result);
        CHECK(*result);
        return std::move(*result).value();
    }
};

void executor_and_memory() {
    auto executor = std::make_shared<ManualExecutor>();
    int count = 0;
    executor->post([&] {
        ++count;
        throw std::runtime_error("callback");
    });
    auto cancelled = executor->schedule(1ms, [&] { count += 100; });
    cancelled->cancel();
    executor->schedule(2ms, [&] { ++count; });
    executor->run_ready();
    CHECK(count == 1);
    executor->advance(2ms);
    CHECK(count == 2);
    auto channels = MemoryChannel::pair(executor, {2, 4});
    std::optional<Result<Bytes>> first, second;
    std::optional<Result<void>> write;
    channels.first->async_read([&](auto r) { first = std::move(r); });
    channels.first->async_read([&](auto r) { second = std::move(r); });
    channels.second->async_write(Bytes(5), [&](auto r) { write = std::move(r); });
    executor->run_ready();
    CHECK(!first);
    CHECK(second->error().code == ErrorCode::busy);
    CHECK(write->error().code == ErrorCode::resource_limit);
    channels.second->async_write({1, 2, 3}, [&](auto r) { write = std::move(r); });
    executor->run_ready();
    CHECK(*write);
    CHECK(first->value() == Bytes({1, 2}));
    channels.second->close();
    executor->run_ready();
    channels.first->async_read([&](auto r) { first = std::move(r); });
    executor->run_ready();
    CHECK(first->value() == Bytes{3});
    channels.first->async_read([&](auto r) { first = std::move(r); });
    executor->run_ready();
    CHECK(first->error().code == ErrorCode::closed);

    // 待写预算在调用时占用：即使读任务先于写任务运行，也不能绕过排队字节限制。
    auto bounded = MemoryChannel::pair(executor, {4, 4, 2});
    std::optional<Result<void>> queued_first, queued_second;
    bounded.second->async_read([](auto) {});
    bounded.first->async_write(Bytes(4), [&](auto r) { queued_first = std::move(r); });
    bounded.first->async_write(Bytes(1), [&](auto r) { queued_second = std::move(r); });
    executor->run_ready();
    CHECK(queued_first && *queued_first);
    CHECK(queued_second->error().code == ErrorCode::resource_limit);
    bounded.first->async_write({}, [](auto) {});
    bounded.first->async_write({}, [](auto) {});
    bounded.first->async_write({}, [&](auto r) { queued_second = std::move(r); });
    executor->run_ready();
    CHECK(queued_second->error().code == ErrorCode::resource_limit);

    auto destroyed = MemoryChannel::pair(executor);
    std::optional<Result<Bytes>> destroyed_read;
    destroyed.first->async_read([&](auto r) { destroyed_read = std::move(r); });
    executor->run_ready();
    destroyed = {};
    executor->run_ready();
    CHECK(destroyed_read->error().code == ErrorCode::closed);
}

void queued_lifetime_and_clock_failure() {
    auto executor = std::make_shared<ManualExecutor>();
    auto channels = MemoryChannel::pair(executor);
    auto client = std::make_shared<Session>(channels.first, executor);
    std::optional<Result<apdu::ConnectResponse>> result;
    int completed = 0;
    client->start();
    client->async_connect([&](auto r) {
        result = std::move(r);
        ++completed;
    });
    client.reset();
    executor->run_ready();
    CHECK(result->error().code == ErrorCode::closed);
    CHECK(completed == 1);

    // 从未驱动的 start/read/write/close 任务不能形成执行器与状态之间的内部强引用环。
    std::weak_ptr<ManualExecutor> observer;
    {
        auto unpumped = std::make_shared<ManualExecutor>();
        observer = unpumped;
        auto pair = MemoryChannel::pair(unpumped);
        auto session = std::make_shared<Session>(pair.first, unpumped);
        session->start();
        session->async_connect([](auto) {});
        pair.second->async_write(Bytes(4), [](auto) {});
        pair.second->async_read([](auto) {});
    }
    CHECK(observer.expired());

    SessionOptions options;
    options.role = Role::server;
    options.calendar_clock = []() -> model::DateTime { throw std::runtime_error("clock"); };
    auto clock_pair = MemoryChannel::pair(executor);
    auto server = std::make_shared<Session>(clock_pair.second, executor, options);
    server->start();
    std::optional<Result<apdu::LinkResponse>> linked;
    server->async_link(apdu::LinkRequestType::login, 60, [&](auto r) { linked = std::move(r); });
    executor->run_ready();
    CHECK(linked && !*linked);
    CHECK(linked->error().code == ErrorCode::invalid_value);
    CHECK(server->state() == State::closed);

    // 客户机收到 CONNECT 成功后若本地日历失败，挂起连接回调同样只能结束一次。
    auto connection_pair = MemoryChannel::pair(executor);
    options.role = Role::client;
    auto bad_clock = std::make_shared<Session>(connection_pair.first, executor, options);
    options.role = Role::server;
    options.calendar_clock = {};
    auto normal_server = std::make_shared<Session>(connection_pair.second, executor, options);
    bad_clock->start();
    normal_server->start();
    result.reset();
    completed = 0;
    bad_clock->async_connect([&](auto r) {
        result = std::move(r);
        ++completed;
    });
    executor->run_ready();
    CHECK(result && !*result);
    CHECK(result->error().code == ErrorCode::invalid_value);
    CHECK(completed == 1);
    CHECK(bad_clock->state() == State::closed);

    // 闲置释放通知编码超限后必须维持 closed，不能再恢复到 preconnected。
    auto notification_pair = MemoryChannel::pair(executor);
    SessionOptions tiny;
    tiny.role = Role::server;
    tiny.preset_association = true;
    tiny.parameters.apdu_bytes = 1;
    tiny.parameters.timeout_seconds = 1;
    auto notification_server = std::make_shared<Session>(notification_pair.second, executor, tiny);
    notification_server->start();
    executor->run_ready();
    CHECK(notification_server->state() == State::associated);
    executor->advance(1s);
    CHECK(notification_server->state() == State::closed);
}

void object_access() {
    Harness h;
    CHECK(std::get<std::uint8_t>(h.objects->read({0x9999, 2, 0})) == 4);
    CHECK(std::get<std::uint8_t>(h.objects->read({0x2000, 3, 0})) == 3);
    CHECK(std::get<std::uint8_t>(h.objects->read({0x2000, 2, 4})) == 8);
    CHECK(std::get<std::uint8_t>(h.objects->read({0x2000, 0x22, 0})) == 4);
    h.voltage->set(4, model::Int8{1});
    CHECK(std::get<std::uint8_t>(h.objects->read({0x2000, 4, 0})) == 7);
    CHECK(!h.objects->register_object({0x2000, "重复", {{2, model::DataType::array, true}}},
                                      h.voltage));
    CHECK(!h.objects->register_object({0x1234, "错误", {{0, model::DataType::null, true}}},
                                      h.voltage));
    const auto response = h.connect();
    CHECK(response.result == 0);
    CHECK(response.parameters.protocol[0] == 0xe1);
    CHECK(response.parameters.receive_window == 1);
    CHECK(response.parameters.function == std::array<std::uint8_t, 16>{});
    CHECK(h.client->state() == State::associated);
    CHECK(h.server->state() == State::associated);
    ClientService client(h.client);
    std::optional<Result<ObjectValue>> single;
    client.async_get({0x2000, 2, 1}, [&](auto r) { single = std::move(r); });
    h.executor->run_ready();
    CHECK(single && *single);
    CHECK(std::get<model::Data>(single->value()) == model::Data{model::UInt16{2413}});
    std::optional<Result<apdu::GetResponse>> list;
    client.async_get_list({{0x2000, 2, 0}, {0x9999, 2, 0}, {0x2000, 3, 0}},
                          [&](auto r) { list = std::move(r); });
    h.executor->run_ready();
    CHECK(list && *list);
    const auto& values = list->value().attributes;
    CHECK(values.size() == 3);
    CHECK(std::get<model::Data>(values[0].result).type() == model::DataType::array);
    CHECK(std::get<std::uint8_t>(values[1].result) == 4);
    CHECK(std::get<std::uint8_t>(values[2].result) == 3);
    std::optional<Result<void>> released;
    h.client->async_release([&](auto r) { released = std::move(r); });
    h.executor->run_ready();
    CHECK(released && *released);
    CHECK(h.client->state() == State::preconnected);
    CHECK(h.server->state() == State::preconnected);
    single.reset();
    client.async_get({0x2000, 2, 0}, [&](auto r) { single = std::move(r); });
    h.executor->run_ready();
    CHECK(single->error().code == ErrorCode::not_associated);
    CHECK(h.connect().result == 0);
    // 回调可以提交下一次读取；执行器投递而不重入正在完成的事务。
    int done = 0;
    bool failed = false;
    client.async_get({0x2000, 2, 1}, [&](auto r) {
        if (!r) failed = true;
        ++done;
        client.async_get({0x2000, 2, 2}, [&](auto v) {
            if (!v) failed = true;
            ++done;
        });
    });
    h.executor->run_ready();
    CHECK(done == 2 && !failed);
}

void link_and_negotiation() {
    SessionOptions client_options, server_options;
    client_options.require_login = server_options.require_login = true;
    client_options.calendar_clock = server_options.calendar_clock = [] {
        return model::DateTime{{7, 0xe0, 5, 19, 4, 8, 5, 0, 0, 0xa4}};
    };
    client_options.clock_trusted = true;
    client_options.parameters.send_frame_bytes = 256;
    client_options.parameters.receive_frame_bytes = 512;
    client_options.parameters.apdu_bytes = 128;
    Harness h(client_options, server_options);
    CHECK(h.client->state() == State::disconnected);
    std::optional<Result<apdu::ConnectResponse>> denied;
    h.client->async_connect([&](auto r) { denied = std::move(r); });
    h.executor->run_ready();
    CHECK(denied->error().code == ErrorCode::not_associated);
    std::optional<Result<apdu::LinkResponse>> login;
    h.server->async_link(apdu::LinkRequestType::login, 180, [&](auto r) { login = std::move(r); });
    h.executor->run_ready();
    CHECK(login && *login);
    CHECK(login->value().result == 0x80);
    CHECK(h.client->state() == State::preconnected);
    CHECK(h.server->state() == State::preconnected);
    const auto response = h.connect();
    CHECK(!response.result);
    CHECK(response.parameters.send_frame_bytes == 512);
    CHECK(response.parameters.receive_frame_bytes == 256);
    CHECK(response.parameters.apdu_bytes == 128);
    h.server->async_link(apdu::LinkRequestType::heartbeat, 180,
                         [&](auto r) { login = std::move(r); });
    h.executor->run_ready();
    CHECK(*login);
    CHECK(h.client->state() == State::associated);
    std::optional<Result<void>> released;
    h.client->async_release([&](auto r) { released = std::move(r); });
    h.executor->run_ready();
    CHECK(*released);
    h.server->async_link(apdu::LinkRequestType::logout, 0, [&](auto r) { login = std::move(r); });
    h.executor->run_ready();
    CHECK(*login);
    CHECK(h.server->state() == State::disconnected);
    server_options.parameters.version = 0x11;
    client_options.require_login = server_options.require_login = false;
    Harness mismatch(client_options, server_options);
    CHECK(mismatch.connect().result == 5);
    CHECK(mismatch.client->state() == State::preconnected);
    // 协议一致性块只允许协商交集，不能因为 GET codec 已实现就强行开启被关闭的能力。
    server_options.parameters.version = 0x10;
    server_options.parameters.protocol = {0x80};
    Harness connect_only({}, server_options);
    CHECK(connect_only.connect().parameters.protocol[0] == 0x80);
    std::optional<Result<apdu::GetResponse>> unsupported;
    connect_only.client->async_get({{0x2000, 2, 0}}, false,
                                   [&](auto r) { unsupported = std::move(r); });
    connect_only.executor->run_ready();
    CHECK(unsupported->error().code == ErrorCode::unsupported_service);
}

struct PeerHarness {
    std::shared_ptr<ManualExecutor> executor = std::make_shared<ManualExecutor>();
    std::pair<std::shared_ptr<MemoryChannel>, std::shared_ptr<MemoryChannel>> channels =
        MemoryChannel::pair(executor);
    std::shared_ptr<Session> client;

    explicit PeerHarness(SessionOptions options = {}) {
        options.preset_association = true;
        client = std::make_shared<Session>(channels.first, executor, options);
        client->start();
        executor->run_ready();
    }

    ~PeerHarness() {
        if (client) client->close();
        channels.second->close();
        executor->run_ready();
    }

    apdu::GetRequest request() {
        std::optional<Result<Bytes>> bytes;
        channels.second->async_read([&](auto r) { bytes = std::move(r); });
        executor->run_ready();
        CHECK(bytes && *bytes);
        auto frame = link::decode_frame(bytes->value());
        CHECK(frame);
        return std::get<apdu::GetRequest>(apdu::decode_apdu(frame.value().payload).value());
    }

    void send(apdu::Apdu message, std::uint8_t control = 0xc3, std::uint8_t ca = 0) {
        link::Frame frame;
        frame.control = control;
        frame.client = ca;
        frame.payload = apdu::encode_apdu(message).value();
        auto bytes = link::encode_frame(frame);
        CHECK(bytes);
        channels.second->async_write(std::move(bytes).value(), [](auto) {});
        executor->run_ready();
    }
};

void matching_and_close() {
    PeerHarness h;
    std::optional<Result<apdu::GetResponse>> first, busy;
    int completed = 0, diagnostics = 0;
    h.client->set_diagnostic_handler([&](const Error&) { ++diagnostics; });
    h.client->async_get({{0x2000, 2, 0}}, false, [&](auto r) {
        first = std::move(r);
        ++completed;
    });
    h.client->async_get({{0x2000, 2, 0}}, false, [&](auto r) { busy = std::move(r); });
    h.executor->run_ready();
    CHECK(busy->error().code == ErrorCode::busy);
    CHECK(!first);
    const auto request = h.request();
    apdu::GetResponse response{
        request.piid, false, {{{0x2000, 2, 0}, model::Data{model::UInt16{2413}}}}, {}};
    h.send(response, 0x43);
    CHECK(!first);
    h.send(response, 0xc3, 1);
    CHECK(!first);
    auto wrong = response;
    wrong.piid_acd = 63;
    h.send(wrong);
    CHECK(!first);
    wrong = response;
    wrong.attributes[0].attribute.index = 1;
    h.send(wrong);
    CHECK(!first);
    wrong = response;
    wrong.list = true;
    h.send(wrong);
    CHECK(!first);
    h.send(response);
    CHECK(first && *first);
    CHECK(completed == 1);
    h.send(response);
    CHECK(completed == 1);
    CHECK(diagnostics == 6);
    first.reset();
    h.client->async_get({{0x2000, 2, 0}}, false, [&](auto r) {
        first = std::move(r);
        ++completed;
    });
    h.executor->run_ready();
    h.request();
    h.send(apdu::ErrorResponse{true, 1, 2, apdu::TimeTag{}});
    CHECK(!first);
    h.send(apdu::ErrorResponse{true, 1, 2, {}});
    CHECK(first->error().code == ErrorCode::remote_error);
    CHECK(first->error().remote_code == 2);
    CHECK(completed == 2);
    first.reset();
    h.client->async_get({{0x2000, 2, 0}}, false, [&](auto r) {
        first = std::move(r);
        ++completed;
    });
    h.executor->run_ready();
    h.client->close();
    h.client->close();
    h.executor->run_ready();
    CHECK(first->error().code == ErrorCode::closed);
    CHECK(completed == 3);
}

void timeouts_cancel_and_lifetime() {
    SessionOptions options;
    options.request_timeout = 10ms;
    options.id_reuse_delay = 20ms;
    PeerHarness timeout(options);
    int callbacks = 0;
    std::optional<Result<apdu::GetResponse>> result;
    timeout.client->async_get({{0x2000, 2, 0}}, false, [&](auto r) {
        result = std::move(r);
        ++callbacks;
    });
    timeout.executor->run_ready();
    const auto request = timeout.request();
    timeout.executor->advance(9ms);
    CHECK(!result);
    timeout.executor->advance(1ms);
    CHECK(result->error().code == ErrorCode::timeout);
    CHECK(timeout.client->state() == State::closed);
    timeout.send(apdu::GetResponse{request.piid, false, {{{0x2000, 2, 0}, std::uint8_t{4}}}, {}});
    CHECK(callbacks == 1);
    PeerHarness cancelled(options);
    result.reset();
    callbacks = 0;
    cancelled.client->async_get({{0x2000, 2, 0}}, false, [&](auto r) {
        result = std::move(r);
        ++callbacks;
    });
    cancelled.executor->run_ready();
    cancelled.client->cancel();
    cancelled.client->cancel();
    cancelled.executor->run_ready();
    CHECK(result->error().code == ErrorCode::cancelled);
    CHECK(callbacks == 1);
    cancelled.executor->advance(100ms);
    CHECK(callbacks == 1);
    PeerHarness destroyed(options);
    result.reset();
    callbacks = 0;
    destroyed.client->async_get({{0x2000, 2, 0}}, false, [&](auto r) {
        result = std::move(r);
        ++callbacks;
    });
    destroyed.executor->run_ready();
    destroyed.client.reset();
    destroyed.executor->run_ready();
    CHECK(result->error().code == ErrorCode::closed);
    CHECK(callbacks == 1);
    // RELEASE 可打断在途 GET，其后迟到的 GET 响应必须被忽略。
    PeerHarness release(options);
    result.reset();
    std::optional<Result<void>> released;
    release.client->async_get({{0x2000, 2, 0}}, false, [&](auto r) { result = std::move(r); });
    release.executor->run_ready();
    release.request();
    release.client->async_release([&](auto r) { released = std::move(r); });
    release.executor->run_ready();
    CHECK(result->error().code == ErrorCode::cancelled);
    CHECK(release.client->state() == State::releasing);
    release.send(apdu::ReleaseResponse{1, 0, apdu::TimeTag{}});
    CHECK(!released);
    CHECK(release.client->state() == State::releasing);
    release.send(apdu::ReleaseResponse{1, 0, {}});
    CHECK(released && *released);
    CHECK(release.client->state() == State::preconnected);
    SessionOptions idle;
    idle.parameters.timeout_seconds = 1;
    Harness h(idle, idle);
    CHECK(h.connect().result == 0);
    h.executor->advance(1s);
    CHECK(h.client->state() == State::preconnected);
    CHECK(h.server->state() == State::preconnected);
}

void authentication_and_agreement_rejection() {
    auto executor = std::make_shared<ManualExecutor>();
    auto channels = MemoryChannel::pair(executor);
    SessionOptions options;
    options.role = Role::server;
    auto server = std::make_shared<Session>(channels.second, executor, options);
    server->start();
    executor->run_ready();
    std::optional<Result<Bytes>> reply;
    channels.first->async_read([&](auto r) { reply = std::move(r); });
    apdu::ConnectRequest request;
    request.piid = 9;
    request.parameters.protocol.fill(255);
    request.parameters.function.fill(255);
    request.mechanism = apdu::PasswordSecurity{"pass"};
    link::Frame frame;
    frame.payload = apdu::encode_apdu(request).value();
    auto bytes = link::encode_frame(frame);
    CHECK(bytes);
    channels.first->async_write(std::move(bytes).value(), [](auto) {});
    executor->run_ready();
    CHECK(reply && *reply);
    auto received = link::decode_frame(reply->value());
    CHECK(received);
    auto response =
        std::get<apdu::ConnectResponse>(apdu::decode_apdu(received.value().payload).value());
    CHECK(response.piid_acd == 9);
    CHECK(response.result == 255);
    CHECK(!response.security);
    CHECK(server->state() == State::preconnected);
    server->close();
    executor->run_ready();

    // 对端伪造成功但增加未请求的能力时，客户机必须拒绝，不能静默降低校验要求。
    for (unsigned invalid = 0; invalid < 4; ++invalid) {
        auto pair = MemoryChannel::pair(executor);
        auto client = std::make_shared<Session>(pair.first, executor);
        client->start();
        std::optional<Result<apdu::ConnectResponse>> result;
        client->async_connect([&](auto r) { result = std::move(r); });
        executor->run_ready();
        link::Frame old_notification;
        old_notification.control = 0x83;
        old_notification.payload = apdu::encode_apdu(apdu::ReleaseNotification{}).value();
        auto notification_bytes = link::encode_frame(old_notification);
        CHECK(notification_bytes);
        pair.second->async_write(std::move(notification_bytes).value(), [](auto) {});
        executor->run_ready();
        CHECK(!result);
        CHECK(client->state() == State::associating);
        apdu::ConnectResponse lie;
        lie.parameters.protocol[0] = 0xe0;
        if (invalid == 0) lie.parameters.protocol[1] = 1;
        if (invalid == 1) lie.parameters.send_frame_bytes = 2048;
        if (invalid == 2) lie.security = apdu::SecurityData{{1}, {2}};
        if (invalid == 3) lie.parameters.receive_window = 2;
        link::Frame answer;
        answer.control = 0xc3;
        answer.payload = apdu::encode_apdu(lie).value();
        auto encoded = link::encode_frame(answer);
        CHECK(encoded);
        pair.second->async_write(std::move(encoded).value(), [](auto) {});
        executor->run_ready();
        CHECK(result && !*result);
        CHECK(result->error().code == ErrorCode::association_failed);
        CHECK(client->state() == State::closed);
        client.reset();
        executor->run_ready();
    }
}

void quarantine_and_resources() {
    SessionOptions options;
    options.parameters.timeout_seconds = 1000;
    options.request_timeout = 5ms;
    options.id_reuse_delay = 50ms;
    Harness h(options, options);
    CHECK(h.connect().result == 0);
    int success = 0;
    std::optional<Result<apdu::GetResponse>> last;
    for (unsigned n = 0; n < 64; ++n) {
        h.client->async_get({{0x2000, 2, 1}}, false, [&](auto r) {
            if (r) ++success;
            last = std::move(r);
        });
        h.executor->run_ready();
    }
    CHECK(success == 63);
    CHECK(last->error().code == ErrorCode::resource_limit);
    h.executor->advance(50ms);
    h.client->async_get({{0x2000, 2, 1}}, false, [&](auto r) { last = std::move(r); });
    h.executor->run_ready();
    CHECK(*last);
    h.client->async_get({}, false, [&](auto r) { last = std::move(r); });
    h.executor->run_ready();
    CHECK(last->error().code == ErrorCode::invalid_length);
    CHECK(h.client->state() == State::associated);
    SessionOptions small;
    small.parameters.apdu_bytes = 80;
    Harness bounded(small, small);
    CHECK(bounded.connect().result == 0);
    bounded.voltage->set(2, model::Array{std::vector<model::Data>(100, model::UInt16{2413})});
    bounded.client->async_get({{0x2000, 2, 0}}, false, [&](auto r) { last = std::move(r); });
    bounded.executor->run_ready();
    CHECK(last && !*last);
    CHECK(bounded.server->state() == State::closed);
}

int main() {
    return tests([] {
        executor_and_memory();
        queued_lifetime_and_clock_failure();
        object_access();
        link_and_negotiation();
        matching_and_close();
        timeouts_cancel_and_lifetime();
        authentication_and_agreement_rejection();
        quarantine_and_resources();
    });
}
