#include <dlt698/dlt698.hpp>
#include <dlt698/service/service.hpp>
#include <dlt698/service/sync.hpp>
#include <dlt698/transport/memory.hpp>
#include <dlt698/transport/serial_link.hpp>
#ifdef HAVE_TRANSPORT
#include <dlt698/transport/serial.hpp>
#include <dlt698/transport/tcp.hpp>
#endif
int main() {
    // 仅通过安装后的公开头文件和库调用 API，验证导出目标不依赖源码目录。
    const auto data = dlt698::codec::encode_data(dlt698::model::UInt16{2413});
    if (!data || dlt698::to_hex(data.value()) != "12 09 6D") return 1;
    auto executor = std::make_shared<dlt698::ManualExecutor>();
    auto channels = dlt698::transport::MemoryChannel::pair(executor);
    dlt698::session::SessionOptions client_options, server_options;
    client_options.preset_association = server_options.preset_association = true;
    server_options.role = dlt698::session::Role::server;
    auto client =
        std::make_shared<dlt698::session::Session>(channels.first, executor, client_options);
    auto server =
        std::make_shared<dlt698::session::Session>(channels.second, executor, server_options);
    auto objects = std::make_shared<dlt698::service::ObjectRegistry>();
    auto provider = std::make_shared<dlt698::service::MemoryObject>();
    provider->set(2, dlt698::model::UInt16{2413});
    if (!objects->register_object(
            {0x2000, "电压", {{2, dlt698::model::DataType::uint16, true, true}}}, provider))
        return 2;
    dlt698::service::ServerService server_api(server, objects);
    dlt698::service::ClientService client_api(client);
    client->start();
    server->start();
    bool read_ok = false;
    client_api.async_get({0x2000, 2, 0}, [&](auto result) {
        read_ok = result && std::holds_alternative<dlt698::model::Data>(result.value()) &&
                  std::get<dlt698::model::Data>(result.value()) ==
                      dlt698::model::Data{dlt698::model::UInt16{2413}};
    });
    executor->run_ready();
    dlt698::service::SyncClientService sync(
        client, [executor](auto elapsed) { executor->advance(elapsed); });
    const auto set = sync.set({0x2000, 2, 0}, dlt698::model::UInt16{2400});
    if (!set || set.value()) return 4;
    client->close();
    server->close();
    executor->run_ready();
    if (!read_ok) return 3;
    auto serial_pair = dlt698::transport::MemoryChannel::pair(executor);
    auto serial_link = dlt698::transport::SerialLinkChannel::wrap(serial_pair.first, executor);
    serial_link->close();
    executor->run_ready();
#ifdef HAVE_TRANSPORT
    dlt698::transport::IoRuntime runtime;
    runtime.stop();
    if (dlt698::transport::SerialChannel::open({}, "COM1").error().code !=
        dlt698::ErrorCode::invalid_value)
        return 5;
#endif
    return 0;
}
