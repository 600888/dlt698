#include <dlt698/dlt698.hpp>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: dlt698_decode \"68 ... 16\"\n";
        return 2;
    }
    auto bytes = dlt698::from_hex(argv[1]);
    if (!bytes) {
        std::cerr << bytes.error().context << '\n';
        return 1;
    }
    // 先检查链路帧长度和 HCS/FCS，成功后取得的 payload 已完成去扰码。
    auto frame = dlt698::protocol::link::decode_frame(bytes.value());
    if (!frame) {
        std::cerr << frame.error().context << " at offset " << frame.error().offset << '\n';
        return 1;
    }
    const auto& f = frame.value();
    std::cout << "SA (wire order): " << dlt698::to_hex(f.server.bytes)
              << "\nCA: " << unsigned(f.client) << "\nAPDU: " << dlt698::to_hex(f.payload) << '\n';
    // 本示例进一步解析已支持的 GET APDU；其他服务仍可显示原始用户数据。
    auto apdu = dlt698::protocol::apdu::decode_get(f.payload);
    if (apdu)
        std::visit(
            [](const auto& m) { std::cout << "GET attributes: " << m.attributes.size() << '\n'; },
            apdu.value());
    return 0;
}
