/** @file client.cpp
 * @brief 与托管服务器配对的最小客户机：连接、读取频率、断开。
 */
#include <dlt698/app.hpp>
#include <iostream>

int main(int argc, char** argv) {
    dlt698::app::Client client;
    // 无参数连接本机服务器；提供串口名时自动使用默认 9600/8E1 和本地 CONNECT。
    auto connected =
        argc == 1 ? client.connect_tcp("127.0.0.1", 6980) : client.open_serial(argv[1]);
    if (!connected) {
        std::cerr << connected.error().context << '\n';
        return 1;
    }
    auto result = client.get({0x200F, 2, 0});
    if (!result) {
        std::cerr << result.error().context << '\n';
        return 1;
    }
    // Data/DAR 保留协议语义，不能只检查外层 Result 后就假定读到了数值。
    if (const auto dar = std::get_if<std::uint8_t>(&result.value())) {
        std::cerr << "DAR=" << unsigned(*dar) << '\n';
        return 1;
    }
    const auto& data = std::get<dlt698::model::Data>(result.value());
    if (data.type() != dlt698::model::DataType::uint16) {
        std::cerr << "Unexpected frequency type\n";
        return 1;
    }
    std::cout << "Frequency=" << data.as<dlt698::model::UInt16>().value << " (unit: 0.01 Hz)\n";
    const auto disconnected = client.disconnect();
    if (!disconnected) {
        std::cerr << disconnected.error().context << '\n';
        return 1;
    }
    return 0;
}
