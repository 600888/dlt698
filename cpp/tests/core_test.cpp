#include <dlt698/dlt698.hpp>
#include <fstream>
#include <iterator>
#include <limits>

#include "test.hpp"

using namespace dlt698;
using namespace dlt698::model;
using namespace dlt698::protocol;

Bytes fixture(const char* name) {
    std::ifstream input(std::string(DLT698_VECTOR_DIR) + "/" + name);
    CHECK(input.good());
    const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    auto bytes = from_hex(text);
    CHECK(bytes);
    return std::move(bytes).value();
}

void expect_data(const char* bytes, Data data) {
    auto expected = hex(bytes);
    auto decoded = codec::decode_data(expected);
    CHECK(decoded);
    CHECK(decoded.value() == data);
    auto encoded = codec::encode_data(data);
    CHECK(encoded);
    CHECK(encoded.value() == expected);
    // 枚举每个截断位置，确认完整输入接口对半个字段返回 need_more_data。
    for (std::size_t n = 0; n < expected.size(); ++n) {
        auto cut = codec::decode_data(ByteView(expected).subview(0, n));
        CHECK(!cut);
        CHECK(cut.error().code == ErrorCode::need_more_data);
    }
}

void data_tests() {
    expect_data("00", Null{});
    expect_data("03 01", Boolean{true});
    expect_data("0F 80", Int8{-128});
    expect_data("10 80 00", Int16{-32768});
    expect_data("05 80 00 00 00", Int32{std::numeric_limits<std::int32_t>::min()});
    expect_data("14 80 00 00 00 00 00 00 00", Int64{std::numeric_limits<std::int64_t>::min()});
    expect_data("11 FF", UInt8{255});
    expect_data("12 FF FF", UInt16{65535});
    expect_data("06 FF FF FF FF", UInt32{std::numeric_limits<std::uint32_t>::max()});
    expect_data("15 FF FF FF FF FF FF FF FF", UInt64{std::numeric_limits<std::uint64_t>::max()});
    expect_data("16 02", Enum{2});
    expect_data("17 3F 80 00 00", Float32{1.0f});
    expect_data("18 C0 04 00 00 00 00 00 00", Float64{-2.5});
    expect_data("09 03 00 68 16", OctetString{hex("00 68 16")});
    expect_data("0A 03 41 42 43", VisibleString{"ABC"});
    expect_data("0C 03 E4 B8 AD", Utf8String{std::string("\xE4\xB8\xAD")});
    expect_data("04 09 80 80", BitString{9, hex("80 80")});
    expect_data("01 02 12 09 6D 12 09 6D", Array{{UInt16{2413}, UInt16{2413}}});
    expect_data("02 02 11 01 01 01 00", Structure{{UInt8{1}, Array{{Null{}}}}});
    expect_data("1C 07 E0 01 14 FF FF FF", DateTimeS{{0x07, 0xe0, 1, 20, 0xff, 0xff, 0xff}});
    expect_data("19 07 E0 01 14 03 00 00 00 00 64",
                DateTime{{0x07, 0xe0, 1, 20, 3, 0, 0, 0, 0, 100}});
    expect_data("1A FF FF FF FF FF", Date{{0xff, 0xff, 0xff, 0xff, 0xff}});
    expect_data("1B 17 3B 3B", Time{{23, 59, 59}});
    expect_data("50 40 01", Oi{0x4001});
    expect_data("51 40 01 02 00", Oad{0x4001, 2, 0});
    expect_data("53 40 00 01 00", Omd{0x4000, 1, 0});
    expect_data("54 00 00 1E", Ti{0, 30});
    expect_data("55 06 04 00 00 00 01 21", Tsa{hex("04 00 00 00 01 21")});
    expect_data("59 FF 21", ScalerUnit{-1, 33});
    for (auto bad : {"03 02", "04 01 81", "0A 01 00", "0C 02 C0 80", "0C 03 ED A0 80", "55 01 00",
                     "54 06 00 01"}) {
        auto input = hex(bad);
        auto result = codec::decode_data(input);
        CHECK(!result);
        CHECK(result.error().code == ErrorCode::invalid_value);
    }
    for (auto bad : {"09 80", "09 81 01 00", "09 82 00 80"}) {
        auto input = hex(bad);
        auto result = codec::decode_data(input);
        CHECK(!result);
        CHECK(result.error().code == ErrorCode::invalid_length);
    }
    auto unknown = hex("07");
    auto unsupported = codec::decode_data(unknown);
    CHECK(!unsupported);
    CHECK(unsupported.error().code == ErrorCode::unsupported_tag);
    auto trailing = hex("00 00");
    CHECK(codec::decode_data(trailing).error().code == ErrorCode::trailing_data);
    OctetString long_string{Bytes(128, 0x41)};
    auto long_encoded = codec::encode_data(long_string);
    CHECK(long_encoded);
    CHECK(long_encoded.value()[1] == 0x81 && long_encoded.value()[2] == 0x80);
    CHECK(codec::decode_data(long_encoded.value()).value() == Data(long_string));
    Limits small;
    small.max_elements = 2;
    auto array = hex("01 02 00 00");
    CHECK(codec::decode_data(array, small).error().code == ErrorCode::resource_limit);
    CHECK(codec::encode_data(Array{{Null{}, Null{}}}, small).error().code ==
          ErrorCode::resource_limit);
    small = {};
    small.max_depth = 0;
    auto nested = hex("01 01 00");
    CHECK(codec::decode_data(nested, small).error().code == ErrorCode::resource_limit);
    // NaN 的原始位模式须在解码再编码后保留，不能使用浮点语义相等来验证。
    auto nan = hex("17 7F C0 00 42");
    auto dnan = codec::decode_data(nan);
    CHECK(dnan);
    CHECK(codec::encode_data(dnan.value()).value() == nan);
}

