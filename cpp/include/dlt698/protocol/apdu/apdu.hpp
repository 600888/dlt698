/** @file apdu.hpp
 * @brief 当前已支持 APDU 的统一分发入口。
 */
#pragma once
#include <dlt698/protocol/apdu/connection.hpp>
#include <dlt698/protocol/apdu/mutation.hpp>

namespace dlt698::protocol::apdu {
using Apdu =
    std::variant<LinkRequest, LinkResponse, ConnectRequest, ConnectResponse, ReleaseRequest,
                 ReleaseResponse, ReleaseNotification, ErrorResponse, GetRequest, GetResponse,
                 SetRequest, SetResponse, ActionRequest, ActionResponse>;
/** @brief 解码当前支持的一个完整 APDU。
 * @param[in] bytes 完整 APDU 字节。
 * @param[in] limits 编解码资源上限。
 * @return 精确类型消息或不支持、语法及资源错误。
 */
DLT698_API Result<Apdu> decode_apdu(ByteView bytes, const Limits& limits = {});
/** @brief 编码当前支持的一个完整 APDU。
 * @param[in] message 精确类型消息。
 * @param[in] limits 编解码资源上限。
 * @return 编码字节或字段及资源错误。
 */
DLT698_API Result<Bytes> encode_apdu(const Apdu& message, const Limits& limits = {});
}  // namespace dlt698::protocol::apdu
