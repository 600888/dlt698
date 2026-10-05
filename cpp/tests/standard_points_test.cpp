#include <dlt698/dlt698.hpp>
#include <dlt698/service/service.hpp>
#include <dlt698/service/standard_object.hpp>
#include <dlt698/transport/memory.hpp>
#include <limits>
#include <set>

#include "test.hpp"

using namespace dlt698;
using namespace dlt698::model;
namespace standard = dlt698::standard;

void catalog_tests() {
    // 独立依据附录 E.1/E.3/E.5，避免只检查实现生成的表能够自洽。
    struct Expected {
        std::uint16_t oi;
        std::uint8_t class_id;
        DataType type;
        std::optional<DataType> element;
        int scaler;
        int unit;
    };

    const Expected expected[] = {{0x0000, 1, DataType::array, DataType::int32, -2, 33},
                                 {0x0010, 1, DataType::array, DataType::uint32, -2, 33},
                                 {0x0020, 1, DataType::array, DataType::uint32, -2, 33},
                                 {0x0030, 1, DataType::array, DataType::int32, -2, 35},
                                 {0x0040, 1, DataType::array, DataType::int32, -2, 35},
                                 {0x2000, 3, DataType::array, DataType::uint16, -1, 38},
                                 {0x2001, 3, DataType::array, DataType::int32, -3, 36},
                                 {0x2004, 4, DataType::array, DataType::int32, -1, 27},
                                 {0x2005, 4, DataType::array, DataType::int32, -1, 31},
                                 {0x2006, 4, DataType::array, DataType::int32, -1, 29},
                                 {0x200a, 4, DataType::array, DataType::int16, -3, 255},
                                 {0x200f, 6, DataType::uint16, {}, -2, 47},
                                 {0x4000, 8, DataType::date_time_s, {}, 0, 0},
                                 {0x4001, 8, DataType::octet_string, {}, 0, 0}};
    CHECK(standard::objects().size() == 118);
    std::set<std::uint16_t> identifiers;
    for (const auto& entry : expected) {
        CHECK(identifiers.insert(entry.oi).second);
        const auto object = standard::find_object(entry.oi);
        CHECK(object && object->class_id == entry.class_id);
        CHECK(std::string(object->name).size() && std::string(object->source).size());
        CHECK(std::string(object->version) == "DL/T 698.45-2017");
        const auto attribute = standard::find_attribute({entry.oi, 2, 0});
        CHECK(attribute && attribute->type == entry.type &&
              attribute->element_type == entry.element);
        CHECK(attribute->readable && !attribute->record);
        CHECK(bool(attribute->scaling) == (entry.unit != 0));
        if (attribute->scaling) {
            CHECK(attribute->scaling->scaler == entry.scaler);
            CHECK(attribute->scaling->unit == entry.unit);
        }
        std::set<std::uint8_t> numbers;
        for (const auto& a : object->attributes) {
            CHECK(numbers.insert(a.number).second);
            CHECK(a.number && a.number <= 31 && std::string(a.source).size());
        }
        CHECK(standard::find_attribute({entry.oi, 1, 0})->type == DataType::octet_string);
    }
    CHECK(standard::find_attribute({0x0010, 4, 0})->element_type == DataType::uint64);
    CHECK(standard::find_attribute({0x0000, 4, 0})->element_type == DataType::int64);
    CHECK(standard::find_attribute({0x0010, 4, 0})->scaling->scaler == -4);
    CHECK(standard::find_attribute({0x2001, 4, 0})->type == DataType::int32);
    CHECK(standard::find_attribute({0x4001, 2, 0})->writable);
    CHECK(!standard::find_attribute({0x2000, 2, 0})->writable);
    CHECK(!standard::find_object(0x9999));
    CHECK(!standard::find_attribute({0x2000, 0, 0}));
    CHECK(!standard::find_attribute({0x4000, 5, 0}));
    CHECK(standard::find_attribute({0x2000, 0x22, 255}) ==
          standard::find_attribute({0x2000, 2, 0}));
    CHECK(standard::unit_symbol(33) == "kWh" && standard::unit_symbol(27) == "W");
    CHECK(standard::unit_symbol(255).empty() && standard::unit_symbol(0).empty());
}