void get_tests() {
    for (auto file : {"get-normal-request.hex", "get-normal-response.hex", "get-list-request.hex",
                      "get-list-response.hex"}) {
        const auto bytes = fixture(file);
        auto decoded = apdu::decode_get(bytes);
        CHECK(decoded);
        CHECK(apdu::encode_get(decoded.value()).value() == bytes);
        for (std::size_t n = 0; n < bytes.size(); ++n)
            CHECK(!apdu::decode_get(ByteView(bytes).subview(0, n)));
    }
    const auto request = fixture("get-normal-request.hex");
    const auto parsed = apdu::decode_get(request);
    const auto& req = std::get<apdu::GetRequest>(parsed.value());
    CHECK(req.piid == 1);
    CHECK(req.attributes[0] == Oad{0x4001, 2, 0});
    CHECK(!req.time_tag);
    auto list_bytes = fixture("get-list-response.hex");
    auto list = apdu::decode_get(list_bytes);
    const auto& attrs = std::get<apdu::GetResponse>(list.value()).attributes;
    CHECK(attrs.size() == 2);
    CHECK(std::get<Data>(attrs[0].result) ==
          Data(Array{{UInt16{2413}, UInt16{2413}, UInt16{2413}}}));
    CHECK(std::get<Data>(attrs[1].result) == Data(Array{{Int32{1000}, Int32{1000}, Int32{1000}}}));
    auto dar_bytes = hex("85 01 41 40 01 02 00 00 04 00 00");
    auto dar = apdu::decode_get(dar_bytes);
    CHECK(dar);
    CHECK(std::get<std::uint8_t>(std::get<apdu::GetResponse>(dar.value()).attributes[0].result) ==
          4);
    auto timed_bytes = hex("05 01 01 40 01 02 00 01 07 E0 01 14 00 00 00 00 00 1E");
    auto timed = apdu::decode_get(timed_bytes);
    CHECK(timed);
    CHECK(std::get<apdu::GetRequest>(timed.value()).time_tag->allowed_delay.interval == 30);
    CHECK(apdu::encode_get(timed.value()).value() == timed_bytes);
    auto follow = hex("85 01 01 40 01 02 00 00 04 01");
    CHECK(apdu::decode_get(follow).error().code == ErrorCode::unsupported_service);
}

