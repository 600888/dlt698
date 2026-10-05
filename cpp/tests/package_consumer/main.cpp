#include <dlt698/dlt698.hpp>
#include <dlt698/service/memory_records.hpp>
#include <dlt698/service/point_probe.hpp>
#include <dlt698/service/service.hpp>
#include <dlt698/service/standard_object.hpp>
#include <dlt698/service/sync.hpp>
#include <dlt698/standard/oi.hpp>
#include <dlt698/transport/memory.hpp>
#include <dlt698/transport/serial_link.hpp>
#ifdef HAVE_TRANSPORT
#include <dlt698/transport/serial.hpp>
#include <dlt698/transport/tcp.hpp>
#endif
int main() {
    // 新目录由 core 导出，绑定辅助由 service 导出；安装后的中文头文件需继承 /utf-8。
    namespace oi = dlt698::standard::oi;
    // 安装后的轻量头文件可在编译期使用常量构造 OAD。
    constexpr dlt698::model::Oad voltage_point{oi::voltage, 2, 1};
    if (dlt698::standard::objects().size() != 127 ||
        !dlt698::standard::find_object(oi::combination_active_energy))
        return 9;
    const auto energy_point = dlt698::standard::tariff_oad(oi::forward_active_energy, 0);
    if (!energy_point || !(energy_point.value() == dlt698::model::Oad{0x0010, 2, 1})) return 10;
    const auto engineering =
        dlt698::standard::engineering_values(voltage_point, dlt698::model::UInt16{2413});
    if (!engineering || dlt698::standard::decimal_text(engineering.value()[0]) != "241.3")
        return 11;
    dlt698::service::ObjectRegistry standard_objects;
    auto frequency = std::make_shared<dlt698::service::MemoryObject>();
    frequency->set(2, dlt698::model::UInt16{5000});
    if (!dlt698::service::register_standard_object(standard_objects, oi::frequency, {2}, frequency))
        return 12;
    if (std::get<dlt698::model::Data>(standard_objects.read({oi::frequency, 2, 0}))
            .as<dlt698::model::UInt16>()
            .value != 5000)
        return 13;
    const auto phased_energy =
        dlt698::standard::energy_oad(oi::forward_active_energy, dlt698::standard::Phase::b, 0);
    const auto maximum = dlt698::standard::demand_oad(oi::forward_active_maximum_demand,
                                                      dlt698::standard::Phase::total, 0);
    const auto harmonic =
        dlt698::standard::harmonic_oad(oi::voltage_harmonics, dlt698::standard::Phase::c, 2);
    if (!phased_energy || !maximum || !harmonic ||
        !(phased_energy.value() == dlt698::model::Oad{0x0012, 2, 1}) ||
        !(harmonic.value() == dlt698::model::Oad{0x200d, 4, 2}))
        return 14;
    const dlt698::model::DateTimeS occurred{{0x07, 0xea, 10, 5, 12, 30, 0}};
    const auto demand = dlt698::standard::demand_values(
        maximum.value(), dlt698::model::Structure{{dlt698::model::UInt32{123456}, occurred}});
    if (!demand || !(demand.value()[0].occurred_at == occurred) ||
        dlt698::standard::decimal_text(demand.value()[0].number) != "12.3456")
        return 15;
    // 新增记录和能力接口只依赖安装包的导出符号。
    auto daily = dlt698::service::MemoryRecords::create(oi::daily_freeze);
    auto daily_query = dlt698::standard::record_sequences(oi::daily_freeze, 1, 2);
    if (!daily || !daily_query ||
        !daily.value()->replace_rows({{dlt698::model::UInt32{1}, occurred}}))
        return 16;
    if (std::get<std::vector<dlt698::protocol::apdu::RecordRow>>(
            daily.value()->read_record(daily_query.value()).result)
            .size() != 1)
        return 17;
    if (!dlt698::standard::plan_reads({}, {voltage_point}) ||
        !dlt698::standard::require_record_service({}))
        return 18;
    // 仅通过安装后的公开头文件和库调用 API，验证导出目标不依赖源码目录。
    const auto data = dlt698::codec::encode_data(dlt698::model::UInt16{2413});
    if (!data || dlt698::to_hex(data.value()) != "12 09 6D") return 1;
    const dlt698::model::Data selector =
        dlt698::model::RecordData{dlt698::model::Rsd{dlt698::model::Selector9{1}}};
    const auto selector_wire = dlt698::codec::encode_data(selector);
    if (!selector_wire || dlt698::to_hex(selector_wire.value()) != "5A 09 01" ||
        !(dlt698::codec::decode_data(selector_wire.value()).value() == selector))
        return 6;
    dlt698::protocol::link::LinkFragmenter fragments({1, 2, 3}, 1, 10);
    if (!fragments.acknowledge(0) || fragments.current().sequence != 1) return 7;
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
    provider->bind_record(3, [](const dlt698::protocol::apdu::GetRecord& q) {
        return dlt698::protocol::apdu::RecordResult{
            q.attribute,
            {dlt698::model::Oad{0x2000, 2, 0}},
            std::vector<dlt698::protocol::apdu::RecordRow>{{dlt698::model::UInt16{42}}}};
    });
    // 此对象用于通用读写/记录服务导出测试，不代表标准电压接口类。
    if (!objects->register_object({0x2000,
                                   "电压",
                                   {{2, dlt698::model::DataType::uint16, true, true},
                                    {3, dlt698::model::DataType::null, true, false, true}}},
                                  provider))
        return 2;
    dlt698::service::ServerService server_api(server, objects);
    dlt698::service::ClientService client_api(client);
    client->start();
    server->start();
    bool probe_ok = false;
    dlt698::service::async_probe_points(client, {}, {{0x2000, 2, 0}}, {}, [&](auto result) {
        probe_ok = result && result.value().size() == 1 &&
                   std::holds_alternative<dlt698::model::Data>(result.value()[0].outcome);
    });
    executor->run_ready();
    if (!probe_ok) return 19;
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
    const auto record = sync.get_record({{0x2000, 3, 0}, dlt698::model::SelectAll{}, {}});
    if (!record ||
        std::get<std::vector<dlt698::protocol::apdu::RecordRow>>(record.value().result)[0][0]
                .as<dlt698::model::UInt16>()
                .value != 42)
        return 8;
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
