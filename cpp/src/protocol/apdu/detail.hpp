/** @file detail.hpp
 * @brief APDU 编解码共用的存在标记、时间标签和字段检查。
 */
#pragma once
#include <dlt698/protocol/apdu/get.hpp>

namespace dlt698::protocol::apdu::detail {
/** @brief 抛出字段非法错误。
 * @param[in] offset 出错字节偏移。
 * @param[in] field 诊断字段名。
 * @throws DecodeFailure 始终抛出 invalid_value。
 */
inline void invalid(std::size_t offset, const char* field) {
    throw DecodeFailure({ErrorCode::invalid_value, offset, field});
}

/** @brief 检查 PIID 保留位。
 * @param[in] value PIID 字节。
 * @param[in] offset 字段偏移。
 * @throws DecodeFailure 第 6 位非零。
 */
inline void piid(std::uint8_t value, std::size_t offset) {
    if (value & 0x40) invalid(offset, "PIID reserved bit");
}

/** @brief 读取严格的 OPTIONAL 存在标记。
 * @param[in,out] reader 输入读取器。
 * @param[in] field 字段名。
 * @return 字段存在时为 true。
 * @throws DecodeFailure 标记不是 0/1 或输入不足。
 */
inline bool present(Reader& reader, const char* field) {
    const auto offset = reader.position();
    const auto value = reader.u8(field);
    if (value > 1) invalid(offset, field);
    return value != 0;
}

/** @brief 读取不含类型标签的固定字节字段。
 * @tparam T 含 value 字节数组的协议类型。
 * @param[in,out] reader 输入读取器。
 * @return 原样保存的字段。
 * @throws DecodeFailure 输入不足。
 */
template <class T>
T calendar(Reader& reader) {
    T value;
    for (auto& b : value.value) b = reader.u8("calendar field");
    return value;
}

/** @brief 读取可选时间标签。
 * @param[in,out] reader 输入读取器。
 * @return 时间标签或空值。
 * @throws DecodeFailure 字段非法或输入不足。
 */
inline std::optional<TimeTag> read_time_tag(Reader& reader) {
    if (!present(reader, "TimeTag presence")) return std::nullopt;
    TimeTag tag;
    tag.sent_at = calendar<model::DateTimeS>(reader);
    tag.allowed_delay.unit = reader.u8("TI unit");
    if (tag.allowed_delay.unit > 5) invalid(reader.position() - 1, "TI unit");
    tag.allowed_delay.interval = static_cast<std::uint16_t>(reader.be(2, "TI interval"));
    return tag;
}

/** @brief 写入可选时间标签。
 * @param[in,out] writer 输出写入器。
 * @param[in] tag 待写时间标签。
 * @throws DecodeFailure 字段非法或输出超限。
 */
inline void write_time_tag(Writer& writer, const std::optional<TimeTag>& tag) {
    writer.u8(tag ? 1 : 0);
    if (!tag) return;
    writer.bytes({tag->sent_at.value.data(), tag->sent_at.value.size()});
    if (tag->allowed_delay.unit > 5) invalid(writer.size(), "TI unit");
    writer.u8(tag->allowed_delay.unit);
    writer.be(tag->allowed_delay.interval, 2);
}

/** @brief 读取服务器 APDU 的无跟随上报标记。
 * @param[in,out] reader 输入读取器。
 * @throws DecodeFailure 标记非法、输入不足或存在未支持的 FollowReport。
 */
inline void no_follow(Reader& reader) {
    const auto offset = reader.position();
    if (present(reader, "FollowReport presence"))
        throw DecodeFailure({ErrorCode::unsupported_service, offset, "FollowReport"});
}
}  // namespace dlt698::protocol::apdu::detail