void addressing_tests() {
    const standard::DeviceLayout single{standard::Wiring::single_phase, 0};
    const standard::DeviceLayout two_tariffs{standard::Wiring::three_phase, 2};
    const auto total = standard::tariff_oad(0x0010, 0);
    CHECK(total && total.value() == Oad{0x0010, 2, 1});
    CHECK(standard::tariff_oad(0x0010, 1).value() == Oad{0x0010, 2, 2});
    CHECK(standard::tariff_oad(0x0010, 2, two_tariffs, true).value() == Oad{0x0010, 4, 3});
    CHECK(!standard::tariff_oad(0x0010, 3, two_tariffs));
    CHECK(!standard::tariff_oad(0x0010, 1, single));
    CHECK(!standard::tariff_oad(0x2000, 0));
    CHECK(standard::phase_oad(0x2000, standard::Phase::c).value() == Oad{0x2000, 2, 3});
    CHECK(standard::phase_oad(0x2004, standard::Phase::total).value() == Oad{0x2004, 2, 1});
    CHECK(standard::phase_oad(0x2004, standard::Phase::a, single).value() == Oad{0x2004, 2, 2});
    CHECK(!standard::phase_oad(0x2000, standard::Phase::total));
    CHECK(!standard::phase_oad(0x2004, standard::Phase::b, single));
    CHECK(!standard::phase_oad(0x2000, static_cast<standard::Phase>(-1)));
    CHECK(!standard::phase_oad(0x0010, standard::Phase::a));
    CHECK(standard::make_oad(0x2001, 4).value() == Oad{0x2001, 4, 0});
    CHECK(!standard::make_oad(0x2001, 4, 1));
    CHECK(!standard::make_oad(0x2000, 2, 4));
    CHECK(!standard::make_oad(0x2000, 2, 256));
    CHECK(!standard::make_oad(0x2000, 32));
    CHECK(!standard::make_oad(0x2000, 0));
    CHECK(!standard::make_oad(0x9999));
    const standard::DeviceLayout maximum{standard::Wiring::three_phase, 254};
    CHECK(standard::tariff_oad(0x0010, 254, maximum).value().index == 255);
    CHECK(!standard::validate_layout({standard::Wiring::three_phase, 255}));
    CHECK(!standard::validate_layout({static_cast<standard::Wiring>(42), 4}));
    CHECK(!standard::tariff_oad(0x0010, std::numeric_limits<std::size_t>::max()));
    CHECK(standard::validate_oad({0x2000, 0x22, 0}).error().code == ErrorCode::unsupported_service);
}

void value_tests() {
    const Data voltage = Array{{UInt16{2413}, UInt16{2413}, UInt16{2413}}};
    CHECK(standard::validate_value({0x2000, 2, 0}, voltage));
    CHECK(standard::validate_value({0x2000, 2, 1}, UInt16{2413}));
    CHECK(!standard::validate_value({0x2000, 2, 0}, UInt16{2413}));
    CHECK(!standard::validate_value({0x2000, 2, 1}, voltage));
    CHECK(!standard::validate_value({0x2000, 2, 0}, Array{{UInt16{1}, UInt16{2}}}));
    CHECK(!standard::validate_value({0x2000, 2, 0}, Array{{UInt16{1}, Int16{2}, UInt16{3}}}));
    CHECK(!standard::validate_value({0x2000, 2, 0}, Structure{{UInt16{1}, UInt16{2}, UInt16{3}}}));
    CHECK(!standard::validate_value({0x2000, 2, 0},
                                    Array{{UInt16{1}, Array{{UInt16{2}}}, UInt16{3}}}));
    CHECK(!standard::validate_value({0x2000, 2, 0}, Array{}));
    CHECK(standard::validate_value({0x2000, 2, 0}, Array{{UInt16{2413}}},
                                   {standard::Wiring::single_phase, 0}));
    CHECK(standard::validate_value({0x2004, 2, 0}, Array{{Int32{-500}, Int32{-500}}},
                                   {standard::Wiring::single_phase, 0}));
    CHECK(standard::validate_value({0x0010, 2, 0}, Array{{UInt32{1}, UInt32{2}}},
                                   {standard::Wiring::three_phase, 1}));
    CHECK(!standard::validate_value({0x0010, 2, 1}, Int32{1}));
    CHECK(standard::validate_value({0x2000, 3, 0}, ScalerUnit{-1, 38}));
    CHECK(standard::validate_value({0x4000, 2, 0}, DateTimeS{{0x07, 0xe0, 1, 20, 0, 0, 0}}));
    CHECK(!standard::validate_value({0x4000, 2, 0}, OctetString{}));
    Limits limited;
    limited.max_elements = 3;  // 数组还含根节点，三个元素实际共四个节点。
    CHECK(standard::validate_value({0x2000, 2, 0}, voltage, {}, limited).error().code ==
          ErrorCode::resource_limit);
    limited = {};
    limited.max_data_bytes = 4;
    CHECK(
        standard::validate_value({0x4001, 2, 0}, OctetString{hex("12 34 56 78 90 12")}, {}, limited)
            .error()
            .code == ErrorCode::resource_limit);
    limited = {};
    limited.max_depth = 0;
    CHECK(!standard::validate_value({0x2000, 2, 0}, voltage, {}, limited));
    CHECK(standard::validate_value({0x2000, 2, 1}, UInt16{2413}, {}, limited));

    const auto volts = standard::engineering_values({0x2000, 2, 0}, voltage);
    CHECK(volts && volts.value().size() == 3);
    CHECK(standard::decimal_text(volts.value()[0]) == "241.3");
    CHECK(standard::approximate_value(volts.value()[0]) > 241.299 &&
          standard::approximate_value(volts.value()[0]) < 241.301);
    CHECK(standard::decimal_text(
              standard::engineering_values({0x2001, 2, 1}, Int32{1000}).value()[0]) == "1.000");
    CHECK(standard::decimal_text(
              standard::engineering_values({0x2004, 2, 1}, Int32{-12345}).value()[0]) == "-1234.5");
    CHECK(standard::decimal_text(
              standard::engineering_values({0x200a, 2, 1}, Int16{-999}).value()[0]) == "-0.999");
    CHECK(standard::decimal_text(
              standard::engineering_values({0x200f, 2, 0}, UInt16{5000}).value()[0]) == "50.00");
    CHECK(!standard::engineering_values({0x4001, 2, 0}, OctetString{}));
    CHECK(!standard::engineering_values({0x4000, 2, 0}, DateTimeS{}));
    CHECK(!standard::engineering_values({0x2000, 3, 0}, ScalerUnit{-1, 38}));
    const auto largest = standard::engineering_values(
        {0x0010, 4, 1}, UInt64{std::numeric_limits<std::uint64_t>::max()});
    CHECK(largest && std::get<std::uint64_t>(largest.value()[0].raw) ==
                         std::numeric_limits<std::uint64_t>::max());
    CHECK(standard::decimal_text(largest.value()[0]) == "1844674407370955.1615");
    const auto smallest = standard::engineering_values(
        {0x0000, 4, 1}, Int64{std::numeric_limits<std::int64_t>::min()});
    CHECK(smallest && standard::decimal_text(smallest.value()[0]) == "-922337203685477.5808");
    CHECK(standard::decimal_text({std::uint64_t{0}, {-4, 33}}) == "0.0000");
    CHECK(standard::decimal_text({std::int64_t{-12}, {3, 33}}) == "-12000");
    CHECK(standard::decimal_text({std::uint64_t{1}, {-128, 33}}).size() == 130);
}

