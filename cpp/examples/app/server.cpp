/** @file server.cpp
 * @brief 最小托管服务端：设置标准频率，启动 TCP 或串口，然后等待应用退出。
 */
#include <dlt698/app.hpp>
#include <iostream>

int main(int argc, char** argv) {
    dlt698::app::Server server;
    auto published = server.set({0x200F, 2, 0}, dlt698::model::UInt16{5000});
    if (!published) {
        std::cerr << published.error().context << '\n';
        return 1;
    }
    // 无参数监听 TCP；提供串口名时使用默认 9600/8E1，库内部驱动事件循环。
    auto started = argc == 1 ? server.start_tcp("0.0.0.0", 6980) : server.start_serial(argv[1]);
    if (!started) {
        std::cerr << started.error().context << '\n';
        return 1;
    }
    std::cout << "Serving frequency=5000 (50.00 Hz). Press Enter to stop.\n";
    std::cin.get();
    const auto stopped = server.stop();
    if (!stopped) {
        std::cerr << stopped.error().context << '\n';
        return 1;
    }
    return 0;
}