void frame_tests() {
    const Bytes check_string{'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    CHECK(link::crc16(check_string) == 0x906e);
    link::Frame f;
    f.server.bytes = hex("07 09 19 05 16 20");
    f.payload = fixture("get-normal-request.hex");
    auto bytes = link::encode_frame(f);
    CHECK(bytes);
    CHECK(bytes.value() == fixture("get-frame.hex"));
    CHECK(bytes.value().size() == 25);
    CHECK(bytes.value()[1] == 23);
    CHECK(link::decode_frame(bytes.value()).value() == f);
    for (std::size_t split = 0; split <= bytes.value().size(); ++split) {
        link::FrameStreamDecoder decoder;
        auto first = decoder.feed(ByteView(bytes.value()).subview(0, split));
        auto second =
            decoder.feed(ByteView(bytes.value()).subview(split, bytes.value().size() - split));
        CHECK(first.size() + second.size() == 1);
        CHECK(std::get<link::Frame>(first.empty() ? second.front() : first.front()) == f);
        CHECK(decoder.buffered_size() == 0);
    }
    auto corrupt = bytes.value();
    corrupt[12] ^= 1;
    CHECK(link::decode_frame(corrupt).error().code == ErrorCode::checksum_header);
    corrupt = bytes.value();
    corrupt[16] ^= 1;
    CHECK(link::decode_frame(corrupt).error().code == ErrorCode::checksum_frame);
    link::FrameStreamDecoder decoder;
    Bytes stream{0xfe, 0xfe, 0x00, 0x68, 0xff, 0xff};
    stream.insert(stream.end(), corrupt.begin(), corrupt.end());
    stream.insert(stream.end(), bytes.value().begin(), bytes.value().end());
    stream.insert(stream.end(), bytes.value().begin(), bytes.value().end());
    const auto events = decoder.feed(stream);
    std::size_t frames = 0, errors = 0;
    for (const auto& event : events) {
        if (std::holds_alternative<link::Frame>(event))
            ++frames;
        else
            ++errors;
    }
    CHECK(frames == 2);
    CHECK(errors >= 2);
    CHECK(decoder.buffered_size() == 0);
    f.control |= 8;
    f.payload = hex("00 68 16 FF");
    auto scrambled = link::encode_frame(f);
    CHECK(scrambled);
    CHECK(link::decode_frame(scrambled.value()).value() == f);
    f.server.bytes = Bytes(16, 0x12);
    f.server.logical = 3;
    auto long_address = link::encode_frame(f);
    CHECK(long_address);
    CHECK(link::decode_frame(long_address.value()).value() == f);
    f.server.type = link::AddressType::broadcast;
    f.server.bytes = {0xaa};
    auto broadcast = link::encode_frame(f);
    CHECK(broadcast);
    CHECK(link::decode_frame(broadcast.value()).value() == f);
    f.server.bytes = {0};
    CHECK(!link::encode_frame(f));
    f.server.type = link::AddressType::single;
    f.payload = Bytes(16373, 0);
    auto maximum = link::encode_frame(f);
    CHECK(maximum);
    CHECK(maximum.value().size() == 16385);
    CHECK(link::decode_frame(maximum.value()));
    f.payload.push_back(0);
    CHECK(!link::encode_frame(f));
    Bytes flood(100000, 0xfe);
    CHECK(decoder.feed(flood).empty());
    CHECK(decoder.buffered_size() == 0);
}

int main() {
    return tests([] {
        data_tests();
        get_tests();
        frame_tests();
    });
}
