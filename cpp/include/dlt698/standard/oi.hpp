/**
 * @file oi.hpp
 * @brief DL/T 698.45—2017 首批常用对象标识常量。
 * @note OI 仅标识对象；属性及元素索引由 OAD 指定，设备支持情况须另行确认。
 */
#pragma once
#include <cstdint>

namespace dlt698::standard::oi {
/// 组合有功电能（附录 E.1）。
inline constexpr std::uint16_t combination_active_energy = 0x0000;
/// 正向有功电能（附录 E.1）。
inline constexpr std::uint16_t forward_active_energy = 0x0010;
/// 反向有功电能（附录 E.1）。
inline constexpr std::uint16_t reverse_active_energy = 0x0020;
/// 组合无功 1 电能（附录 E.1）。
inline constexpr std::uint16_t combination_reactive_energy_1 = 0x0030;
/// 组合无功 2 电能（附录 E.1）。
inline constexpr std::uint16_t combination_reactive_energy_2 = 0x0040;
/// 电压（附录 E.3）。
inline constexpr std::uint16_t voltage = 0x2000;
/// 电流（附录 E.3）。
inline constexpr std::uint16_t current = 0x2001;
/// 有功功率（附录 E.3）。
inline constexpr std::uint16_t active_power = 0x2004;
/// 无功功率（附录 E.3）。
inline constexpr std::uint16_t reactive_power = 0x2005;
/// 视在功率（附录 E.3）。
inline constexpr std::uint16_t apparent_power = 0x2006;
/// 功率因数（附录 E.3）。
inline constexpr std::uint16_t power_factor = 0x200a;
/// 电网频率（附录 E.3）。
inline constexpr std::uint16_t frequency = 0x200f;
/// 日期时间（附录 E.5）。
inline constexpr std::uint16_t date_time = 0x4000;
/// 电能表通信地址（附录 E.5）。
inline constexpr std::uint16_t communication_address = 0x4001;
}  // namespace dlt698::standard::oi
