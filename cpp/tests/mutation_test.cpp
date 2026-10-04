#include <dlt698/service/sync.hpp>
#include <dlt698/transport/memory.hpp>
#include <fstream>
#include <iterator>

#include "test.hpp"
using namespace dlt698;
using namespace dlt698::protocol;
using namespace dlt698::service;
using namespace std::chrono_literals;

Bytes fixture(const char* name) {
    std::ifstream file(std::string(DLT698_VECTOR_DIR) + "/" + name);
    CHECK(file.good());
    return hex(std::string(std::istreambuf_iterator<char>(file), {}).c_str());
}

void expect(const char* name, apdu::MutationApdu model) {
    const auto bytes = fixture(name);
    CHECK(apdu::encode_mutation(model).value() == bytes);
    const auto decoded = apdu::decode_mutation(bytes);
    CHECK(decoded && decoded.value().index() == model.index());
    CHECK(apdu::encode_mutation(decoded.value()).value() == bytes);
    CHECK(apdu::encode_apdu(apdu::decode_apdu(bytes).value()).value() == bytes);
    for (std::size_t n = 0; n < bytes.size(); ++n)
        CHECK(apdu::decode_mutation(ByteView(bytes).subview(0, n)).error().code ==
              ErrorCode::need_more_data);
    auto extra = bytes;
    extra.push_back(0);
    CHECK(apdu::decode_mutation(extra).error().code == ErrorCode::trailing_data);
}

void codec_vectors() {
    model::Data clock = model::DateTimeS{{7, 0xe0, 1, 0x14, 0x10, 0x1b, 0x0b}};
    expect("set-normal-request.hex", apdu::SetRequest{2, false, {{{0x4000, 2, 0}, clock}}, {}});
    expect("set-normal-response.hex", apdu::SetResponse{2, false, {{{0x4000, 2, 0}, 0}}, {}});
    expect("set-list-request.hex",
           apdu::SetRequest{
               3,
               true,
               {{{0x4001, 2, 0}, model::OctetString{{0, 0, 0, 0, 0, 1}}}, {{0x4000, 2, 0}, clock}},
               {}});
    expect("set-list-response.hex",
           apdu::SetResponse{3, true, {{{0x4001, 2, 0}, 0}, {{0x4000, 2, 0}, 0}}, {}});
    expect("action-normal-request.hex",
           apdu::ActionRequest{5, false, {{{0x10, 1, 0}, model::Int8{0}}}, {}});
    expect("action-normal-response.hex",
           apdu::ActionResponse{5, false, {{{0x10, 1, 0}, 0, {}}}, {}});
    const auto list_request = hex("07 02 06 02 00 10 01 00 0F 00 00 10 02 01 00 00");
    const auto list_response = hex("87 02 06 02 00 10 01 00 00 01 00 00 10 02 01 03 00 00 00");
    CHECK(apdu::encode_mutation(
              apdu::ActionRequest{
                  6, true, {{{0x10, 1, 0}, model::Int8{0}}, {{0x10, 2, 1}, model::Null{}}}, {}})
              .value() == list_request);
    CHECK(apdu::encode_mutation(apdu::ActionResponse{6,
                                                     true,
                                                     {{{0x10, 1, 0}, 0, model::Data{model::Null{}}},
                                                      {{0x10, 2, 1}, 3, {}}},
                                                     {}})
              .value() == list_response);
    CHECK(std::get<apdu::ActionResponse>(apdu::decode_mutation(list_response).value())
              .methods[0]
              .data->type() == model::DataType::null);
    auto invalid = fixture("action-normal-response.hex");
    invalid[8] = 2;
    CHECK(apdu::decode_mutation(invalid).error().code == ErrorCode::invalid_value);
    invalid = fixture("set-normal-request.hex");
    invalid[2] = 0x40;
    CHECK(apdu::decode_mutation(invalid).error().code == ErrorCode::invalid_value);
    invalid[1] = 3;
    CHECK(apdu::decode_mutation(invalid).error().code == ErrorCode::unsupported_service);
    invalid = fixture("set-normal-response.hex");
    invalid[8] = 1;
    CHECK(apdu::decode_mutation(invalid).error().code == ErrorCode::unsupported_service);
    CHECK(!apdu::encode_mutation(apdu::SetRequest{}));
    Limits limits;
    limits.max_data_bytes = 4;
    CHECK(apdu::decode_mutation(list_request, limits).error().code == ErrorCode::resource_limit);
    limits = {};
    limits.max_elements = 1;
    CHECK(apdu::decode_mutation(list_request, limits).error().code == ErrorCode::resource_limit);
}

