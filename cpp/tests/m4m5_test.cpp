#include <dlt698/dlt698.hpp>
#include <dlt698/service/sync.hpp>
#include <dlt698/transport/memory.hpp>
#include <fstream>

#include "test.hpp"

using namespace dlt698;
using namespace dlt698::model;
using namespace dlt698::protocol;
using namespace dlt698::session;
using namespace dlt698::service;
using namespace dlt698::transport;
using namespace std::chrono_literals;

struct InspectChannel : IChannel {
    std::shared_ptr<IChannel> raw;
    std::shared_ptr<IExecutor> ex;
    std::function<bool(const link::Frame&)> drop;
    std::function<bool(const link::Frame&)> duplicate;
    std::vector<link::Frame> frames;

    InspectChannel(std::shared_ptr<IChannel> r, std::shared_ptr<IExecutor> e)
        : raw(std::move(r)), ex(std::move(e)) {}

    void async_read(ReadHandler h) override { raw->async_read(std::move(h)); }

    void async_write(Bytes b, WriteHandler h) override {
        auto f = link::decode_frame(b);
        CHECK(f);
        frames.push_back(f.value());
        if (drop && drop(f.value())) {
            ex->post([h = std::move(h)] {
                if (h) h(Result<void>{});
            });
            return;
        }
        if (duplicate && duplicate(f.value())) raw->async_write(b, [](auto) {});
        raw->async_write(std::move(b), std::move(h));
    }

    void close() override { raw->close(); }
};

struct Pair {
    std::shared_ptr<ManualExecutor> ex = std::make_shared<ManualExecutor>();
    std::pair<std::shared_ptr<MemoryChannel>, std::shared_ptr<MemoryChannel>> raw =
        MemoryChannel::pair(ex, {7, 1048576});
    std::shared_ptr<InspectChannel> a = std::make_shared<InspectChannel>(raw.first, ex),
                                    b = std::make_shared<InspectChannel>(raw.second, ex);
    std::shared_ptr<Session> client, server;
    std::shared_ptr<ObjectRegistry> registry = std::make_shared<ObjectRegistry>();
    std::shared_ptr<MemoryObject> object = std::make_shared<MemoryObject>();
    std::unique_ptr<SyncClientService> sync;

    Pair(SessionOptions c = {}, SessionOptions s = {}) {
        c.role = Role::client;
        s.role = Role::server;
        client = std::make_shared<Session>(a, ex, c);
        server = std::make_shared<Session>(b, ex, s);
        CHECK(registry->register_object(
            {0x2000,
             "模拟记录",
             {{2, DataType::octet_string, true, true}, {3, DataType::null, true, false, true}}},
            object));
        ServerService service(server, registry);
        client->start();
        server->start();
        ex->run_ready();
        sync = std::make_unique<SyncClientService>(client, [this](auto) { ex->advance(1ms); });
    }

    ~Pair() {
        client->close();
        server->close();
        ex->run_ready();
    }
};

