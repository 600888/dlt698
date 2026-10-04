#include <algorithm>
#include <cmath>
#include <dlt698/codec/data_codec.hpp>
#include <dlt698/standard/catalog.hpp>

namespace dlt698::standard {
namespace {
using model::DataType;

AttributeDefinition scalar(std::uint8_t number, const char* name, DataType type, const char* source,
                           std::optional<model::ScalerUnit> scaling = {}) {
    return {number, name, type, {}, ArrayLayout::none, scaling, true, false, false, source};
}

AttributeDefinition array(std::uint8_t number, const char* name, DataType element,
                          ArrayLayout layout, model::ScalerUnit scaling, const char* source) {
    return {number, name, DataType::array, element, layout, scaling, true, false, false, source};
}

ObjectDefinition energy(std::uint16_t oi, const char* name, bool signed_value, std::uint8_t unit) {
    // 表 125：索引 1 是总量，之后是费率；附录 E.1 单独规定符号和精度。
    return {oi,
            name,
            1,
            "DL/T 698.45-2017",
            "附录 E.1 表 E.1",
            {scalar(1, "逻辑名", DataType::octet_string, "7.3.1 表 125"),
             array(2, "总及费率电能量", signed_value ? DataType::int32 : DataType::uint32,
                   ArrayLayout::total_tariffs, {-2, unit}, "7.3.1 表 125；附录 E.1"),
             scalar(3, "换算及单位", DataType::scaler_unit, "7.3.1 表 125"),
             array(4, "扩展精度总及费率电能量", signed_value ? DataType::int64 : DataType::uint64,
                   ArrayLayout::total_tariffs, {-4, unit}, "7.3.1 表 125；附录 E.1"),
             scalar(5, "扩展精度换算及单位", DataType::scaler_unit, "7.3.1 表 125")}};
}

ObjectDefinition variable(std::uint16_t oi, const char* name, std::uint8_t class_id, DataType type,
                          ArrayLayout layout, model::ScalerUnit scaling) {
    const char* source = class_id == 3   ? "7.3.3 表 130～131；附录 E.3"
                         : class_id == 4 ? "7.3.4 表 133～134；附录 E.3"
                                         : "7.3.6 表 139～140；附录 E.3";
    auto value = layout == ArrayLayout::none ? scalar(2, "数值", type, source, scaling)
                                             : array(2, "数值组", type, layout, scaling, source);
    ObjectDefinition object{oi,
                            name,
                            class_id,
                            "DL/T 698.45-2017",
                            "附录 E.3 表 E.3",
                            {scalar(1, "逻辑名", DataType::octet_string, source), std::move(value),
                             scalar(3, "换算及单位", DataType::scaler_unit, source)}};
    if (oi == 0x2001)
        object.attributes.push_back(
            scalar(4, "零线电流", DataType::int32, "附录 E.3 表 E.3", model::ScalerUnit{-3, 36}));
    return object;
}

std::size_t element_count(ArrayLayout array_layout, const DeviceLayout& layout) {
    // 单相功率依旧包含总量和 A 相两项；电能费率与相别数量无关。
    switch (array_layout) {
        case ArrayLayout::phases:
            return layout.wiring == Wiring::single_phase ? 1 : 3;
        case ArrayLayout::total_phases:
            return layout.wiring == Wiring::single_phase ? 2 : 4;
        case ArrayLayout::total_tariffs:
            return layout.tariff_count + 1;
        case ArrayLayout::none:
            return 0;
    }
    return 0;
}

ScaledNumber scaled(const model::Data& data, model::ScalerUnit scaling) {
    // 每一种标签先独立取出再提升整数宽度，UInt64 不经过有符号或浮点中间值。
    switch (data.type()) {
        case DataType::int16:
            return {std::int64_t{data.as<model::Int16>().value}, scaling};
        case DataType::int32:
            return {std::int64_t{data.as<model::Int32>().value}, scaling};
        case DataType::int64:
            return {data.as<model::Int64>().value, scaling};
        case DataType::uint16:
            return {std::uint64_t{data.as<model::UInt16>().value}, scaling};
        case DataType::uint32:
            return {std::uint64_t{data.as<model::UInt32>().value}, scaling};
        default:
            return {data.as<model::UInt64>().value, scaling};
    }
}
}  // namespace

const std::vector<ObjectDefinition>& objects() {
    // 唯一不可变数据源；构造函数局部静态表的初始化由 C++ 保证并发安全。
    static const auto catalog = [] {
        std::vector<ObjectDefinition> result{
            energy(0x0000, "组合有功电能", true, 33),
            energy(0x0010, "正向有功电能", false, 33),
            energy(0x0020, "反向有功电能", false, 33),
            energy(0x0030, "组合无功1电能", true, 35),
            energy(0x0040, "组合无功2电能", true, 35),
            variable(0x2000, "电压", 3, DataType::uint16, ArrayLayout::phases, {-1, 38}),
            variable(0x2001, "电流", 3, DataType::int32, ArrayLayout::phases, {-3, 36}),
            variable(0x2004, "有功功率", 4, DataType::int32, ArrayLayout::total_phases, {-1, 27}),
            variable(0x2005, "无功功率", 4, DataType::int32, ArrayLayout::total_phases, {-1, 31}),
            variable(0x2006, "视在功率", 4, DataType::int32, ArrayLayout::total_phases, {-1, 29}),
            variable(0x200a, "功率因数", 4, DataType::int16, ArrayLayout::total_phases, {-3, 255}),
            variable(0x200f, "电网频率", 6, DataType::uint16, ArrayLayout::none, {-2, 47})};
        for (const auto oi : {std::uint16_t{0x4000}, std::uint16_t{0x4001}}) {
            auto value = scalar(2, oi == 0x4000 ? "日期时间" : "通信地址",
                                oi == 0x4000 ? DataType::date_time_s : DataType::octet_string,
                                "附录 E.5 表 E.5");
            value.writable = true;
            result.push_back(
                {oi,
                 value.name,
                 8,
                 "DL/T 698.45-2017",
                 "附录 E.5 表 E.5",
                 {scalar(1, "逻辑名", DataType::octet_string, "7.3.8 表 145～146"), value}});
        }
        return result;
    }();
    return catalog;
}

const ObjectDefinition* find_object(std::uint16_t oi) {
    const auto& catalog = objects();
    const auto it = std::find_if(catalog.begin(), catalog.end(),
                                 [oi](const auto& object) { return object.oi == oi; });
    return it == catalog.end() ? nullptr : &*it;
}

const AttributeDefinition* find_attribute(const model::Oad& attribute) {
    const auto object = find_object(attribute.oi);
    if (!object) return nullptr;
    const auto it =
        std::find_if(object->attributes.begin(), object->attributes.end(),
                     [&](const auto& entry) { return entry.number == (attribute.attribute & 31); });
    return it == object->attributes.end() ? nullptr : &*it;
}

Result<void> validate_layout(const DeviceLayout& layout) {
    if ((layout.wiring != Wiring::single_phase && layout.wiring != Wiring::three_phase) ||
        layout.tariff_count > 254)
        return Error{ErrorCode::invalid_value, 0, "standard device layout"};
    return {};
}

Result<void> validate_oad(const model::Oad& attribute, const DeviceLayout& layout) {
    auto config = validate_layout(layout);
    if (!config) return config;
    const auto definition = find_attribute(attribute);
    if (!definition) return Error{ErrorCode::unsupported_tag, 0, "standard OI/attribute"};
    // schema 查低五位，特征位原样保留；本批不对快照特征伪造语义。
    if (attribute.attribute & 0xe0)
        return Error{ErrorCode::unsupported_service, 0, "standard attribute feature"};
    if (attribute.index && attribute.index > element_count(definition->layout, layout))
        return Error{ErrorCode::invalid_length, 0, "standard element index"};
    return {};
}

Result<model::Oad> make_oad(std::uint16_t oi, std::size_t attribute, std::size_t index,
                            const DeviceLayout& layout) {
    if (!attribute || attribute > 31 || index > 255)
        return Error{ErrorCode::invalid_value, 0, "standard OAD fields"};
    const model::Oad oad{oi, static_cast<std::uint8_t>(attribute),
                         static_cast<std::uint8_t>(index)};
    auto valid = validate_oad(oad, layout);
    if (!valid) return valid.error();
    return oad;
}

Result<model::Oad> phase_oad(std::uint16_t oi, Phase phase, const DeviceLayout& layout) {
    const auto definition = find_attribute({oi, 2, 0});
    if (!definition ||
        (definition->layout != ArrayLayout::phases &&
         definition->layout != ArrayLayout::total_phases) ||
        phase < Phase::total || phase > Phase::c ||
        (phase == Phase::total && definition->layout == ArrayLayout::phases) ||
        (layout.wiring == Wiring::single_phase && (phase == Phase::b || phase == Phase::c)))
        return Error{ErrorCode::invalid_value, 0, "standard phase selection"};
    const auto index =
        static_cast<std::size_t>(phase) + (definition->layout == ArrayLayout::total_phases ? 1 : 0);
    return make_oad(oi, 2, index, layout);
}

Result<model::Oad> tariff_oad(std::uint16_t oi, std::size_t tariff, const DeviceLayout& layout,
                              bool high_precision) {
    auto config = validate_layout(layout);
    if (!config) return config.error();
    const std::uint8_t attribute = high_precision ? 4 : 2;
    const auto definition = find_attribute({oi, attribute, 0});
    if (!definition || definition->layout != ArrayLayout::total_tariffs ||
        tariff > layout.tariff_count)
        return Error{ErrorCode::invalid_value, 0, "standard tariff selection"};
    return make_oad(oi, attribute, tariff + 1, layout);
}

Result<void> validate_value(const model::Oad& attribute, const model::Data& value,
                            const DeviceLayout& layout, const Limits& limits) {
    auto valid = validate_oad(attribute, layout);
    if (!valid) return valid;
    const auto definition = find_attribute(attribute);
    const auto expected = attribute.index ? *definition->element_type : definition->type;
    if (value.type() != expected) return Error{ErrorCode::invalid_value, 0, "standard Data type"};
    if (!attribute.index && definition->type == DataType::array) {
        const auto& elements = value.as<model::Array>().value;
        if (elements.size() > limits.max_elements)
            return Error{ErrorCode::resource_limit, 0, "standard array elements"};
        if (elements.size() != element_count(definition->layout, layout))
            return Error{ErrorCode::invalid_length, 0, "standard array layout"};
        for (std::size_t i = 0; i < elements.size(); ++i)
            if (elements[i].type() != *definition->element_type)
                return Error{ErrorCode::invalid_value, i, "standard array element type"};
    }
    // 复用 codec 的深度/元素/字节限制；例如地址字符串也受总字节限制。
    auto encoded = codec::encode_data(value, limits);
    if (!encoded) return encoded.error();
    return {};
}

Result<std::vector<ScaledNumber>> engineering_values(const model::Oad& attribute,
                                                     const model::Data& value,
                                                     const DeviceLayout& layout,
                                                     const Limits& limits) {
    auto valid = validate_value(attribute, value, layout, limits);
    if (!valid) return valid.error();
    const auto scaling = find_attribute(attribute)->scaling;
    if (!scaling) return Error{ErrorCode::unsupported_tag, 0, "non-numeric standard attribute"};
    std::vector<ScaledNumber> result;
    if (value.type() == DataType::array) {
        const auto& elements = value.as<model::Array>().value;
        result.reserve(elements.size());
        for (const auto& element : elements) result.push_back(scaled(element, *scaling));
    } else {
        result.push_back(scaled(value, *scaling));
    }
    return result;
}

std::string decimal_text(const ScaledNumber& number) {
    auto text = std::visit([](auto raw) { return std::to_string(raw); }, number.raw);
    const bool negative = text.front() == '-';
    if (negative) text.erase(0, 1);
    const int scaler = number.scaling.scaler;
    if (scaler >= 0) {
        if (text != "0") text.append(static_cast<std::size_t>(scaler), '0');
    } else {
        // 直接插入小数点；先提升到 int 再取负，避免 -128 的一字节溢出。
        const auto places = static_cast<std::size_t>(-scaler);
        if (text.size() <= places) text.insert(0, places + 1 - text.size(), '0');
        text.insert(text.size() - places, 1, '.');
    }
    if (negative) text.insert(0, 1, '-');
    return text;
}

double approximate_value(const ScaledNumber& number) {
    return std::visit([](auto raw) { return static_cast<double>(raw); }, number.raw) *
           std::pow(10.0, static_cast<int>(number.scaling.scaler));
}

std::string_view unit_symbol(std::uint8_t unit) {
    switch (unit) {
        case 27:
            return "W";
        case 29:
            return "VA";
        case 31:
            return "var";
        case 33:
            return "kWh";
        case 35:
            return "kvarh";
        case 36:
            return "A";
        case 38:
            return "V";
        case 47:
            return "Hz";
        case 255:
            return "";
        default:
            return "";
    }
}
}  // namespace dlt698::standard