void services_and_sync() {
    auto executor = std::make_shared<ManualExecutor>();
    auto pair = transport::MemoryChannel::pair(executor, {1, 65536});
    session::SessionOptions options;
    options.role = session::Role::server;
    auto server = std::make_shared<session::Session>(pair.second, executor, options);
    auto client = std::make_shared<session::Session>(pair.first, executor);
    auto objects = std::make_shared<ObjectRegistry>();
    auto object = std::make_shared<MemoryObject>();
    object->set(2, model::UInt16{10});
    object->set(3, model::Array{{model::UInt16{1}, model::UInt16{2}}});
    int invocations = 0;
    object->bind_method(1,
                        [&, weak = std::weak_ptr<MemoryObject>(object)](
                            const auto&, const model::Data& parameter) -> ActionValue {
                            ++invocations;
                            const auto value = parameter.as<model::UInt16>().value;
                            weak.lock()->set(2, model::UInt16{value});
                            return {0, model::Data{model::UInt16{value}}};
                        });
    object->bind_method(
        2, [](const auto&, const auto&) -> ActionValue { throw std::runtime_error("provider"); });
    CHECK(objects->register_object({0x2000,
                                    "可写模拟对象",
                                    {{2, model::DataType::uint16, true, true},
                                     {3, model::DataType::array, true, true},
                                     {4, model::DataType::uint16, true, false}},
                                    {{1, model::DataType::uint16, model::DataType::uint16, true},
                                     {2, {}, {}, true},
                                     {3, {}, {}, false}}},
                                   object));
    ServerService service(server, objects);
    client->start();
    server->start();
    SyncClientService sync(client, [executor](auto elapsed) { executor->advance(elapsed); });
    auto associated = sync.connect();
    CHECK(associated && !associated.value().result);
    CHECK(associated.value().parameters.protocol[0] == 0xf3);
    CHECK(associated.value().parameters.protocol[1] == 0x8c);
    CHECK(sync.set({0x2000, 2, 0}, model::UInt16{25}).value() == 0);
    CHECK(std::get<model::Data>(sync.get({0x2000, 2, 0}).value()) ==
          model::Data{model::UInt16{25}});
    auto partial = sync.set_list({{{0x2000, 2, 0}, model::UInt16{30}},
                                  {{0x2000, 4, 0}, model::UInt16{1}},
                                  {{0x9999, 2, 0}, model::UInt16{1}},
                                  {{0x2000, 2, 0}, model::Int8{1}}});
    CHECK(partial);
    CHECK(partial.value().attributes[0].dar == 0);
    CHECK(partial.value().attributes[1].dar == 3);
    CHECK(partial.value().attributes[2].dar == 4);
    CHECK(partial.value().attributes[3].dar == 7);
    const auto old = std::get<model::Data>(objects->read({0x2000, 3, 0}));
    CHECK(sync.set({0x2000, 3, 2}, model::UInt16{50}).value() == 0);
    CHECK(old.as<model::Array>().value[1] == model::Data{model::UInt16{2}});
    CHECK(sync.set({0x2000, 3, 3}, model::UInt16{50}).value() == 8);
    auto action = sync.action({0x2000, 1, 0}, model::UInt16{42});
    CHECK(action && !action.value().dar && *action.value().data == model::Data{model::UInt16{42}});
    CHECK(invocations == 1);
    auto actions = sync.action_list({{{0x2000, 1, 0}, model::UInt16{43}},
                                     {{0x2000, 1, 0}, model::Int8{0}},
                                     {{0x2000, 2, 0}, model::Null{}},
                                     {{0x2000, 3, 0}, model::Null{}},
                                     {{0x2000, 9, 0}, model::Null{}},
                                     {{0x2000, 1, 1}, model::UInt16{0}}});
    CHECK(actions);
    const unsigned expected[] = {0, 7, 255, 3, 4, 3};
    for (unsigned i = 0; i < 6; ++i) CHECK(actions.value().methods[i].dar == expected[i]);
    CHECK(invocations == 2);
    std::optional<Result<ObjectValue>> reentrant;
    executor->post([&] { reentrant = sync.get({0x2000, 2, 0}); });
    executor->run_ready();
    CHECK(reentrant->error().code == ErrorCode::busy);
    CHECK(sync.release());
    CHECK(sync.connect().value().result == 0);
    client->close();
    server->close();
    executor->run_ready();

    // 外部驱动推进虚拟超时，未收到响应的写入只完成一次，不重放副作用。
    auto timeout_pair = transport::MemoryChannel::pair(executor);
    options = {};
    options.preset_association = true;
    options.request_timeout = 10ms;
    auto timeout_client = std::make_shared<session::Session>(timeout_pair.first, executor, options);
    timeout_client->start();
    SyncClientService timeout_sync(timeout_client,
                                   [executor](auto elapsed) { executor->advance(elapsed); });
    CHECK(timeout_sync.set({0x2000, 2, 0}, model::UInt16{99}).error().code == ErrorCode::timeout);
    CHECK(timeout_client->state() == session::State::closed);
}