void fragment_vectors_and_limits() {
    auto decode = [](Bytes b) { return link::decode_fragment(b); };
    CHECK(link::encode_fragment({link::FragmentType::first, 0, {5, 1}}).value() ==
          hex("00 00 05 01"));
    CHECK(link::encode_fragment({link::FragmentType::middle, 4095, {1}}).value() ==
          hex("FF CF 01"));
    CHECK(link::encode_fragment({link::FragmentType::last, 1, {0}}).value() == hex("01 40 00"));
    CHECK(link::encode_fragment({link::FragmentType::acknowledgement, 4095, {}}).value() ==
          hex("FF 8F"));
    CHECK(!decode(hex("00 10 01")));
    CHECK(!decode(hex("00 80 01")));
    CHECK(!decode(hex("00 00")));
    CHECK(!link::encode_fragment({link::FragmentType::first, 4096, {1}}));
    link::LinkReassembler receiver(8);
    auto first = receiver.accept({link::FragmentType::first, 0, {1, 2}});
    CHECK(first && first.value().acknowledge == 0);
    CHECK(receiver.accept({link::FragmentType::first, 0, {1, 2}}).value().duplicate);
    CHECK(!receiver.accept({link::FragmentType::middle, 2, {3}}));
    CHECK(receiver.accept({link::FragmentType::middle, 1, {3}}));
    auto end = receiver.accept({link::FragmentType::last, 2, {4}});
    CHECK(end.value().apdu == Bytes({1, 2, 3, 4}));
    CHECK(!end.value().acknowledge);
    CHECK(receiver.accept({link::FragmentType::last, 2, {4}}).value().duplicate);
    receiver.reset();
    CHECK(!receiver.accept({link::FragmentType::last, 0, {1}}));
    CHECK(receiver.accept({link::FragmentType::first, 0, Bytes(8)}));
    CHECK(receiver.accept({link::FragmentType::last, 1, {1}}).error().code ==
          ErrorCode::resource_limit);
    CHECK(!receiver.active());
    // 4095 -> 0 的序号回绕不能覆盖总进度或把中间帧当起始帧。
    Bytes value(4100);
    for (std::size_t i = 0; i < value.size(); ++i) value[i] = static_cast<std::uint8_t>(i);
    link::LinkFragmenter sender(value, 1, 5000);
    link::LinkReassembler wrapped(5000);
    for (std::size_t i = 0; i < value.size(); ++i) {
        const auto f = sender.current();
        CHECK(f.sequence == (i & 4095));
        auto result = wrapped.accept(f);
        CHECK(result);
        if (i + 1 == value.size())
            CHECK(result.value().apdu == value);
        else {
            CHECK(!sender.acknowledge(static_cast<std::uint16_t>((f.sequence + 1) & 4095)));
            CHECK(sender.acknowledge(f.sequence));
        }
    }
    link::Frame sc;
    sc.control = 0x6b;
    sc.payload = hex("01 C0 68 16 FE");
    auto wire = link::encode_frame(sc);
    CHECK(wire);
    CHECK(link::decode_frame(wire.value()).value().payload == sc.payload);
}