struct ProbeProvider : service::IObjectProvider {
    service::ObjectValue value = UInt16{2413};
    Oad last;
    int reads = 0;
    bool throwing = false;
    service::ObjectRegistry* reentrant_registry = nullptr;

    service::ObjectValue read(const Oad& attribute) override {
        if (throwing) throw std::runtime_error("hardware error");
        last = attribute;
        ++reads;
        // 校验适配器必须在目录锁外运行；provider 可查询其他对象。
        if (reentrant_registry)
            CHECK(std::get<std::uint8_t>(reentrant_registry->read({0x9999, 2, 0})) == 4);
        return value;
    }
};

void binding_tests() {
    auto schema = service::make_object_schema(0x0010, {1, 2, 3, 4, 5});
    CHECK(schema && schema.value().attributes.size() == 5 && schema.value().methods.empty());
    CHECK(!service::make_object_schema(0x4000, {2}).value().attributes[0].writable);
    CHECK(!service::make_object_schema(0x9999, {2}));
    CHECK(!service::make_object_schema(0x2000, {}));
    CHECK(!service::make_object_schema(0x2000, {2, 2}));
    CHECK(!service::make_object_schema(0x2000, {0}));
    CHECK(!service::make_object_schema(0x2000, {0x22}));
    CHECK(!service::make_object_schema(0x2000, {4}));
    service::ObjectRegistry registry;
    auto probe = std::make_shared<ProbeProvider>();
    probe->reentrant_registry = &registry;
    CHECK(!service::register_standard_object(registry, 0x2000, {2}, {}));
    CHECK(!service::register_standard_object(registry, 0x2000, {2}, probe,
                                             {standard::Wiring::three_phase, 255}));
    CHECK(service::register_standard_object(registry, 0x2000, {2}, probe));
    CHECK(!service::register_standard_object(registry, 0x2000, {2}, probe));
    CHECK(std::get<Data>(registry.read({0x2000, 2, 3})) == Data{UInt16{2413}});
    CHECK(probe->last == Oad{0x2000, 2, 3} && probe->reads == 1);
    CHECK(std::get<std::uint8_t>(registry.read({0x2000, 2, 4})) == 8);
    CHECK(std::get<std::uint8_t>(registry.read({0x2000, 0x22, 1})) == 3);
    CHECK(std::get<std::uint8_t>(registry.read({0x2000, 3, 0})) == 4);
    CHECK(probe->reads == 1);
    CHECK(std::get<std::uint8_t>(registry.read({0x2000, 2, 0})) == 7);
    probe->value = Array{{UInt16{2413}, UInt16{2414}, UInt16{2415}}};
    CHECK(std::holds_alternative<Data>(registry.read({0x2000, 2, 0})));
    probe->value = Array{{UInt16{1}, UInt32{2}, UInt16{3}}};
    CHECK(std::get<std::uint8_t>(registry.read({0x2000, 2, 0})) == 7);
    probe->value = std::uint8_t{42};
    CHECK(std::get<std::uint8_t>(registry.read({0x2000, 2, 1})) == 42);
    probe->throwing = true;
    CHECK(std::get<std::uint8_t>(registry.read({0x2000, 2, 1})) == 255);
    CHECK(registry.write({0x2000, 2, 1}, UInt16{42}) == 3);
    CHECK(registry.invoke({0x2000, 1, 0}, Int8{0}).dar == 4);
    auto memory = std::make_shared<service::MemoryObject>();
    memory->set(2, OctetString{hex("12 34 56 78 90 12")});
    std::weak_ptr<service::MemoryObject> retained = memory;
    CHECK(service::register_standard_object(registry, 0x4001, {2}, memory));
    memory.reset();
    CHECK(!retained.expired());
    CHECK(std::holds_alternative<Data>(registry.read({0x4001, 2, 0})));
    CHECK(registry.write({0x4001, 2, 0}, OctetString{}) == 3);
    Limits tiny;
    tiny.max_data_bytes = 1;
    CHECK(service::register_standard_object(registry, 0x200f, {2}, probe, {}, tiny));
    probe->throwing = false;
    probe->value = UInt16{5000};
    CHECK(std::get<std::uint8_t>(registry.read({0x200f, 2, 0})) == 7);
    // 厂家扩展及原手写 schema 不受标准目录约束。
    CHECK(registry.register_object({0xf001, "厂家量", {{2, DataType::octet_string}}},
                                   retained.lock()));
    CHECK(std::holds_alternative<Data>(registry.read({0xf001, 2, 0})));
}

