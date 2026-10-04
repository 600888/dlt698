#include <dlt698/protocol/apdu/apdu.hpp>
#include <fstream>
#include <iterator>

#include "test.hpp"

using namespace dlt698;
using namespace dlt698::protocol::apdu;

// 仅在本次调用内借用临时测试字节，解码结果始终拥有内存。
template <class T>
Result<ConnectionApdu> decode_owned(const T& input, const Limits& limits = {}) {
    return decode_connection(input, limits);
}

Result<Apdu> parse_owned(const Bytes& input) { return decode_apdu(input); }

Bytes fixture(const char* name) {
    std::ifstream input(std::string(DLT698_VECTOR_DIR) + "/" + name);
    CHECK(input.good());
    return hex(std::string(std::istreambuf_iterator<char>(input), {}).c_str());
}

void expect(const char* name, ConnectionApdu model) {
    const auto expected = fixture(name);
    auto encoded = encode_connection(model);
    CHECK(encoded);
    CHECK(encoded.value() == expected);
    auto decoded = decode_owned(expected);
    CHECK(decoded);
    CHECK(decoded.value().index() == model.index());
    CHECK(encode_connection(decoded.value()).value() == expected);
    auto unified = parse_owned(expected);
    CHECK(unified);
    CHECK(encode_apdu(unified.value()).value() == expected);
    // 每个字段边界都截断一次，检验组合读取不会越界或接受不完整尾部。
    for (std::size_t n = 0; n < expected.size(); ++n) {
        auto cut = decode_owned(ByteView(expected).subview(0, n));
        CHECK(!cut);
        CHECK(cut.error().code == ErrorCode::need_more_data);
    }
    auto trailing = expected;
    trailing.push_back(0);
    CHECK(decode_owned(trailing).error().code == ErrorCode::trailing_data);
}

void vectors() {
    const model::DateTime request{{7, 0xe0, 5, 19, 4, 8, 5, 0, 0, 0xa4}};
    expect("link-login-request.hex", LinkRequest{0, LinkRequestType::login, 180, request});
    expect("link-login-response.hex", LinkResponse{0,
                                                   0x80,
                                                   {{7, 0xe0, 5, 19, 4, 8, 5, 0, 0, 0x89}},
                                                   {{7, 0xe0, 5, 19, 4, 8, 5, 1, 2, 0x5f}},
                                                   {{7, 0xe0, 5, 19, 4, 8, 5, 2, 2, 0xda}}});
    AssociationParameters parameters;
    parameters.protocol.fill(255);
    parameters.function.fill(255);
    expect("connect-request.hex", ConnectRequest{0, parameters, NullSecurity{}, {}});
    FactoryVersion factory;
    factory.manufacturer = {'T', 'O', 'P', 'S'};
    factory.software_version = {'0', '1', '0', '2'};
    factory.software_date = {'1', '6', '0', '7', '3', '1'};
    factory.hardware_version = factory.software_version;
    factory.hardware_date = factory.software_date;
    expect("connect-response.hex", ConnectResponse{0, factory, parameters, 0, {}, {}});
    expect("release-request.hex", ReleaseRequest{1, {}});
    expect("release-response.hex", ReleaseResponse{1, 0, {}});
    expect("release-notification.hex",
           ReleaseNotification{2, {{7, 0xe0, 5, 19, 8, 5, 0}}, {{7, 0xe0, 5, 19, 8, 6, 0}}, {}});
    expect("error-response.hex", ErrorResponse{true, 3, 2, {}});
    const auto link = decode_owned(fixture("link-login-request.hex"));
    CHECK(std::get<LinkRequest>(link.value()).heartbeat_seconds == 180);
    auto connect = decode_owned(fixture("connect-response.hex"));
    CHECK(std::get<ConnectResponse>(connect.value()).parameters.timeout_seconds == 100);
    CHECK(std::get<ConnectResponse>(connect.value()).factory.manufacturer == factory.manufacturer);
}

void mechanisms_and_bounds() {
    auto prefix = fixture("connect-request.hex");
    prefix.resize(prefix.size() - 2);
    ConnectRequest request;
    request.parameters.protocol.fill(255);
    request.parameters.function.fill(255);
    const std::pair<ConnectMechanism, Bytes> mechanisms[] = {
        {PasswordSecurity{"pass"}, hex("01 04 70 61 73 73 00")},
        {SymmetrySecurity{hex("01 02"), hex("03")}, hex("02 02 01 02 01 03 00")},
        {SignatureSecurity{hex("AB"), hex("CD EF")}, hex("03 01 AB 02 CD EF 00")}};
    for (const auto& item : mechanisms) {
        request.mechanism = item.first;
        auto expected = prefix;
        expected.insert(expected.end(), item.second.begin(), item.second.end());
        CHECK(encode_connection(request).value() == expected);
        CHECK(decode_owned(expected).value().index() == 2);
        CHECK(encode_connection(decode_owned(expected).value()).value() == expected);
        for (std::size_t n = prefix.size(); n < expected.size(); ++n)
            CHECK(decode_owned(ByteView(expected).subview(0, n)).error().code ==
                  ErrorCode::need_more_data);
    }
    auto security = fixture("connect-response.hex");
    security.resize(security.size() - 3);
    const auto suffix = hex("01 02 01 02 01 AB 00 00");
    security.insert(security.end(), suffix.begin(), suffix.end());
    auto decoded = decode_owned(security);
    CHECK(decoded);
    CHECK(std::get<ConnectResponse>(decoded.value()).security->random == hex("01 02"));
    CHECK(encode_connection(decoded.value()).value() == security);
    auto tagged = hex("03 01 01 07 E0 05 13 08 05 00 00 00 1E");
    CHECK(decode_owned(tagged));
    CHECK(std::get<ReleaseRequest>(decode_owned(tagged).value()).time_tag->allowed_delay.interval ==
          30);
    CHECK(encode_connection(decode_owned(tagged).value()).value() == tagged);
    for (auto text :
         {"03 40 00", "81 40", "01 00 03", "81 00 08", "EE 01 03", "83 01 01", "03 01 02"}) {
        auto result = decode_owned(hex(text));
        CHECK(!result);
        CHECK(result.error().code == ErrorCode::invalid_value);
    }
    auto follow = hex("83 01 00 01");
    CHECK(decode_owned(follow).error().code == ErrorCode::unsupported_service);
    auto invalid = fixture("connect-request.hex");
    invalid[invalid.size() - 2] = 4;
    CHECK(decode_owned(invalid).error().code == ErrorCode::invalid_value);
    CHECK(encode_connection(ErrorResponse{false, 1, 2, {}}).value() == hex("6E 01 02 00"));
    Limits limit;
    limit.max_data_bytes = 10;
    CHECK(encode_connection(request, limit).error().code == ErrorCode::resource_limit);
    CHECK(decode_owned(fixture("connect-request.hex"), limit).error().code ==
          ErrorCode::resource_limit);
    CHECK(parse_owned(hex("04 00")).error().code == ErrorCode::unsupported_service);
    CHECK(parse_owned(hex("05 01 01 40 01 02 00 00")));
}

int main() {
    return tests([] {
        vectors();
        mechanisms_and_bounds();
    });
}