void record_and_selector_vectors() {
    auto vector_bytes = [](const char* name) {
        std::ifstream input(std::string(DLT698_VECTOR_DIR) + "/" + name);
        CHECK(input);
        const std::string text((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
        auto bytes = from_hex(text);
        CHECK(bytes);
        return std::move(bytes).value();
    };
    for (const auto name :
         {"get_record_request.hex", "get_record_response.hex", "get_record_meters_request.hex"}) {
        const auto bytes = vector_bytes(name);
        const auto value = apdu::decode_get(bytes);
        CHECK(value);
        CHECK(apdu::encode_get(value.value()).value() == bytes);
    }
    const auto frozen_bytes = vector_bytes("get_record_response.hex");
    const auto frozen = apdu::decode_get(frozen_bytes);
    const auto& frozen_row = std::get<std::vector<apdu::RecordRow>>(
        std::get<apdu::GetRecordResponse>(frozen.value()).records[0].result)[0];
    CHECK(frozen_row.size() == 2 && frozen_row[0].type() == DataType::date_time_s &&
          frozen_row[1].as<Array>().value.size() == 5);
    const auto meters_request = vector_bytes("get_record_meters_request.hex");
    const auto decoded_meters = apdu::decode_get(meters_request);
    const auto& query = std::get<apdu::GetRecordRequest>(decoded_meters.value()).records[0];
    CHECK(query.rows.index() == 5 &&
          std::get<MeterAddresses>(std::get<Selector5>(query.rows).meters).values.size() == 5);
    CHECK(query.columns.size() == 5 && std::get<Road>(query.columns[4]).associated.size() == 2);
    // 表 67/74 的独立字节：Selector9 一字节次数；每行只按 RCSD 列数读取 Data。
    apdu::GetRecordRequest request{
        1, false, {{{0x6012, 2, 0}, Selector9{2}, {Oad{0x2021, 2, 0}}}}, {}};
    CHECK(apdu::encode_get(apdu::GetApdu{request}).value() ==
          hex("05 03 01 60 12 02 00 09 02 01 00 20 21 02 00 00"));
    apdu::GetRecordResponse response{1,
                                     false,
                                     {{{0x6012, 2, 0},
                                       {Oad{0x2021, 2, 0}},
                                       std::vector<apdu::RecordRow>{{UInt16{42}}, {Enum{1}}}}},
                                     {}};
    const auto expected = hex("85 03 01 60 12 02 00 01 00 20 21 02 00 01 02 12 00 2A 16 01 00 00");
    CHECK(apdu::encode_get(apdu::GetApdu{response}).value() == expected);
    const auto decoded = apdu::decode_get(expected);
    CHECK(decoded);
    const auto& rows = std::get<std::vector<apdu::RecordRow>>(
        std::get<apdu::GetRecordResponse>(decoded.value()).records[0].result);
    CHECK(rows[0][0].type() == DataType::uint16 && rows[1][0].type() == DataType::enumeration);
    for (std::size_t n = 0; n < expected.size(); ++n)
        CHECK(!apdu::decode_get(ByteView(expected.data(), n)));
    auto trailing = expected;
    trailing.push_back(0);
    CHECK(!apdu::decode_get(trailing));
    response.records[0].result = std::vector<apdu::RecordRow>{{UInt16{1}, UInt16{2}}};
    CHECK(!apdu::encode_get(apdu::GetApdu{response}));
    CHECK(apdu::encode_get(apdu::GetApdu{apdu::GetNextRequest{2, 0x1234, {}}}).value() ==
          hex("05 05 02 12 34 00"));
    CHECK(apdu::encode_get(
              apdu::GetApdu{apdu::GetNextResponse{2, true, 0x1234, std::uint8_t{11}, {}}})
              .value() == hex("85 05 02 01 12 34 00 0B 00 00"));
    DateTimeS date{{7, 0xe8, 2, 29, 12, 0, 0}};
    std::vector<Ms> meters{NoMeters{},
                           AllMeters{},
                           MeterTypes{{1, 2}},
                           MeterAddresses{{Tsa{{0, 1}}}},
                           MeterNumbers{{1, 65535}},
                           MeterTypeRegions{{Region{0, UInt8{1}, UInt8{2}}}},
                           MeterAddressRegions{{Region{2, Tsa{{0, 1}}, Tsa{{0, 2}}}}},
                           MeterNumberRegions{{Region{3, UInt16{1}, UInt16{2}}}}};
    for (const auto& m : meters) {
        Writer w(1000);
        codec::write_ms(w, m);
        const auto bytes = w.take();
        CHECK(bytes.front() == m.index());
        Reader r(bytes);
        const auto back = codec::read_ms(r);
        r.finish();
        Writer out(1000);
        codec::write_ms(out, back);
        CHECK(out.take() == bytes);
    }
    std::vector<Rsd> selectors{
        SelectAll{},
        Selector1{{0x2021, 2, 0}, UInt16{1}},
        Selector2{{0x2021, 2, 0}, UInt16{1}, UInt16{2}, Null{}},
        Selector3{{Selector2{{0x2021, 2, 0}, UInt16{1}, UInt16{2}, UInt16{1}}}},
        Selector4{date, meters[3]},
        Selector5{date, meters[2]},
        Selector6{date, date, {0, 1}, meters[7]},
        Selector7{date, date, {1, 1}, meters[6]},
        Selector8{date, date, {2, 1}, meters[5]},
        Selector9{1},
        Selector10{10, meters[1]}};
    for (const auto& rsd : selectors) {
        Writer w(1000);
        codec::write_rsd(w, rsd);
        const auto bytes = w.take();
        CHECK(bytes[0] == rsd.index());
        Reader r(bytes);
        const auto back = codec::read_rsd(r);
        r.finish();
        Writer out(1000);
        codec::write_rsd(out, back);
        CHECK(out.take() == bytes);
        for (std::size_t n = 0; n < bytes.size(); ++n) {
            bool failed = false;
            try {
                Reader short_r(ByteView(bytes.data(), n));
                codec::read_rsd(short_r);
            } catch (const DecodeFailure&) {
                failed = true;
            }
            CHECK(failed);
        }
    }
    Writer w(100);
    codec::write_rcsd(w, {Road{{0x6012, 2, 0}, {{0x2021, 2, 0}}}});
    CHECK(w.take() == hex("01 01 60 12 02 00 01 20 21 02 00"));
    bool failed = false;
    try {
        Writer out(100);
        codec::write_ms(out, MeterTypeRegions{{Region{4, UInt16{1}, UInt16{2}}}});
    } catch (const DecodeFailure&) {
        failed = true;
    }
    CHECK(failed);

    // 六类描述符作为带标签 Data 使用，字节预期独立于往返路径。
    std::vector<std::pair<Data, Bytes>> data_vectors{
        {RecordData{Road{{0x6012, 2, 0}, {{0x2021, 2, 0}}}}, hex("52 60 12 02 00 01 20 21 02 00")},
        {RecordData{Region{0, UInt8{1}, UInt8{2}}}, hex("58 00 11 01 11 02")},
        {RecordData{Rsd{Selector9{2}}}, hex("5A 09 02")},
        {RecordData{Csd{Oad{0x2021, 2, 0}}}, hex("5B 00 20 21 02 00")},
        {RecordData{Ms{MeterTypes{{1, 2}}}}, hex("5C 02 02 01 02")},
        {RecordData{Rcsd{Oad{0x2021, 2, 0}}}, hex("60 01 00 20 21 02 00")}};
    for (const auto& vector : data_vectors) {
        CHECK(codec::encode_data(vector.first).value() == vector.second);
        const auto value = codec::decode_data(vector.second);
        CHECK(value && value.value() == vector.first);
        for (std::size_t n = 0; n < vector.second.size(); ++n)
            CHECK(!codec::decode_data(ByteView(vector.second.data(), n)));
    }
    for (const auto& rsd : selectors) {
        Data value = RecordData{rsd};
        auto bytes = codec::encode_data(value);
        CHECK(bytes);
        CHECK(codec::decode_data(bytes.value()).value() == value);
    }
    for (const auto& ms : meters) {
        Data value = RecordData{ms};
        auto bytes = codec::encode_data(value);
        CHECK(bytes);
        CHECK(codec::decode_data(bytes.value()).value() == value);
    }
    Rsd mutable_selector = Selector1{{0x2000, 2, 0}, UInt16{1}};
    Data immutable = RecordData{mutable_selector};
    std::get<Selector1>(mutable_selector).value = UInt16{2};
    CHECK(std::get<Selector1>(immutable.as<RecordData>().as<Rsd>()).value == Data{UInt16{1}});
    Data nested = RecordData{Region{0, Array{{UInt8{1}, UInt8{2}}}, Null{}}};
    auto encoded = codec::encode_data(nested);
    CHECK(encoded);
    Limits nodes;
    nodes.max_elements = 4;
    CHECK(!codec::encode_data(nested, nodes));
    CHECK(!codec::decode_data(encoded.value(), nodes));
    Limits depth;
    depth.max_depth = 1;
    CHECK(!codec::encode_data(nested, depth));
    CHECK(!codec::decode_data(encoded.value(), depth));
    nodes.max_elements = 5;
    CHECK(codec::encode_data(nested, nodes));
    CHECK(codec::decode_data(encoded.value(), nodes));
    // RSD -> Region -> RSD 递归必须继续扣减同一深度，不能反复重置预算。
    Data recursive = Null{};
    for (unsigned i = 0; i < 40; ++i)
        recursive = RecordData{Rsd{Selector1{{0x2000, 2, 0}, std::move(recursive)}}};
    CHECK(!codec::encode_data(recursive));
    Limits deep;
    deep.max_depth = 50;
    const auto deep_wire = codec::encode_data(recursive, deep);
    CHECK(deep_wire);
    CHECK(!codec::decode_data(deep_wire.value()));
}

void blocks_and_composed_sessions() {
    apdu::GetResponse snapshot{5, true, {}, {}};
    for (unsigned n = 0; n < 20; ++n)
        snapshot.attributes.push_back(
            {{0x2000, 2, static_cast<std::uint8_t>(n)}, UInt16{static_cast<std::uint16_t>(n)}});
    auto blocks = apdu::GetBlockTransfer::split(snapshot, 45, {});
    CHECK(blocks && blocks.value().size() > 1);
    CHECK(blocks.value().front().block == 0 && !blocks.value().front().last);
    apdu::GetBlockTransfer collector(5, false, {});
    CHECK(!collector.accept(blocks.value()[1]));
    CHECK(collector.accept(blocks.value()[0]));
    CHECK(!collector.accept(blocks.value()[0]));
    std::optional<apdu::GetSnapshot> full;
    for (std::size_t i = 1; i < blocks.value().size(); ++i) {
        auto r = collector.accept(blocks.value()[i]);
        CHECK(r);
        full = std::move(r).value();
    }
    CHECK(full && std::get<apdu::GetResponse>(*full).attributes.size() == 20);
    apdu::GetBlockTransfer failure(5, false, {});
    CHECK(failure.accept({5, true, 0, std::uint8_t{10}, {}}).error().remote_code == 10);
    Limits limit;
    limit.max_data_bytes = 20;
    apdu::GetBlockTransfer small(5, false, limit);
    CHECK(!small.accept(blocks.value()[0]));

    SessionOptions options;
    options.parameters.send_frame_bytes = 100;
    options.parameters.receive_frame_bytes = 100;
    options.parameters.apdu_bytes = 512;
    options.fragment_timeout = 20ms;
    Pair pair(options, options);
    CHECK(pair.sync->connect().value().result == 0);
    pair.object->set(2, OctetString{Bytes(250, 42)});
    unsigned record_calls = 0;
    pair.object->bind_record(3, [&](const apdu::GetRecord& q) {
        ++record_calls;
        return apdu::RecordResult{q.attribute,
                                  {Oad{0x2000, 2, 0}, Oad{0x2021, 2, 0}},
                                  std::vector<apdu::RecordRow>{
                                      {UInt16{42}, Enum{1}}, {OctetString{Bytes(150, 7)}, Null{}}}};
    });
    std::vector<Oad> attrs(3, Oad{0x2000, 2, 0});
    auto got = pair.sync->get_list(attrs);
    CHECK(got && got.value().attributes.size() == 3);
    unsigned fragment_count = 0, block_count = 0;
    for (const auto& f : pair.b->frames) {
        if (f.control & 0x20)
            ++fragment_count;
        else if (f.payload.size() > 1 && f.payload[0] == 0x85 && f.payload[1] == 5)
            ++block_count;
        CHECK(f.payload.size() + 9 + f.server.bytes.size() <= 100);
    }
    CHECK(fragment_count > 3);  // 每个 GET Next 块本身超帧长，组合链路分帧。
    unsigned next_requests = 0;
    for (const auto& f : pair.a->frames)
        if (f.payload.size() > 1 && !(f.control & 0x20) && f.payload[0] == 5 && f.payload[1] == 5)
            ++next_requests;
    CHECK(next_requests == 2);
    std::vector<apdu::GetRecord> queries(2, apdu::GetRecord{{0x2000, 3, 0}, SelectAll{}, {}});
    auto record = pair.sync->get_record_list(queries);
    CHECK(record && record.value().records.size() == 2 && record_calls == 2);
    CHECK(std::get<std::vector<apdu::RecordRow>>(record.value().records[0].result)[0][1].type() ==
          DataType::enumeration);
    CHECK(pair.sync->release());
    (void)block_count;
}

void loss_and_duplicate_sessions() {
    SessionOptions opt;
    opt.preset_association = true;
    opt.prefer_get_blocks = false;
    opt.parameters.send_frame_bytes = 100;
    opt.parameters.receive_frame_bytes = 100;
    opt.parameters.apdu_bytes = 512;
    opt.fragment_timeout = 10ms;
    opt.request_timeout = 100ms;
    opt.id_reuse_delay = 100ms;
    Pair pair(opt, opt);
    pair.object->set(2, OctetString{Bytes(250, 7)});
    bool lost = false, duplicated = false;
    pair.a->drop = [&](const link::Frame& f) {
        if (f.control & 0x20) {
            auto p = link::decode_fragment(f.payload);
            if (p && p.value().type == link::FragmentType::acknowledgement && !lost) {
                lost = true;
                return true;
            }
        }
        return false;
    };
    pair.b->duplicate = [&](const link::Frame& f) {
        if ((f.control & 0x20) && !duplicated) {
            duplicated = true;
            return true;
        }
        return false;
    };
    auto result = pair.sync->get({0x2000, 2, 0});
    CHECK(result && lost && duplicated);
    CHECK(std::get<Data>(result.value()).as<OctetString>().value == Bytes(250, 7));
    // 所有确认丢失时，发送方在有限次重发后关闭，两端无永久缓冲或悬挂回调。
    pair.a->drop = [](const link::Frame& f) { return (f.control & 0x20) != 0; };
    auto timeout = pair.sync->get({0x2000, 2, 0});
    CHECK(!timeout);
    CHECK(pair.server->state() == State::closed);
    pair.ex->run_ready();
    CHECK(pair.client->state() == State::closed);
}

void calendar_and_heartbeat() {
    apdu::TimeTag tag{DateTimeS{{7, 0xe8, 2, 29, 12, 0, 0}}, Ti{0, 1}};
    CHECK(apdu::valid_time_tag(tag, DateTimeS{{7, 0xe8, 2, 29, 12, 0, 1}}).value());
    CHECK(!apdu::valid_time_tag(tag, DateTimeS{{7, 0xe8, 2, 29, 12, 0, 2}}).value());
    tag.allowed_delay = {5, 1};
    CHECK(apdu::valid_time_tag(tag, DateTimeS{{7, 0xe9, 2, 28, 12, 0, 0}}).value());
    CHECK(!apdu::valid_time_tag(tag, DateTimeS{{7, 0xe9, 3, 1, 12, 0, 0}}).value());
    tag.allowed_delay = {4, 1};
    tag.sent_at = DateTimeS{{7, 0xe8, 1, 31, 0, 0, 0}};
    CHECK(apdu::valid_time_tag(tag, DateTimeS{{7, 0xe8, 2, 29, 0, 0, 0}}).value());
    CHECK(!apdu::valid_time_tag(tag, DateTimeS{{7, 0xe7, 2, 29, 0, 0, 0}}));
    tag.allowed_delay = {0, 0};
    CHECK(apdu::valid_time_tag(tag, DateTimeS{{7, 0xe8, 12, 1, 0, 0, 0}}).value());
    SessionOptions c, s;
    c.calendar_clock = [] { return DateTime{{7, 0xe8, 1, 1, 1, 0, 0, 0, 0, 0}}; };
    s.calendar_clock = c.calendar_clock;
    c.request_time_tag = Ti{0, 5};
    s.heartbeat_seconds = 1;
    Pair pair(c, s);
    pair.object->set(2, OctetString{{42}});
    CHECK(pair.sync->connect());
    CHECK(pair.sync->set({0x2000, 2, 0}, OctetString{{7}}).value() == 0);
    CHECK(pair.sync->get({0x2000, 2, 0}));
    pair.ex->advance(1s);
    pair.ex->advance(1s);
    unsigned heartbeat = 0;
    for (const auto& f : pair.b->frames)
        if (f.payload.size() > 2 && f.payload[0] == 1 && f.payload[2] == 1) ++heartbeat;
    CHECK(heartbeat == 2);
    // 服务端晚十秒：有副作用请求被丢弃，旧值不变，客户机最终收到超时。
    c.preset_association = true;
    s.preset_association = true;
    c.request_timeout = 30ms;
    c.id_reuse_delay = 30ms;
    s.calendar_clock = [] { return DateTime{{7, 0xe8, 1, 1, 1, 0, 0, 10, 0, 0}}; };
    s.heartbeat_seconds = 0;
    Pair stale(c, s);
    stale.object->set(2, OctetString{{42}});
    auto result = stale.sync->set({0x2000, 2, 0}, OctetString{{1}});
    CHECK(result.error().code == ErrorCode::timeout);
    CHECK(std::get<Data>(stale.object->read({0x2000, 2, 0})).as<OctetString>().value == Bytes{42});
    pair.server->close();
    pair.ex->run_ready();
    const auto frames = pair.b->frames.size();
    pair.ex->advance(10s);
    CHECK(pair.b->frames.size() == frames);
}

void server_block_cancellation_and_timers() {
    SessionOptions opt;
    opt.reassembly_timeout = 100ms;
    opt.preset_association = true;
    opt.parameters.send_frame_bytes = 100;
    opt.parameters.receive_frame_bytes = 100;
    opt.parameters.apdu_bytes = 512;
    opt.request_timeout = 100ms;
    opt.id_reuse_delay = 100ms;
    Pair pair(opt, opt);
    pair.object->set(2, OctetString{Bytes(80, 7)});
    auto send = [&](apdu::Apdu message) {
        link::Frame frame;
        auto bytes = apdu::encode_apdu(message);
        CHECK(bytes);
        frame.payload = std::move(bytes).value();
        auto wire = link::encode_frame(frame);
        CHECK(wire);
        pair.a->async_write(std::move(wire).value(), [](auto) {});
        pair.ex->run_ready();
    };
    auto last = [&] {
        CHECK(!pair.b->frames.empty());
        auto value = apdu::decode_apdu(pair.b->frames.back().payload);
        CHECK(value);
        return std::get<apdu::GetNextResponse>(value.value());
    };
    send(apdu::GetNextRequest{42, 17, {}});
    CHECK(last().last && last().block == 17 && std::get<std::uint8_t>(last().result) == 11);
    send(apdu::GetRequest{42, true, {{0x2000, 2, 0}, {0x2000, 2, 0}, {0x2000, 2, 0}}, {}});
    send(apdu::GetNextRequest{42, 2, {}});
    CHECK(last().block == 2 && std::get<std::uint8_t>(last().result) == 10);
    send(apdu::GetNextRequest{42, 0, {}});
    CHECK(std::get<std::uint8_t>(last().result) == 11);
    send(apdu::GetRequest{42, true, {{0x2000, 2, 0}, {0x2000, 2, 0}, {0x2000, 2, 0}}, {}});
    pair.ex->advance(100ms);
    send(apdu::GetNextRequest{42, 0, {}});
    CHECK(std::get<std::uint8_t>(last().result) == 11);
    // 未完成重组在接收超时后关闭通道，不能污染下一次会话。
    link::Frame frame;
    frame.control = 0x63;
    frame.payload = link::encode_fragment({link::FragmentType::first, 0, {5, 1}}).value();
    auto bytes = link::encode_frame(frame);
    CHECK(bytes);
    pair.a->async_write(std::move(bytes).value(), [](auto) {});
    pair.ex->run_ready();
    pair.ex->advance(100ms);
    CHECK(pair.server->state() == State::closed);

    SessionOptions heartbeat;
    heartbeat.heartbeat_seconds = 1;
    heartbeat.request_timeout = 30ms;
    heartbeat.id_reuse_delay = 30ms;
    Pair missing({}, heartbeat);
    missing.a->drop = [](const link::Frame& f) { return (f.control & 7) == 1; };
    missing.ex->advance(1s);
    missing.ex->advance(30ms);
    CHECK(missing.server->state() == State::closed);
    CHECK(missing.client->state() == State::closed);
}

int main() {
    return tests([] {
        fragment_vectors_and_limits();
        record_and_selector_vectors();
        blocks_and_composed_sessions();
        loss_and_duplicate_sessions();
        calendar_and_heartbeat();
        server_block_cancellation_and_timers();
    });
}
