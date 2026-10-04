#include <dlt698/protocol/apdu/get_block.hpp>

namespace dlt698::protocol::apdu {
Result<std::vector<GetNextResponse>> GetBlockTransfer::split(GetSnapshot snapshot,
                                                             std::size_t target,
                                                             const Limits& limits) {
    std::vector<GetNextResponse> blocks;
    auto result = std::visit(
        [&](auto& response) -> Result<void> {
            using T = std::decay_t<decltype(response)>;
            auto& units = [&]() -> auto& {
                if constexpr (std::is_same_v<T, GetResponse>)
                    return response.attributes;
                else
                    return response.records;
            }();
            using Units = std::decay_t<decltype(units)>;
            if (units.empty() || units.size() > limits.max_elements)
                return Error{ErrorCode::resource_limit, 0, "GET snapshot units"};
            GetNextResponse current{response.piid_acd, false, 0, Units{}, response.time_tag};
            for (auto& unit : units) {
                auto& group = std::get<Units>(current.result);
                group.push_back(std::move(unit));
                auto bytes = encode_get(GetApdu{current}, limits);
                if ((!bytes || bytes.value().size() > target) && group.size() > 1) {
                    auto last = std::move(group.back());
                    group.pop_back();
                    if (!encode_get(GetApdu{current}, limits))
                        return Error{ErrorCode::resource_limit, 0, "GET indivisible unit"};
                    blocks.push_back(std::move(current));
                    if (blocks.size() >= 65536)
                        return Error{ErrorCode::resource_limit, 0, "GET block sequence exhausted"};
                    current = GetNextResponse{response.piid_acd, false,
                                              static_cast<std::uint16_t>(blocks.size()),
                                              Units{std::move(last)}, response.time_tag};
                    bytes = encode_get(GetApdu{current}, limits);
                }
                if (!bytes) return bytes.error();
            }
            current.last = true;
            blocks.push_back(std::move(current));
            return {};
        },
        snapshot);
    if (!result) return result.error();
    return blocks;
}

GetBlockTransfer::GetBlockTransfer(std::uint8_t piid, bool records, Limits limits)
    : piid_(piid), records_(records), limits_(limits) {}

Result<std::optional<GetSnapshot>> GetBlockTransfer::accept(const GetNextResponse& block) {
    if (finished_ || (block.piid_acd & 0xbf) != piid_ || block.block != next_)
        return Error{ErrorCode::invalid_value, 0, "GET block PIID/order"};
    if (const auto dar = std::get_if<std::uint8_t>(&block.result)) {
        finished_ = true;
        return Error{ErrorCode::remote_error, 0, "GET block DAR", *dar};
    }
    if (block.result.index() != (records_ ? 2u : 1u))
        return Error{ErrorCode::invalid_value, 0, "GET block result kind"};
    auto encoded = encode_get(GetApdu{block}, limits_);
    if (!encoded) return encoded.error();
    if (encoded.value().size() > limits_.max_data_bytes - bytes_)
        return Error{ErrorCode::resource_limit, 0, "GET aggregate bytes"};
    if (!block.last && next_ == 65535)
        return Error{ErrorCode::resource_limit, 0, "GET block sequence exhausted"};
    const auto n = records_ ? std::get<std::vector<RecordResult>>(block.result).size()
                            : std::get<std::vector<AttributeResult>>(block.result).size();
    const auto have = records_ ? records_data_.size() : attributes_.size();
    if (!n || n > limits_.max_elements - have)
        return Error{ErrorCode::resource_limit, 0, "GET aggregate units"};
    // 上限按实际传输字节累计（含每块表头），避免攻击者用小块绕过完整结果预算。
    bytes_ += encoded.value().size();
    ++next_;
    if (records_) {
        const auto& v = std::get<std::vector<RecordResult>>(block.result);
        records_data_.insert(records_data_.end(), v.begin(), v.end());
    } else {
        const auto& v = std::get<std::vector<AttributeResult>>(block.result);
        attributes_.insert(attributes_.end(), v.begin(), v.end());
    }
    if (!block.last) return std::optional<GetSnapshot>{};
    finished_ = true;
    if (records_)
        return std::optional<GetSnapshot>{
            GetRecordResponse{block.piid_acd, false, std::move(records_data_), block.time_tag}};
    return std::optional<GetSnapshot>{
        GetResponse{block.piid_acd, false, std::move(attributes_), block.time_tag}};
}
}  // namespace dlt698::protocol::apdu
