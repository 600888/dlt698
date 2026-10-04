#include <dlt698/protocol/apdu/get.hpp>

namespace dlt698::protocol::apdu {
namespace {
std::optional<TimeTag> read_time_tag(Reader& reader) {
    const auto offset = reader.position();
    const auto present = reader.u8("TimeTag presence");
    // OPTIONAL 使用独立的 0/1 存在标记，不是任意非零值都表示存在。
    if (!present) return std::nullopt;
    if (present != 1) throw DecodeFailure({ErrorCode::invalid_value, offset, "TimeTag presence"});
    TimeTag tag;
    for (auto& b : tag.sent_at.value) b = reader.u8("TimeTag calendar");
    tag.allowed_delay.unit = reader.u8("TI unit");
    if (tag.allowed_delay.unit > 5)
        throw DecodeFailure({ErrorCode::invalid_value, reader.position() - 1, "TI unit"});
    tag.allowed_delay.interval = static_cast<std::uint16_t>(reader.be(2, "TI interval"));
    return tag;
}

void write_time_tag(Writer& writer, const std::optional<TimeTag>& tag) {
    writer.u8(tag ? 1 : 0);
    if (!tag) return;
    writer.bytes({tag->sent_at.value.data(), tag->sent_at.value.size()});
    if (tag->allowed_delay.unit > 5)
        throw DecodeFailure({ErrorCode::invalid_value, writer.size(), "TI unit"});
    writer.u8(tag->allowed_delay.unit);
    writer.be(tag->allowed_delay.interval, 2);
}

std::size_t read_count(Reader& r, bool list, const Limits& limits) {
    // Normal 隐含一个属性，NormalList 才在线上包含 A-XDR 数量字段；两者都不允许空列表。
    const auto count = list ? codec::read_length(r, limits.max_elements) : 1;
    if (!count || count > limits.max_elements)
        throw DecodeFailure({ErrorCode::invalid_length, r.position(), "GET attribute count"});
    return count;
}
}  // namespace

Result<GetApdu> decode_get(ByteView bytes, const Limits& limits) {
    if (bytes.size() > limits.max_data_bytes)
        return Error{ErrorCode::resource_limit, 0, "APDU input limit"};
    try {
        Reader reader(bytes);
        const auto service = reader.u8("GET service");
        if (service != 0x05 && service != 0x85)
            return Error{ErrorCode::unsupported_service, 0, "GET service"};
        const auto choice = reader.u8("GET choice");
        if (choice != 1 && choice != 2)
            return Error{ErrorCode::unsupported_service, 1, "GET variant"};
        const auto piid = reader.u8("PIID");
        if (service == 5 && (piid & 0x40))
            return Error{ErrorCode::invalid_value, 2, "PIID reserved bit"};
        const auto count = read_count(reader, choice == 2, limits);
        // 每个属性至少需要四字节 OAD，响应还需结果选择符及 DAR/Data。
        // 先用最低字节需求排除伪造的大数量，再分配属性列表。
        if (count > reader.remaining() / 4)
            return Error{ErrorCode::need_more_data, reader.position(), "GET attributes"};
        if (service == 5) {
            GetRequest request{piid, choice == 2, {}, {}};
            request.attributes.reserve(count);
            for (std::size_t i = 0; i < count; ++i)
                request.attributes.push_back(codec::read_oad(reader));
            request.time_tag = read_time_tag(reader);
            reader.finish();
            return GetApdu{std::move(request)};
        }
        GetResponse response{piid, choice == 2, {}, {}};
        response.attributes.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            const auto oad = codec::read_oad(reader);
            const auto offset = reader.position();
            const auto result_choice = reader.u8("GetResult choice");
            // 结果选择符 0 表示 DAR，1 表示带标签的 Data，不能将 DAR 当作布尔值。
            if (result_choice == 0)
                response.attributes.push_back({oad, reader.u8("DAR")});
            else if (result_choice == 1)
                response.attributes.push_back({oad, codec::read_data(reader, limits)});
            else
                return Error{ErrorCode::invalid_value, offset, "GetResult choice"};
        }
        const auto follow_offset = reader.position();
        const auto follow = reader.u8("FollowReport presence");
        // 未实现 FollowReport 的结构，遇到存在标记即返回不支持，避免误读后续时间标签。
        if (follow == 1)
            return Error{ErrorCode::unsupported_service, follow_offset, "FollowReport"};
        if (follow != 0)
            return Error{ErrorCode::invalid_value, follow_offset, "FollowReport presence"};
        response.time_tag = read_time_tag(reader);
        reader.finish();
        return GetApdu{std::move(response)};
    } catch (const DecodeFailure& e) {
        return e.error;
    }
}

Result<Bytes> encode_get(const GetApdu& apdu, const Limits& limits) {
    try {
        Writer writer(limits.max_data_bytes);
        std::visit(
            [&](const auto& message) {
                using T = std::decay_t<decltype(message)>;
                const auto count = message.attributes.size();
                if (!count || count > limits.max_elements || (!message.list && count != 1))
                    throw DecodeFailure({ErrorCode::invalid_length, 0, "GET attribute count"});
                if constexpr (std::is_same_v<T, GetRequest>) {
                    if (message.piid & 0x40)
                        throw DecodeFailure({ErrorCode::invalid_value, 2, "PIID reserved bit"});
                    writer.u8(5);
                    writer.u8(message.list ? 2 : 1);
                    writer.u8(message.piid);
                } else {
                    writer.u8(0x85);
                    writer.u8(message.list ? 2 : 1);
                    writer.u8(message.piid_acd);
                }
                if (message.list) codec::write_length(writer, count);
                for (const auto& attribute : message.attributes) {
                    if constexpr (std::is_same_v<T, GetRequest>)
                        codec::write_oad(writer, attribute);
                    else {
                        codec::write_oad(writer, attribute.attribute);
                        if (std::holds_alternative<std::uint8_t>(attribute.result)) {
                            writer.u8(0);
                            writer.u8(std::get<std::uint8_t>(attribute.result));
                        } else {
                            writer.u8(1);
                            codec::write_data(writer, std::get<model::Data>(attribute.result),
                                              limits);
                        }
                    }
                }
                // 响应中先写入 FollowReport 不存在标记，再写入可选时间标签。
                if constexpr (std::is_same_v<T, GetResponse>) writer.u8(0);
                write_time_tag(writer, message.time_tag);
            },
            apdu);
        return writer.take();
    } catch (const DecodeFailure& e) {
        return e.error;
    }
}
}  // namespace dlt698::protocol::apdu
