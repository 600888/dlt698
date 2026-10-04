/**
 * @file get.hpp
 * @brief GET Normal/NormalList 请求、响应及其 APDU 编解码。
 */
#pragma once
#include <dlt698/codec/data_codec.hpp>
#include <optional>

namespace dlt698::protocol::apdu {
/// 可选时间标签，日期时间保持协议原始字段，不附加本机时区。
struct TimeTag {
    model::DateTimeS sent_at;  ///< 发送时间。
    model::Ti allowed_delay;   ///< 允许的传输延时。
};

/// GET 请求，Normal 恰含一个属性，NormalList 必须为非空列表。
struct GetRequest {
    std::uint8_t piid = 0;  ///< 优先级位及低六位调用标识，第 6 位保留且须为零。
    bool list = false;
    std::vector<model::Oad> attributes;
    std::optional<TimeTag> time_tag;
};

/// 单个属性的读取结果，DAR 与精确类型 Data 分开保存。
struct AttributeResult {
    model::Oad attribute;
    std::variant<std::uint8_t, model::Data> result;  ///< uint8_t 为 DAR，Data 为属性值。
};

/// GET 响应，仅支持无 FollowReport 的 Normal/NormalList 形式。
struct GetResponse {
    std::uint8_t piid_acd = 0;
    bool list = false;
    std::vector<AttributeResult> attributes;
    // 当前未实现 FollowReport；解码带该字段的响应返回 unsupported_service。
    std::optional<TimeTag> time_tag;
};

using GetApdu = std::variant<GetRequest, GetResponse>;
/**
 * @brief 解码完整的 GET 请求或响应 APDU。
 * @param[in] bytes 恰好一个 APDU，不包含链路帧封装或尾随字节。
 * @param[in] limits APDU 字节数、属性数及每个 Data 树的资源上限。
 * @return GET 请求或响应；其他服务、变体或 FollowReport 返回 unsupported_service。
 * @note 也检查 PIID 保留位、属性结果选择符和可选时间标签的合法性。
 */
DLT698_API Result<GetApdu> decode_get(ByteView bytes, const Limits& limits = {});
/**
 * @brief 编码 GET Normal/NormalList 请求或响应。
 * @param[in] apdu 待编码消息；Normal 恰含一个属性，NormalList 至少含一个属性。
 * @param[in] limits 输出字节数、属性数及每个 Data 树的资源上限。
 * @return APDU 字节序列，或属性数、字段值非法及资源超限等错误。
 * @note 响应始终写入 FollowReport 不存在的标记。
 */
DLT698_API Result<Bytes> encode_get(const GetApdu& apdu, const Limits& limits = {});
}  // namespace dlt698::protocol::apdu