void reply_matching_and_capabilities() {
    auto executor = std::make_shared<ManualExecutor>();
    auto pair = transport::MemoryChannel::pair(executor);
    session::SessionOptions options;
    options.preset_association = true;
    auto client = std::make_shared<session::Session>(pair.first, executor, options);
    client->start();
    std::optional<Result<apdu::ActionResponse>> result;
    std::optional<Result<apdu::SetResponse>> busy;
    client->async_action({{{0x2000, 1, 0}, model::UInt16{1}}}, false,
                         [&](auto r) { result = std::move(r); });
    client->async_set({{{0x2000, 2, 0}, model::UInt16{9}}}, false,
                      [&](auto r) { busy = std::move(r); });
    executor->run_ready();
    CHECK(busy->error().code == ErrorCode::busy);
    auto send = [&](apdu::ActionResponse response) {
        link::Frame frame;
        frame.control = 0xc3;
        frame.payload = apdu::encode_apdu(response).value();
        pair.second->async_write(link::encode_frame(frame).value(), [](auto) {});
        executor->run_ready();
    };
    send(apdu::ActionResponse{0, false, {{{0x2000, 1, 1}, 0, {}}}, {}});
    CHECK(!result);  // 模式字节不同也不能匹配。
    send(apdu::ActionResponse{0, true, {{{0x2000, 1, 0}, 0, {}}}, {}});
    CHECK(!result);
    send(apdu::ActionResponse{0, false, {{{0x2000, 1, 0}, 0, {}}}, apdu::TimeTag{}});
    CHECK(!result);
    send(apdu::ActionResponse{0, false, {{{0x2000, 1, 0}, 0, model::Data{model::Null{}}}}, {}});
    CHECK(result && *result && result->value().methods[0].data);
    client->close();
    executor->run_ready();
    auto disabled_pair = transport::MemoryChannel::pair(executor);
    options.parameters.protocol = {0xe0};
    auto disabled = std::make_shared<session::Session>(disabled_pair.first, executor, options);
    disabled->start();
    SyncClientService sync(disabled, [executor](auto elapsed) { executor->advance(elapsed); });
    CHECK(sync.set({0x2000, 2, 0}, model::UInt16{1}).error().code ==
          ErrorCode::unsupported_service);
    CHECK(sync.action({0x2000, 1, 0}, model::Null{}).error().code ==
          ErrorCode::unsupported_service);
    disabled->close();
    executor->run_ready();

    auto failed_pair = transport::MemoryChannel::pair(executor);
    options.parameters.protocol = {0xe1, 0x8c};
    auto failed = std::make_shared<session::Session>(failed_pair.first, executor, options);
    failed->start();
    SyncClientService broken_driver(failed,
                                    [](auto) { throw std::runtime_error("driver failed"); });
    CHECK(broken_driver.set({0x2000, 2, 0}, model::UInt16{1}).error().code == ErrorCode::io_error);
    executor->run_ready();
    CHECK(failed->state() == session::State::closed);
}

int main() {
    return tests([] {
        codec_vectors();
        services_and_sync();
        reply_matching_and_capabilities();
    });
}