void wire_and_service_tests() {
    // 附录 D.3.1/D.3.2 的 Data 字节，以及 D.3.3 的五项零电能数组。
    const auto voltage_bytes = hex("01 03 12 09 6D 12 09 6D 12 09 6D");
    const auto current_bytes = hex("01 03 05 00 00 03 E8 05 00 00 03 E8 05 00 00 03 E8");
    const auto address_bytes = hex("09 06 12 34 56 78 90 12");
    auto volts = codec::decode_data(voltage_bytes);
    auto amps = codec::decode_data(current_bytes);
    auto address = codec::decode_data(address_bytes);
    const auto energy_bytes =
        hex("01 05 06 00 00 00 00 06 00 00 00 00 06 00 00 00 00 06 00 00 00 00 06 00 00 00 00");
    auto energy = codec::decode_data(energy_bytes);
    CHECK(volts && amps && address && energy);
    CHECK(standard::validate_value({0x2000, 2, 0}, volts.value()));
    CHECK(standard::validate_value({0x2001, 2, 0}, amps.value()));
    CHECK(standard::validate_value({0x4001, 2, 0}, address.value()));
    CHECK(standard::validate_value({0x0010, 2, 0}, energy.value()));
    CHECK(codec::encode_data(energy.value()).value() == energy_bytes);
    const auto power_bytes =
        hex("01 04 05 FF FF FF FF 05 00 00 00 00 05 00 00 00 01 05 FF FF FF FE");
    auto power = codec::decode_data(power_bytes);
    CHECK(power && standard::validate_value({0x2004, 2, 0}, power.value()));
    CHECK(codec::encode_data(Array{{Int32{-1}, Int32{0}, Int32{1}, Int32{-2}}}).value() ==
          power_bytes);
    CHECK(codec::encode_data(standard::tariff_oad(0x0010, 0).value()).value() ==
          hex("51 00 10 02 01"));

    auto executor = std::make_shared<ManualExecutor>();
    auto channels = transport::MemoryChannel::pair(executor);
    session::SessionOptions server_options;
    server_options.role = session::Role::server;
    auto client = std::make_shared<session::Session>(channels.first, executor);
    auto server = std::make_shared<session::Session>(channels.second, executor, server_options);
    auto registry = std::make_shared<service::ObjectRegistry>();
    auto voltage = std::make_shared<service::MemoryObject>();
    voltage->set(2, volts.value());
    CHECK(service::register_standard_object(*registry, 0x2000, {2}, voltage));
    service::ServerService server_api(server, registry);
    service::ClientService client_api(client);
    client->start();
    server->start();
    std::optional<Result<protocol::apdu::ConnectResponse>> connect;
    client->async_connect([&](auto result) { connect = std::move(result); });
    executor->run_ready();
    CHECK(connect && *connect && connect->value().result == 0);
    std::optional<Result<service::ObjectValue>> one;
    client_api.async_get(standard::phase_oad(0x2000, standard::Phase::a).value(),
                         [&](auto r) { one = std::move(r); });
    executor->run_ready();
    CHECK(one && *one && std::get<Data>(one->value()) == Data{UInt16{2413}});
    std::optional<Result<protocol::apdu::GetResponse>> list;
    client_api.async_get_list({{0x2000, 2, 0}, {0x2000, 2, 4}, {0x2000, 3, 0}, {0x9999, 2, 0}},
                              [&](auto r) { list = std::move(r); });
    executor->run_ready();
    CHECK(list && *list && list->value().attributes.size() == 4);
    const auto& results = list->value().attributes;
    CHECK(std::get<Data>(results[0].result) == volts.value());
    CHECK(std::get<std::uint8_t>(results[1].result) == 8);
    CHECK(std::get<std::uint8_t>(results[2].result) == 4);
    CHECK(std::get<std::uint8_t>(results[3].result) == 4);
    std::optional<Result<std::uint8_t>> written;
    client_api.async_set({0x2000, 2, 1}, UInt16{42}, [&](auto r) { written = std::move(r); });
    executor->run_ready();
    CHECK(written && *written && written->value() == 3);
    client->close();
    server->close();
    executor->run_ready();
}

