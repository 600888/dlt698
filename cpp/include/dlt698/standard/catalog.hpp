/**
 * @file catalog.hpp
 * @brief DL/T 698.45—2017 常用标准对象、点位寻址和精确工程值。
 */
#pragma once
#include <dlt698/model/data.hpp>
#include <optional>

namespace dlt698::standard {
/// 数组语义；标量使用 none，数组顺序由接口类定义。
enum class ArrayLayout { none, phases, total_phases, total_tariffs };
enum class Wiring { single_phase, three_phase };
enum class Phase { total, a, b, c };

/// 本地设备配置，不代表远端能力；单相按标准的 A（某一相）位置表示。
struct DeviceLayout {
    Wiring wiring = Wiring::three_phase;
    std::size_t tariff_count = 4;  ///< 费率数 0～254，不含总量；默认四费率须与设备配置一致。
};

/// 单个属性的标准定义，运行时写权限仍由应用另行授权。
struct AttributeDefinition {
    std::uint8_t number;
    const char* name;
    model::DataType type;
    std::optional<model::DataType> element_type;
    ArrayLayout layout = ArrayLayout::none;
    std::optional<model::ScalerUnit> scaling;
    bool readable = true;
    bool writable = false;  ///< 本批标准量只读；4000/4001 的参数值允许由应用显式授权写入。
    bool record = false;
    const char* source = "";  ///< 标准章节/表号；字符串随目录存活。
};

/// 常用 OI 定义；只列本批支持的属性，不声称覆盖整个接口类及其可选方法。
struct ObjectDefinition {
    std::uint16_t oi;
    const char* name;
    std::uint8_t class_id;
    const char* version;
    const char* source;
    std::vector<AttributeDefinition> attributes;
};

/// 精确工程值：raw × 10^scaling.scaler；无默认浮点转换。
struct ScaledNumber {
    std::variant<std::int64_t, std::uint64_t> raw;
    model::ScalerUnit scaling;
};

/**
 * @brief 枚举首批 14 个不可变标准对象。
 * @return 进程生命周期内有效的常量引用；首次调用可能分配内存。
 * @note 查到定义不代表远端设备支持或本地已注册 provider；并发只读安全。
 */
DLT698_API const std::vector<ObjectDefinition>& objects();
/**
 * @brief 按 OI 查找本批标准定义。
 * @param[in] oi 对象标识，包括合法的 0x0000。
 * @return 不可变目录中的指针，进程生命周期内有效；未收录返回 nullptr。
 */
DLT698_API const ObjectDefinition* find_object(std::uint16_t oi);
/**
 * @brief 按 OI 和属性低五位查找定义，不验证特征和元素索引。
 * @param[in] attribute 保留完整线上字节的 OAD。
 * @return 进程生命周期内有效的定义指针；未收录返回 nullptr。
 */
DLT698_API const AttributeDefinition* find_attribute(const model::Oad& attribute);
/**
 * @brief 校验设备接线方式及费率数。
 * @param[in] layout 本地配置；费率数不能超过一字节索引可表达的 254。
 * @return 成功或 invalid_value。
 */
DLT698_API Result<void> validate_layout(const DeviceLayout& layout);
/**
 * @brief 验证本批标准 OAD 的属性、特征和索引。
 * @param[in] attribute 原始 OAD；本批只支持特征零。
 * @param[in] layout 数组实例的接线方式和费率数。
 * @return 成功；未知定义为 unsupported_tag，非零特征为 unsupported_service，索引错误为 invalid_length。
 */
DLT698_API Result<void> validate_oad(const model::Oad& attribute, const DeviceLayout& layout = {});
/**
 * @brief 构造特征零的标准 OAD，不截断超范围编号或索引。
 * @param[in] oi 本批标准对象标识。
 * @param[in] attribute 属性编号 1～31。
 * @param[in] index 0 取整体；非零为从 1 开始的一级元素索引。
 * @param[in] layout 本地数组实例配置。
 * @return 合法 OAD 或未知定义、配置/索引错误。
 */
DLT698_API Result<model::Oad> make_oad(std::uint16_t oi, std::size_t attribute = 2,
                                       std::size_t index = 0, const DeviceLayout& layout = {});
/**
 * @brief 构造电压、电流或功率类的相别点位。
 * @param[in] oi 本批分相或总及分相对象标识。
 * @param[in] phase 总量或 A/B/C；分相量没有总量，单相没有 B/C。
 * @param[in] layout 设备接线方式及费率数。
 * @return 属性 2 的元素 OAD，非法相别/对象返回 invalid_value。
 */
DLT698_API Result<model::Oad> phase_oad(std::uint16_t oi, Phase phase,
                                        const DeviceLayout& layout = {});
/**
 * @brief 构造总电能或费率电能的元素点位。
 * @param[in] oi 本批电能对象标识。
 * @param[in] tariff 0 为总量，1～layout.tariff_count 为费率。
 * @param[in] layout 设备配置，费率数 0～254。
 * @param[in] high_precision true 使用属性 4，false 使用属性 2。
 * @return 元素索引为 tariff+1 的 OAD；非法对象/费率返回错误。
 */
DLT698_API Result<model::Oad> tariff_oad(std::uint16_t oi, std::size_t tariff,
                                         const DeviceLayout& layout = {},
                                         bool high_precision = false);
/**
 * @brief 校验标准返回值的精确标签、数组长度和每个元素，以及资源限制。
 * @param[in] attribute 值对应的完整 OAD；索引非零时校验元素类型。
 * @param[in] value 拥有内存的原始值，调用期间借用；不执行浮点或日历转换。
 * @param[in] layout 数组长度由本地设备配置确定。
 * @param[in] limits 编码深度、元素和总字节限制。
 * @return 成功或 OAD/类型/长度/资源错误；不将 Data 错误伪装为测点不存在。
 */
DLT698_API Result<void> validate_value(const model::Oad& attribute, const model::Data& value,
                                       const DeviceLayout& layout = {}, const Limits& limits = {});
/**
 * @brief 使用 2017 标准默认倍率生成拥有内存的精确数值列表，数组按原顺序逐元素输出。
 * @param[in] attribute 对应数值属性或其元素的 OAD。
 * @param[in] value 原始 Data；函数不修改该值。
 * @param[in] layout 用于校验数组长度的设备配置。
 * @param[in] limits 校验资源上限。
 * @return 数值列表；日期、地址、逻辑名、ScalerUnit 属性返回 unsupported_tag。
 * @note 倍率来自目录；若设备单位属性与标准不一致，调用方须处理差异后再使用结果。
 */
DLT698_API Result<std::vector<ScaledNumber>> engineering_values(const model::Oad& attribute,
                                                                const model::Data& value,
                                                                const DeviceLayout& layout = {},
                                                                const Limits& limits = {});
/**
 * @brief 将整数及十进制倍率转为精确十进制文本，不使用浮点中间值。
 * @param[in] number 原始整数及有符号一字节倍率，不附加单位。
 * @return 拥有内存的十进制文本，保留倍率指定的小数位。
 */
DLT698_API std::string decimal_text(const ScaledNumber& number);
/**
 * @brief 显式转换为便于显示的近似浮点工程值。
 * @param[in] number 原始整数及倍率。
 * @return double 近似值；64 位整数及十进制小数可能损失精度。
 */
DLT698_API double approximate_value(const ScaledNumber& number);
/**
 * @brief 查询本批使用的附录 B 单位名称。
 * @param[in] unit 单位枚举代码；255 为无单位。
 * @return 指向静态字符串的视图，进程生命周期内有效；未收录单位返回空视图。
 */
DLT698_API std::string_view unit_symbol(std::uint8_t unit);
}  // namespace dlt698::standard