void expanded_tests() {
    // 附录 E.1/E.2 独立期望：前两组有功无符号，组合无功有符号，其余无符号。
    const std::uint8_t energy_units[] = {33, 33, 35, 35, 35, 35, 35, 35, 34, 34};
    const std::uint8_t demand_units[] = {28, 28, 32, 32, 32, 32, 32, 32, 30, 30};

    struct VariableExpected {
        std::uint16_t oi;
        std::uint8_t class_id;
        DataType type;
        std::optional<DataType> element;
        std::optional<ScalerUnit> scaling;
    };

    const VariableExpected variables[] = {
        {0x200b, 3, DataType::array, DataType::int16, ScalerUnit{-2, 51}},
        {0x200c, 3, DataType::array, DataType::int16, ScalerUnit{-2, 51}},
        {0x200d, 5, DataType::array, DataType::int16, ScalerUnit{-2, 51}},
        {0x200e, 5, DataType::array, DataType::int16, ScalerUnit{-2, 51}},
        {0x2010, 6, DataType::int16, {}, ScalerUnit{-1, 9}},
        {0x2014, 6, DataType::array, DataType::bit_string, {}},
        {0x2015, 6, DataType::bit_string, {}, {}},
        {0x2017, 6, DataType::int32, {}, ScalerUnit{-4, 28}},
        {0x2018, 6, DataType::int32, {}, ScalerUnit{-4, 32}},
        {0x2019, 6, DataType::int32, {}, ScalerUnit{-4, 30}}};
    for (const auto& entry : variables) {
        const auto object = standard::find_object(entry.oi);
        const auto attribute = standard::find_attribute({entry.oi, 2, 0});
        CHECK(object && object->class_id == entry.class_id);
        CHECK(attribute && attribute->type == entry.type &&
              attribute->element_type == entry.element);
        CHECK(attribute->scaling == entry.scaling && !attribute->writable);
    }

    struct ParameterExpected {
        std::uint16_t oi;
        DataType type;
        bool writable;
        std::optional<std::size_t> size;
    };

    const ParameterExpected parameters[] = {{0x4000, DataType::date_time_s, true, {}},
                                            {0x4001, DataType::octet_string, true, {}},
                                            {0x4002, DataType::octet_string, true, {}},
                                            {0x4003, DataType::octet_string, true, {}},
                                            {0x4006, DataType::structure, false, {}},
                                            {0x400c, DataType::structure, true, {}},
                                            {0x400d, DataType::uint8, true, {}},
                                            {0x400e, DataType::uint8, true, {}},
                                            {0x4010, DataType::uint8, false, {}},
                                            {0x4012, DataType::bit_string, true, 8},
                                            {0x4030, DataType::structure, true, {}},
                                            {0x4100, DataType::uint8, true, {}},
                                            {0x4101, DataType::uint8, true, {}},
                                            {0x4103, DataType::visible_string, true, 32},
                                            {0x4104, DataType::visible_string, false, 6},
                                            {0x4105, DataType::visible_string, false, 6},
                                            {0x4106, DataType::visible_string, false, 6},
                                            {0x4107, DataType::visible_string, false, 4},
                                            {0x4108, DataType::visible_string, false, 4},
                                            {0x410b, DataType::visible_string, false, 32}};
    for (const auto& entry : parameters) {
        CHECK(standard::find_object(entry.oi)->class_id == 8);
        const auto attribute = standard::find_attribute({entry.oi, 2, 0});
        CHECK(attribute && attribute->type == entry.type && attribute->writable == entry.writable);
        CHECK((attribute->value_definition ? attribute->value_definition->size
                                           : std::optional<std::size_t>{}) == entry.size);
    }
    for (std::uint16_t family = 1; family <= 10; ++family) {
        for (unsigned phase = 0; phase <= 3; ++phase) {
            const auto energy_id = static_cast<std::uint16_t>(family * 16 + phase);
            const auto demand_id = static_cast<std::uint16_t>(0x1000 + energy_id);
            const bool sign = family == 3 || family == 4;
            const auto energy = standard::find_attribute({energy_id, 2, 0});
            CHECK(energy && energy->element_type == (sign ? DataType::int32 : DataType::uint32));
            CHECK(energy->scaling->scaler == -2 &&
                  energy->scaling->unit == energy_units[family - 1]);
            CHECK(standard::find_attribute({energy_id, 4, 0})->element_type ==
                  (sign ? DataType::int64 : DataType::uint64));
            const auto demand = standard::find_attribute({demand_id, 2, 0});
            CHECK(demand && demand->element_type == DataType::structure &&
                  demand->element_definition);
            const auto& fields = demand->element_definition->fields;
            CHECK(fields.size() == 2 &&
                  fields[0].type == (sign ? DataType::int32 : DataType::uint32));
            CHECK(fields[0].scaling->scaler == -4 &&
                  fields[0].scaling->unit == demand_units[family - 1]);
            CHECK(fields[1].type == DataType::date_time_s && !fields[1].scaling);
        }
    }
    std::set<std::uint16_t> ids;
    for (const auto& object : standard::objects()) {
        CHECK(ids.insert(object.oi).second);
        std::set<std::uint8_t> attributes;
        for (const auto& attribute : object.attributes) {
            CHECK(attributes.insert(attribute.number).second);
            CHECK(attribute.number >= 1 && attribute.number <= 31 && attribute.readable &&
                  !attribute.record);
            CHECK(std::string(attribute.source).size());
            if (attribute.element_definition)
                CHECK(attribute.element_definition->type == attribute.element_type);
            if (attribute.value_definition)
                CHECK(attribute.value_definition->type == attribute.type);
        }
    }
    const standard::DeviceLayout layout{standard::Wiring::three_phase, 1, 3};
    const standard::DeviceLayout single{standard::Wiring::single_phase, 1, 3};
    CHECK(standard::energy_oad(0x0010, standard::Phase::b, 1, layout).value() == Oad{0x0012, 2, 2});
    CHECK(standard::energy_oad(0x0090, standard::Phase::c, 0, layout, true).value() ==
          Oad{0x0093, 4, 1});
    CHECK(!standard::energy_oad(0x0000, standard::Phase::a, 0));
    CHECK(!standard::energy_oad(0x0011, standard::Phase::a, 0));
    CHECK(!standard::energy_oad(0x1010, standard::Phase::a, 0));
    CHECK(!standard::energy_oad(0x0010, standard::Phase::b, 0, single));
    CHECK(!standard::energy_oad(0x0010, static_cast<standard::Phase>(256), 0));
    CHECK(!standard::validate_oad({0x1013, 2, 1}, single));
    CHECK(standard::demand_oad(0x1030, standard::Phase::a, 0, layout).value() == Oad{0x1031, 2, 1});
    CHECK(!standard::tariff_oad(0x1010, 0, layout, true));
    CHECK(!standard::demand_oad(0x0010, standard::Phase::total, 0));
    CHECK(!standard::demand_oad(0x1010, standard::Phase::total, 2, layout));
    CHECK(!standard::validate_layout({standard::Wiring::three_phase, 1, 1}));
    CHECK(!standard::validate_layout({standard::Wiring::three_phase, 1, 256}));

    const DateTimeS time{{0x07, 0xea, 10, 5, 12, 30, 0}};
    const Data item = Structure{{UInt32{123456}, time}};
    const Data signed_item = Structure{{Int32{-123456}, time}};
    const Data demand_array = Array{{item, item}};
    CHECK(standard::validate_value({0x1010, 2, 0}, demand_array, layout));
    CHECK(standard::validate_value({0x1031, 2, 1}, signed_item, layout));
    CHECK(!standard::validate_value({0x1010, 2, 1}, signed_item, layout));
    CHECK(!standard::validate_value({0x1030, 2, 1}, item, layout));
    CHECK(!standard::validate_value({0x1010, 2, 1}, Structure{{time, UInt32{1}}}, layout));
    CHECK(!standard::validate_value({0x1010, 2, 1}, Structure{{UInt32{1}}}, layout));
    CHECK(
        !standard::validate_value({0x1010, 2, 1}, Structure{{UInt32{1}, time, UInt8{1}}}, layout));
    CHECK(!standard::validate_value({0x1010, 2, 1}, Array{{UInt32{1}, time}}, layout));
    CHECK(!standard::validate_value({0x1010, 2, 0}, Array{{item}}, layout));
    auto readings = standard::demand_values({0x1010, 2, 0}, demand_array, layout);
    CHECK(readings && readings.value().size() == 2);
    CHECK(standard::decimal_text(readings.value()[0].number) == "12.3456");
    CHECK(readings.value()[0].occurred_at == time);
    CHECK(standard::validate_value({0x1010, 2, 1}, Structure{{UInt32{0xffffffff}, time}}, layout));
    CHECK(!standard::validate_value({0x1010, 2, 1}, Structure{{UInt32{1}, Structure{{time}}}},
                                    layout));
    CHECK(standard::decimal_text(
              standard::demand_values({0x1031, 2, 1}, signed_item, layout).value()[0].number) ==
          "-12.3456");
    CHECK(!standard::demand_values({0x1010, 3, 0}, ScalerUnit{-4, 28}, layout));
    CHECK(standard::engineering_values({0x1010, 2, 0}, demand_array, layout).value().size() == 2);
    const auto item_bytes = codec::encode_data(item);
    CHECK(item_bytes &&
          to_hex(item_bytes.value()) == "02 02 06 00 01 E2 40 1C 07 EA 0A 05 0C 1E 00");
    Limits limits;
    limits.max_depth = 1;
    CHECK(!standard::validate_value({0x1010, 2, 0}, demand_array, layout, limits));
    limits = {};
    limits.max_elements = 6;  // 根数组、两个结构和四个字段共 7 个节点。
    CHECK(!standard::demand_values({0x1010, 2, 0}, demand_array, layout, limits));
    limits.max_elements = 7;
    CHECK(standard::demand_values({0x1010, 2, 0}, demand_array, layout, limits));

    const Data harmonics = Array{{Int16{345}, Int16{125}, Int16{-5}}};
    CHECK(standard::find_attribute({0x200d, 3, 0})->type == DataType::array);
    CHECK(standard::find_attribute({0x200d, 6, 0})->type == DataType::scaler_unit);
    CHECK(standard::harmonic_oad(0x200d, standard::Phase::b, 0, layout).value() ==
          Oad{0x200d, 3, 1});
    CHECK(standard::harmonic_oad(0x200e, standard::Phase::c, 3, layout).value() ==
          Oad{0x200e, 4, 3});
    CHECK(!standard::harmonic_oad(0x200d, standard::Phase::a, 1, layout));
    CHECK(!standard::harmonic_oad(0x200d, standard::Phase::a, 4, layout));
    CHECK(!standard::harmonic_oad(0x200d, standard::Phase::total, 0, layout));
    CHECK(!standard::harmonic_oad(0x200d, standard::Phase::b, 2, single));
    CHECK(!standard::harmonic_oad(0x2000, standard::Phase::a, 2, layout));
    CHECK(standard::validate_value({0x200d, 3, 0}, harmonics, layout));
    CHECK(!standard::validate_value({0x200d, 3, 0}, harmonics, single));
    CHECK(!standard::validate_value({0x200d, 2, 0}, Array{{Int16{1}, Int16{2}}}, layout));
    CHECK(!standard::validate_value({0x200d, 2, 2}, UInt16{125}, layout));
    CHECK(standard::decimal_text(
              standard::engineering_values({0x200e, 4, 3}, Int16{-5}, layout).value()[0]) ==
          "-0.05");
    CHECK(standard::make_oad(0x200d, 2, 255, {standard::Wiring::three_phase, 0, 255}));

    const Data statuses =
        Array{{BitString{16, {0x80, 0x04}}, BitString{16, {0, 0}}, BitString{16, {0, 0}},
               BitString{16, {0, 0}}, BitString{16, {0, 0}}, BitString{16, {0, 0}},
               BitString{16, {0, 0}}}};
    CHECK(standard::validate_value({0x2014, 2, 0}, statuses, single));
    CHECK(standard::validate_value({0x2014, 2, 7}, BitString{16, {0, 1}}));
    CHECK(!standard::make_oad(0x2014, 2, 8));
    CHECK(!standard::validate_value({0x2014, 2, 1}, BitString{8, {1}}));
    CHECK(!standard::validate_value({0x2014, 2, 1}, UInt16{1}));
    CHECK(!standard::validate_value({0x2014, 2, 0}, Array{{BitString{16, {0, 0}}}}));
    CHECK(!standard::engineering_values({0x2014, 2, 0}, statuses));
    CHECK(standard::validate_value({0x2015, 4, 0}, BitString{32, {0, 0, 0, 1}}));
    CHECK(!standard::validate_value({0x2015, 2, 0}, BitString{16, {0, 1}}));

    CHECK(standard::validate_value({0x4002, 2, 0}, OctetString{{1, 2, 3}}));
    CHECK(!standard::validate_value({0x4003, 2, 0}, VisibleString{"123"}));
    CHECK(standard::validate_value({0x4103, 2, 0}, VisibleString{std::string(32, 'A')}));
    CHECK(!standard::validate_value({0x4103, 2, 0}, VisibleString{std::string(31, 'A')}));
    CHECK(standard::validate_value({0x4104, 2, 0}, VisibleString{"220.0V"}));
    CHECK(!standard::find_attribute({0x4104, 2, 0})->writable);
    CHECK(standard::find_attribute({0x4100, 2, 0})->writable);
    CHECK(standard::validate_value({0x4006, 2, 0}, Structure{{Enum{3}, Enum{0}}}));
    CHECK(!standard::validate_value({0x4006, 2, 0}, Structure{{Enum{5}, Enum{0}}}));
    CHECK(!standard::validate_value({0x4006, 2, 1}, Enum{5}));
    CHECK(standard::validate_value({0x4000, 3, 0}, Enum{255}));
    CHECK(!standard::validate_value({0x4000, 3, 0}, Enum{3}));
    const Data timing = Structure{{UInt8{5}, UInt8{1}, UInt8{1}, UInt8{2}, UInt8{3}}};
    CHECK(standard::validate_value({0x4000, 4, 0}, timing));
    CHECK(standard::engineering_values({0x4000, 4, 0}, timing).value().size() == 1);
    CHECK(standard::unit_symbol(
              standard::engineering_values({0x4000, 4, 4}, UInt8{2}).value()[0].scaling.unit) ==
          "s");
    CHECK(!standard::validate_value({0x4010, 2, 0}, UInt8{4}));
    CHECK(standard::validate_value(
        {0x400c, 2, 0}, Structure{{UInt8{14}, UInt8{8}, UInt8{14}, UInt8{63}, UInt8{254}}}));
    CHECK(!standard::validate_value({0x400c, 2, 4}, UInt8{64}));
    CHECK(!standard::make_oad(0x400c, 2, 6));
    const Data voltage_limits = Structure{{UInt16{2500}, UInt16{1800}, UInt16{2400}, UInt16{1900}}};
    CHECK(standard::validate_value({0x4030, 2, 0}, voltage_limits));
    CHECK(standard::engineering_values({0x4030, 2, 0}, voltage_limits).value().size() == 4);
    CHECK(standard::decimal_text(
              standard::engineering_values({0x4030, 2, 3}, UInt16{2400}).value()[0]) == "240.0");
    CHECK(standard::decimal_text(
              standard::engineering_values({0x4100, 2, 0}, UInt8{15}).value()[0]) == "15");
    CHECK(standard::unit_symbol(34) == "kVAh" && standard::unit_symbol(28) == "kW" &&
          standard::unit_symbol(51) == "%");

    service::ObjectRegistry registry;
    auto provider = std::make_shared<service::MemoryObject>();
    provider->set(2, demand_array);
    CHECK(service::register_standard_object(registry, 0x1010, {2}, provider, layout));
    CHECK(std::get<Data>(registry.read({0x1010, 2, 1})) == item);
    CHECK(registry.write({0x1010, 2, 1}, item) == 3);
    provider->set(2, Array{{signed_item, signed_item}});
    CHECK(std::get<std::uint8_t>(registry.read({0x1010, 2, 1})) == 7);
    auto harmonic_provider = std::make_shared<service::MemoryObject>();
    harmonic_provider->set(3, harmonics);
    CHECK(service::register_standard_object(registry, 0x200d, {3}, harmonic_provider, layout));
    CHECK(std::get<Data>(registry.read({0x200d, 3, 2})) == Data{Int16{125}});
    CHECK(std::get<std::uint8_t>(registry.read({0x200d, 3, 4})) == 8);
    auto limits_provider = std::make_shared<service::MemoryObject>();
    limits_provider->set(2, voltage_limits);
    CHECK(service::register_standard_object(registry, 0x4030, {2}, limits_provider));
    CHECK(std::get<Data>(registry.read({0x4030, 2, 3})) == Data{UInt16{2400}});
    CHECK(registry.write({0x4030, 2, 3}, UInt16{2400}) == 3);
}

int main() {
    return tests([] {
        catalog_tests();
        addressing_tests();
        value_tests();
        binding_tests();
        wire_and_service_tests();
        expanded_tests();
    });
}
