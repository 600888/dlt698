#include <algorithm>
#include <atomic>
#include <ctime>
#include <dlt698/session/session.hpp>

namespace dlt698::session {
namespace {
namespace apdu = protocol::apdu;
namespace link = protocol::link;
using Clock = IExecutor::Clock;

template <class H, class R>
void deliver(H handler, R result) noexcept {
    try {
        if (handler) handler(std::move(result));
    } catch (...) {
    }
}

model::DateTime utc_calendar() {
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - seconds).count();
    const auto raw = std::chrono::system_clock::to_time_t(seconds);
    std::tm value{};
    // 使用线程安全的 UTC 转换，不让宿主本地时区影响协议日历字段。
#ifdef _WIN32
    if (gmtime_s(&value, &raw)) throw std::runtime_error("UTC calendar conversion");
#else
    if (!gmtime_r(&raw, &value)) throw std::runtime_error("UTC calendar conversion");
#endif
    const auto year = static_cast<unsigned>(value.tm_year + 1900);
    return {{static_cast<std::uint8_t>(year >> 8), static_cast<std::uint8_t>(year),
             static_cast<std::uint8_t>(value.tm_mon + 1), static_cast<std::uint8_t>(value.tm_mday),
             static_cast<std::uint8_t>(value.tm_wday ? value.tm_wday : 7),
             static_cast<std::uint8_t>(value.tm_hour), static_cast<std::uint8_t>(value.tm_min),
             static_cast<std::uint8_t>(value.tm_sec), static_cast<std::uint8_t>(ms >> 8),
             static_cast<std::uint8_t>(ms)}};
}

model::DateTimeS seconds_calendar(const model::DateTime& value) {
    const auto& b = value.value;
    return {{b[0], b[1], b[2], b[3], b[5], b[6], b[7]}};
}

bool valid_parameters(const apdu::AssociationParameters& p) {
    return p.send_frame_bytes >= 10 && p.send_frame_bytes <= 16383 && p.receive_frame_bytes >= 10 &&
           p.receive_frame_bytes <= 16383 && p.receive_window && p.apdu_bytes && p.timeout_seconds;
}

std::uint8_t id_of(const apdu::Apdu& message) {
    return std::visit(
        [](const auto& v) -> std::uint8_t {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, apdu::LinkRequest> ||
                          std::is_same_v<T, apdu::ConnectResponse> ||
                          std::is_same_v<T, apdu::ReleaseResponse> ||
                          std::is_same_v<T, apdu::ReleaseNotification> ||
                          std::is_same_v<T, apdu::GetResponse> ||
                          std::is_same_v<T, apdu::SetResponse> ||
                          std::is_same_v<T, apdu::ActionResponse>)
                return v.piid_acd & 0xbf;
            else
                return v.piid;
        },
        message);
}

template <class T>
Result<T> typed(Result<apdu::Apdu> result) {
    if (!result) return result.error();
    return std::get<T>(std::move(result).value());
}
}  // namespace

struct Session::Impl : std::enable_shared_from_this<Impl> {
    enum class Kind { link, connect, get, set, action, release };

    struct Pending {
        Kind kind;
        std::uint8_t id;
        std::function<void(Result<apdu::Apdu>)> handler;
        std::shared_ptr<ITimer> timer;
        apdu::Apdu request;
    };

    std::shared_ptr<transport::IChannel> channel;
    std::shared_ptr<IExecutor> executor;
    SessionOptions options;
    link::FrameStreamDecoder decoder;
    std::atomic<State> state{State::disconnected};
    bool started = false;
    std::optional<Pending> pending;
    std::array<Clock::time_point, 64> reusable{};
    unsigned next_id = 0;
    RequestHandler request_handler;
    SetRequestHandler set_handler;
    ActionRequestHandler action_handler;
    DiagnosticHandler diagnostic;
    apdu::AssociationParameters agreement;
    std::shared_ptr<ITimer> idle_timer;
    model::DateTimeS established_at;

    Impl(std::shared_ptr<transport::IChannel> ch, std::shared_ptr<IExecutor> ex, SessionOptions opt)
        : channel(std::move(ch)),
          executor(std::move(ex)),
          options(std::move(opt)),
          decoder(options.limits),
          agreement(options.parameters) {}

    ~Impl() {
        if (idle_timer) idle_timer->cancel();
        channel->close();
        if (pending) {
            pending->timer->cancel();
            auto handler = std::move(pending->handler);
            executor->post([handler = std::move(handler)]() mutable {
                deliver(std::move(handler),
                        Result<apdu::Apdu>{Error{ErrorCode::closed, 0, "session destroyed"}});
            });
        }
    }

    std::optional<model::DateTime> calendar() {
        try {
            return options.calendar_clock ? options.calendar_clock() : utc_calendar();
        } catch (...) {
            // 时钟回调失败不能被执行器隔离后留下无响应的事务，也不能发送伪造时间。
            Error error{ErrorCode::invalid_value, 0, "calendar clock failed"};
            report(error);
            shutdown(error);
            return {};
        }
    }

    void report(const Error& error) {
        try {
            if (diagnostic) diagnostic(error);
        } catch (...) {
        }
    }

    void complete(Result<apdu::Apdu> result) {
        if (!pending) return;
        auto item = std::move(*pending);
        pending.reset();
        item.timer->cancel();
        // 成功或被释放打断的序号均进入隔离期，避免序号循环后立即命中重复响应。
        const auto now = executor->now();
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::time_point::max() - now);
        reusable[item.id] = options.id_reuse_delay < remaining ? now + options.id_reuse_delay
                                                               : Clock::time_point::max();
        deliver(std::move(item.handler), std::move(result));
    }

    void shutdown(Error error) {
        if (state == State::closed) return;
        state = State::closed;
        decoder.reset();
        if (idle_timer) idle_timer->cancel();
        channel->close();
        complete(std::move(error));
    }

    void leave_association(Error error, State target = State::preconnected) {
        // 发送释放通知/应答可能已关闭物理通道，失败终态不能被后续迁移覆盖。
        if (state == State::closed) return;
        state = target;
        if (idle_timer) idle_timer->cancel();
        complete(std::move(error));
    }

    void touch() {
        if (idle_timer) idle_timer->cancel();
        if (state != State::associated) return;
        const std::weak_ptr<Impl> weak = shared_from_this();
        try {
            idle_timer =
                executor->schedule(std::chrono::seconds(agreement.timeout_seconds), [weak] {
                    if (const auto self = weak.lock()) {
                        if (self->state != State::associated) return;
                        if (self->options.role == Role::server) {
                            apdu::ReleaseNotification notification;
                            notification.established_at = self->established_at;
                            const auto now = self->calendar();
                            if (!now) return;
                            notification.current_time = seconds_calendar(*now);
                            self->send(notification);
                        }
                        self->leave_association(
                            {ErrorCode::not_associated, 0, "association idle timeout"});
                    }
                });
        } catch (...) {
            Error error{ErrorCode::invalid_value, 0, "association timer range"};
            report(error);
            shutdown(error);
        }
    }

    Limits codec_limits(bool active) const {
        auto limits = options.limits;
        if (active)
            limits.max_data_bytes =
                std::min<std::size_t>(limits.max_data_bytes, agreement.apdu_bytes);
        return limits;
    }

    Result<Bytes> wire(const apdu::Apdu& message) {
        const bool link_message = std::holds_alternative<apdu::LinkRequest>(message) ||
                                  std::holds_alternative<apdu::LinkResponse>(message);
        const bool server = options.role == Role::server;
        bool client_initiated =
            !link_message && !std::holds_alternative<apdu::ReleaseNotification>(message);
        if (std::holds_alternative<apdu::ErrorResponse>(message)) client_initiated = server;
        auto encoded = apdu::encode_apdu(
            message, codec_limits(state == State::associated || state == State::releasing));
        if (!encoded) return encoded.error();
        link::Frame frame;
        frame.server = options.server;
        frame.client = options.client_address;
        frame.control = static_cast<std::uint8_t>(
            (server ? 0x80 : 0) | (client_initiated ? 0x40 : 0) | (link_message ? 1 : 3));
        frame.payload = std::move(encoded).value();
        auto limits = options.limits;
        const auto max_length = state == State::associated || state == State::releasing
                                    ? agreement.send_frame_bytes
                                    : options.parameters.send_frame_bytes;
        limits.max_frame_bytes =
            std::min<std::size_t>(limits.max_frame_bytes, static_cast<std::size_t>(max_length) + 2);
        return link::encode_frame(frame, limits);
    }

    void send(const apdu::Apdu& message) {
        auto bytes = wire(message);
        if (!bytes) {
            report(bytes.error());
            shutdown(bytes.error());
            return;
        }
        const std::weak_ptr<Impl> weak = shared_from_this();
        channel->async_write(std::move(bytes).value(), [weak](Result<void> result) {
            if (const auto self = weak.lock())
                self->executor->post([weak, result = std::move(result)] {
                    if (const auto self = weak.lock())
                        if (!result) self->shutdown(result.error());
                });
        });
    }

    void submit(Kind kind, apdu::Apdu message, std::function<void(Result<apdu::Apdu>)> handler) {
        if (!started || state == State::closed) {
            deliver(std::move(handler),
                    Result<apdu::Apdu>{Error{ErrorCode::closed, 0, "session not running"}});
            return;
        }
        if (pending) {
            deliver(std::move(handler),
                    Result<apdu::Apdu>{Error{ErrorCode::busy, 0, "session transaction"}});
            return;
        }
        unsigned id = 64;
        for (unsigned n = 0; n < 64; ++n) {
            const auto candidate = (next_id + n) % 64;
            if (reusable[candidate] <= executor->now()) {
                id = candidate;
                break;
            }
        }
        if (id == 64) {
            if (kind == Kind::release) {
                state = State::associated;
                touch();
            }
            deliver(std::move(handler),
                    Result<apdu::Apdu>{Error{ErrorCode::resource_limit, 0, "PIID quarantine"}});
            return;
        }
        next_id = (id + 1) % 64;
        std::visit(
            [&](auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, apdu::LinkRequest>)
                    v.piid_acd = static_cast<std::uint8_t>(id);
                else if constexpr (std::is_same_v<T, apdu::ConnectRequest> ||
                                   std::is_same_v<T, apdu::GetRequest> ||
                                   std::is_same_v<T, apdu::ReleaseRequest> ||
                                   std::is_same_v<T, apdu::SetRequest> ||
                                   std::is_same_v<T, apdu::ActionRequest>)
                    v.piid = static_cast<std::uint8_t>(id);
            },
            message);
        auto bytes = wire(message);
        if (!bytes) {
            if (kind == Kind::release) {
                state = State::associated;
                touch();
            }
            deliver(std::move(handler), Result<apdu::Apdu>{bytes.error()});
            return;
        }
        if (kind == Kind::connect) state = State::associating;
        if (kind == Kind::release) {
            state = State::releasing;
            if (idle_timer) idle_timer->cancel();
        }
        const std::weak_ptr<Impl> weak = shared_from_this();
        std::shared_ptr<ITimer> timer;
        try {
            timer = executor->schedule(options.request_timeout, [weak] {
                if (const auto self = weak.lock())
                    self->shutdown({ErrorCode::timeout, 0, "session transaction timeout"});
            });
        } catch (...) {
            if (kind == Kind::connect) state = State::preconnected;
            if (kind == Kind::release) {
                state = State::associated;
                touch();
            }
            deliver(std::move(handler), Result<apdu::Apdu>{Error{ErrorCode::invalid_value, 0,
                                                                 "transaction timer range"}});
            return;
        }
        pending.emplace(Pending{kind, static_cast<std::uint8_t>(id), std::move(handler),
                                std::move(timer), std::move(message)});
        channel->async_write(std::move(bytes).value(), [weak](Result<void> result) {
            if (const auto self = weak.lock())
                self->executor->post([weak, result = std::move(result)] {
                    if (const auto self = weak.lock())
                        if (!result) self->shutdown(result.error());
                });
        });
    }

    template <class Request, class Response, class Handler>
    void post_mutation(Request request, Kind kind, unsigned capability, Handler handler) {
        const std::weak_ptr<Impl> weak = shared_from_this();
        executor->post([weak, request = std::move(request), kind, capability,
                        handler = std::move(handler)]() mutable {
            const auto self = weak.lock();
            if (!self || self->state == State::closed) {
                deliver(std::move(handler),
                        Result<Response>{Error{ErrorCode::closed, 0, "mutation closed"}});
                return;
            }
            if (self->options.role != Role::client || self->state != State::associated) {
                deliver(std::move(handler), Result<Response>{Error{ErrorCode::not_associated, 0,
                                                                   "mutation state/role"}});
                return;
            }
            if (!(self->agreement.protocol[capability / 8] & (0x80 >> (capability % 8)))) {
                deliver(std::move(handler), Result<Response>{Error{ErrorCode::unsupported_service,
                                                                   0, "mutation not negotiated"}});
                return;
            }
            self->submit(kind, std::move(request),
                         [handler = std::move(handler)](Result<apdu::Apdu> r) mutable {
                             deliver(std::move(handler), typed<Response>(std::move(r)));
                         });
        });
    }

    template <class Request, class Response, class Handler>
    void serve_mutation(const Request& request, Handler& handler, unsigned capability) {
        if (options.role != Role::server) return;
        if (state != State::associated || request.time_tag ||
            !(agreement.protocol[capability / 8] & (0x80 >> (capability % 8)))) {
            send(apdu::ErrorResponse{true, request.piid, 2, {}});
            return;
        }
        touch();
        if (state == State::closed) return;
        try {
            Response response;
            if (handler)
                response = handler(request);
            else if constexpr (std::is_same_v<Request, apdu::SetRequest>) {
                for (const auto& item : request.attributes)
                    response.attributes.push_back({item.attribute, 3});
            } else {
                for (const auto& item : request.methods)
                    response.methods.push_back({item.method, 3, {}});
            }
            response.piid_acd = request.piid;
            response.list = request.list;
            response.time_tag.reset();
            send(response);
        } catch (...) {
            send(apdu::ErrorResponse{true, request.piid, 255, {}});
        }
    }

    template <class Request, class Response>
    void accept_mutation(apdu::Apdu message) {
        const auto& request = std::get<Request>(pending->request);
        const auto& response = std::get<Response>(message);
        bool matches = request.list == response.list && !response.time_tag;
        if constexpr (std::is_same_v<Request, apdu::SetRequest>) {
            matches = matches && request.attributes.size() == response.attributes.size();
            if (matches)
                for (std::size_t i = 0; i < request.attributes.size(); ++i)
                    if (!(request.attributes[i].attribute == response.attributes[i].attribute))
                        matches = false;
        } else {
            matches = matches && request.methods.size() == response.methods.size();
            if (matches)
                for (std::size_t i = 0; i < request.methods.size(); ++i)
                    if (!(request.methods[i].method == response.methods[i].method)) matches = false;
        }
        // PIID 相同仍必须核对完整 OAD/OMD 和列表顺序，避免将另一项的执行结果交给调用方。
        if (!matches) {
            report({ErrorCode::invalid_value, 0, "mutation descriptor/list mismatch"});
            return;
        }
        touch();
        complete(std::move(message));
    }

    void read() {
        if (state == State::closed) return;
        const std::weak_ptr<Impl> weak = shared_from_this();
        // 跨通道回调只移交拥有内存的 Bytes，再投递到会话执行器；不跨回调保留 ByteView。
        channel->async_read([weak](Result<Bytes> result) {
            if (const auto self = weak.lock())
                self->executor->post([weak, result = std::move(result)]() mutable {
                    const auto self = weak.lock();
                    if (!self || self->state == State::closed) return;
                    if (!result) {
                        self->shutdown(result.error());
                        return;
                    }
                    for (auto& event : self->decoder.feed(result.value())) {
                        if (std::holds_alternative<Error>(event))
                            self->report(std::get<Error>(event));
                        else
                            self->receive(std::get<link::Frame>(event));
                        if (self->state == State::closed) break;
                    }
                    self->read();
                });
        });
    }

    void receive(const link::Frame& frame) {
        if (!(frame.server == options.server) || frame.client != options.client_address) {
            report({ErrorCode::address_mismatch, 0, "session SA/CA"});
            return;
        }
        if ((frame.control & 0x80) == (options.role == Role::server ? 0x80 : 0)) {
            report({ErrorCode::direction_mismatch, 3, "session DIR"});
            return;
        }
        if (frame.control & 0x20) {
            report({ErrorCode::unsupported_service, 3, "link fragmentation"});
            return;
        }
        // 协商后的接收尺寸按完整帧长度域计算；单帧限制不依赖对端自报尺寸。
        if (state == State::associated &&
            frame.payload.size() + 9 + frame.server.bytes.size() > agreement.receive_frame_bytes) {
            report({ErrorCode::resource_limit, 1, "negotiated receive frame limit"});
            return;
        }
        auto decoded = apdu::decode_apdu(frame.payload, codec_limits(state == State::associated));
        if (!decoded) {
            report(decoded.error());
            // 仅对客户机发起的完整用户数据请求返回异常，不对异常响应反复应答。
            if (options.role == Role::server && frame.control == 0x43 && !frame.payload.empty() &&
                frame.payload[0] != 0x6e && frame.payload.size() >= 3) {
                const auto service = frame.payload[0];
                const auto piid = frame.payload[service >= 5 && service <= 9 ? 2 : 1];
                if (!(piid & 0x40))
                    send(apdu::ErrorResponse{
                        true,
                        piid,
                        static_cast<std::uint8_t>(
                            decoded.error().code == ErrorCode::unsupported_service ? 2 : 1),
                        {}});
            }
            return;
        }
        auto message = std::move(decoded).value();
        const bool link_message = std::holds_alternative<apdu::LinkRequest>(message) ||
                                  std::holds_alternative<apdu::LinkResponse>(message);
        bool client_started =
            !link_message && !std::holds_alternative<apdu::ReleaseNotification>(message);
        if (const auto error = std::get_if<apdu::ErrorResponse>(&message))
            client_started = error->server;
        if ((frame.control & 7) != (link_message ? 1 : 3) ||
            static_cast<bool>(frame.control & 0x40) != client_started) {
            report({ErrorCode::direction_mismatch, 3, "session PRM/function"});
            return;
        }
        if (auto request = std::get_if<apdu::LinkRequest>(&message)) {
            if (options.role != Role::client) return;
            apdu::LinkResponse response;
            response.piid = request->piid_acd & 0xbf;
            response.result = options.clock_trusted ? 0x80 : 0;
            response.requested_at = request->requested_at;
            const auto received_at = calendar();
            if (!received_at) return;
            const auto responded_at = calendar();
            if (!responded_at) return;
            response.received_at = *received_at;
            response.responded_at = *responded_at;
            send(response);
            if (request->type == apdu::LinkRequestType::logout)
                leave_association({ErrorCode::not_associated, 0, "peer logout"},
                                  State::disconnected);
            else if (state == State::disconnected)
                state = State::preconnected;
            return;
        }
        if (auto request = std::get_if<apdu::ConnectRequest>(&message)) {
            if (options.role != Role::server) return;
            apdu::ConnectResponse response;
            response.piid_acd = request->piid;
            response.factory = options.factory;
            response.parameters = options.parameters;
            if (request->parameters.version != options.parameters.version)
                response.result = 5;
            else if (!std::holds_alternative<apdu::NullSecurity>(request->mechanism) ||
                     request->time_tag || !valid_parameters(request->parameters) ||
                     state == State::disconnected || state == State::associated)
                response.result = 255;
            else {
                auto& p = response.parameters;
                for (std::size_t i = 0; i < p.protocol.size(); ++i)
                    p.protocol[i] &= request->parameters.protocol[i];
                for (std::size_t i = 0; i < p.function.size(); ++i)
                    p.function[i] &= request->parameters.function[i];
                p.send_frame_bytes =
                    std::min(p.send_frame_bytes, request->parameters.receive_frame_bytes);
                p.receive_frame_bytes =
                    std::min(p.receive_frame_bytes, request->parameters.send_frame_bytes);
                p.receive_window = 1;
                p.apdu_bytes = std::min(p.apdu_bytes, request->parameters.apdu_bytes);
                p.timeout_seconds =
                    std::min(p.timeout_seconds, request->parameters.timeout_seconds);
                if (!(p.protocol[0] & 0x80)) response.result = 255;
            }
            if (!response.result) {
                const auto now = calendar();
                if (!now) return;
                established_at = seconds_calendar(*now);
            }
            // CONNECT 响应经预连接通道发送；尺寸协商从服务器发送响应后才进入会话语境。
            send(response);
            if (state != State::closed && !response.result) {
                agreement = response.parameters;
                state = State::associated;
                touch();
            }
            return;
        }
        if (auto request = std::get_if<apdu::ReleaseRequest>(&message)) {
            if (options.role != Role::server) return;
            if (request->time_tag) {
                send(apdu::ErrorResponse{true, request->piid, 2, {}});
                return;
            }
            send(apdu::ReleaseResponse{request->piid, 0, {}});
            if (state != State::closed)
                leave_association({ErrorCode::not_associated, 0, "peer release"});
            return;
        }
        if (auto request = std::get_if<apdu::SetRequest>(&message)) {
            serve_mutation<apdu::SetRequest, apdu::SetResponse>(*request, set_handler,
                                                                request->list ? 8 : 7);
            return;
        }
        if (auto request = std::get_if<apdu::ActionRequest>(&message)) {
            serve_mutation<apdu::ActionRequest, apdu::ActionResponse>(*request, action_handler,
                                                                      request->list ? 13 : 12);
            return;
        }
        if (auto request = std::get_if<apdu::GetRequest>(&message)) {
            if (options.role != Role::server) return;
            const auto bit = request->list ? 0x20 : 0x40;
            if (state != State::associated || !(agreement.protocol[0] & bit) || request->time_tag) {
                send(apdu::ErrorResponse{true, request->piid, 2, {}});
                return;
            }
            touch();
            if (state == State::closed) return;
            try {
                apdu::GetResponse response;
                if (request_handler)
                    response = request_handler(*request);
                else {
                    // 尚未绑定对象目录时，GET 协议仍可处理，每个未知属性独立返回 DAR=4。
                    for (const auto& attribute : request->attributes)
                        response.attributes.push_back({attribute, std::uint8_t{4}});
                }
                response.piid_acd = request->piid;
                response.list = request->list;
                response.time_tag.reset();
                send(response);
            } catch (...) {
                send(apdu::ErrorResponse{true, request->piid, 255, {}});
            }
            return;
        }
        if (const auto notification = std::get_if<apdu::ReleaseNotification>(&message)) {
            if (options.role != Role::client) return;
            // 通知属于已建立的连接，旧通知不能抢占新 CONNECT 的在途事务。
            if (state != State::associated && state != State::releasing) {
                report({ErrorCode::not_associated, 0, "unexpected release notification"});
                return;
            }
            if (notification->time_tag) {
                report({ErrorCode::unsupported_service, 0, "release notification TimeTag"});
                return;
            }
            leave_association({ErrorCode::not_associated, 0, "peer release notification"});
            return;
        }
        if (!pending || pending->id != id_of(message)) {
            report({ErrorCode::invalid_value, 0, "late or unmatched response"});
            return;
        }
        if (auto error = std::get_if<apdu::ErrorResponse>(&message)) {
            if (error->time_tag) {
                report({ErrorCode::unsupported_service, 0, "error response TimeTag"});
                return;
            }
            const auto kind = pending->kind;
            if (kind == Kind::connect || kind == Kind::release) state = State::preconnected;
            complete(Error{ErrorCode::remote_error, 0, "remote ERROR-Response", error->type});
            return;
        }
        if (pending->kind == Kind::get && std::holds_alternative<apdu::GetResponse>(message)) {
            const auto& request = std::get<apdu::GetRequest>(pending->request);
            const auto& response = std::get<apdu::GetResponse>(message);
            bool matches = request.list == response.list &&
                           request.attributes.size() == response.attributes.size() &&
                           !response.time_tag;
            if (matches)
                for (std::size_t i = 0; i < request.attributes.size(); ++i)
                    if (!(request.attributes[i] == response.attributes[i].attribute)) {
                        matches = false;
                        break;
                    }
            if (!matches) {
                report({ErrorCode::invalid_value, 0, "GET response OAD/list"});
                return;
            }
            touch();
            complete(std::move(message));
        } else if (pending->kind == Kind::set &&
                   std::holds_alternative<apdu::SetResponse>(message)) {
            accept_mutation<apdu::SetRequest, apdu::SetResponse>(std::move(message));
        } else if (pending->kind == Kind::action &&
                   std::holds_alternative<apdu::ActionResponse>(message)) {
            accept_mutation<apdu::ActionRequest, apdu::ActionResponse>(std::move(message));
        } else if (pending->kind == Kind::connect &&
                   std::holds_alternative<apdu::ConnectResponse>(message)) {
            const auto& response = std::get<apdu::ConnectResponse>(message);
            if (response.result && response.time_tag) {
                report({ErrorCode::unsupported_service, 0, "CONNECT rejection TimeTag"});
                return;
            }
            if (!response.result) {
                const auto& p = response.parameters;
                bool valid = valid_parameters(p) && p.version == options.parameters.version &&
                             !response.security && !response.time_tag;
                for (std::size_t i = 0; i < p.protocol.size(); ++i)
                    if (p.protocol[i] & ~options.parameters.protocol[i]) valid = false;
                for (std::size_t i = 0; i < p.function.size(); ++i)
                    if (p.function[i] & ~options.parameters.function[i]) valid = false;
                if (!(p.protocol[0] & 0x80) || p.receive_window != 1 ||
                    p.timeout_seconds > options.parameters.timeout_seconds ||
                    p.send_frame_bytes > options.parameters.receive_frame_bytes ||
                    p.receive_frame_bytes > options.parameters.send_frame_bytes ||
                    p.apdu_bytes > options.parameters.apdu_bytes)
                    valid = false;
                if (!valid) {
                    shutdown({ErrorCode::association_failed, 0, "invalid CONNECT agreement"});
                    return;
                }
                const auto now = calendar();
                if (!now) return;
                established_at = seconds_calendar(*now);
                agreement = p;
                agreement.send_frame_bytes =
                    std::min(options.parameters.send_frame_bytes, p.receive_frame_bytes);
                agreement.receive_frame_bytes =
                    std::min(options.parameters.receive_frame_bytes, p.send_frame_bytes);
                agreement.apdu_bytes = std::min(options.parameters.apdu_bytes, p.apdu_bytes);
                state = State::associated;
                touch();
            } else
                state = State::preconnected;
            complete(std::move(message));
        } else if (pending->kind == Kind::link &&
                   std::holds_alternative<apdu::LinkResponse>(message)) {
            const auto& response = std::get<apdu::LinkResponse>(message);
            const auto& request = std::get<apdu::LinkRequest>(pending->request);
            if (!(response.requested_at == request.requested_at)) {
                report({ErrorCode::invalid_value, 0, "LINK request time mismatch"});
                return;
            }
            if (!(response.result & 7)) {
                if (request.type == apdu::LinkRequestType::logout)
                    state = State::disconnected;
                else if (state == State::disconnected)
                    state = State::preconnected;
            }
            complete(std::move(message));
        } else if (pending->kind == Kind::release &&
                   std::holds_alternative<apdu::ReleaseResponse>(message)) {
            if (std::get<apdu::ReleaseResponse>(message).time_tag) {
                report({ErrorCode::unsupported_service, 0, "release response TimeTag"});
                return;
            }
            state = State::preconnected;
            complete(std::move(message));
        } else
            report({ErrorCode::unsupported_service, 0, "unexpected response service"});
    }
};

Session::Session(std::shared_ptr<transport::IChannel> channel, std::shared_ptr<IExecutor> executor,
                 SessionOptions options) {
    if (!channel || !executor || options.server.type != link::AddressType::single ||
        options.server.bytes.empty() || options.server.bytes.size() > 16 ||
        options.server.logical > 3 || !valid_parameters(options.parameters) ||
        options.request_timeout.count() <= 0 || options.id_reuse_delay < options.request_timeout ||
        options.request_timeout >
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::duration::max()) ||
        options.limits.max_data_bytes < 80 || !options.limits.max_elements ||
        options.parameters.send_frame_bytes + 2u > options.limits.max_frame_bytes ||
        options.parameters.receive_frame_bytes + 2u > options.limits.max_frame_bytes ||
        options.parameters.apdu_bytes > options.limits.max_data_bytes)
        throw std::invalid_argument("session options");
    // 仅声明已实现的普通/列表 GET、SET、ACTION；then-get 和记录等组合能力仍关闭。
    options.parameters.protocol[0] &= 0xe1;
    options.parameters.protocol[1] &= 0x8c;
    for (std::size_t i = 2; i < options.parameters.protocol.size(); ++i)
        options.parameters.protocol[i] = 0;
    if (!(options.parameters.protocol[0] & 0x80))
        throw std::invalid_argument("application association capability required");
    options.parameters.function.fill(0);
    options.parameters.receive_window = 1;
    impl_ = std::make_shared<Impl>(std::move(channel), std::move(executor), std::move(options));
}

Session::~Session() = default;

void Session::start() {
    const std::weak_ptr<Impl> weak = impl_;
    impl_->executor->post([weak] {
        const auto self = weak.lock();
        if (!self) return;
        if (self->started || self->state == State::closed) return;
        self->started = true;
        self->state = self->options.require_login ? State::disconnected : State::preconnected;
        if (self->options.preset_association) {
            self->state = State::associated;
            const auto now = self->calendar();
            if (!now) return;
            self->established_at = seconds_calendar(*now);
            self->touch();
        }
        self->read();
    });
}

void Session::set_request_handler(RequestHandler handler) {
    const std::weak_ptr<Impl> weak = impl_;
    impl_->executor->post([weak, handler = std::move(handler)]() mutable {
        if (const auto self = weak.lock()) self->request_handler = std::move(handler);
    });
}

void Session::set_diagnostic_handler(DiagnosticHandler handler) {
    const std::weak_ptr<Impl> weak = impl_;
    impl_->executor->post([weak, handler = std::move(handler)]() mutable {
        if (const auto self = weak.lock()) self->diagnostic = std::move(handler);
    });
}

void Session::set_set_handler(SetRequestHandler handler) {
    const std::weak_ptr<Impl> weak = impl_;
    impl_->executor->post([weak, handler = std::move(handler)]() mutable {
        if (const auto self = weak.lock()) self->set_handler = std::move(handler);
    });
}

void Session::set_action_handler(ActionRequestHandler handler) {
    const std::weak_ptr<Impl> weak = impl_;
    impl_->executor->post([weak, handler = std::move(handler)]() mutable {
        if (const auto self = weak.lock()) self->action_handler = std::move(handler);
    });
}

void Session::async_set(std::vector<apdu::SetAttribute> attributes, bool list, SetHandler handler) {
    impl_->post_mutation<apdu::SetRequest, apdu::SetResponse>(
        apdu::SetRequest{0, list, std::move(attributes), {}}, Impl::Kind::set, list ? 8 : 7,
        std::move(handler));
}

void Session::async_action(std::vector<apdu::ActionMethod> methods, bool list,
                           ActionHandler handler) {
    impl_->post_mutation<apdu::ActionRequest, apdu::ActionResponse>(
        apdu::ActionRequest{0, list, std::move(methods), {}}, Impl::Kind::action, list ? 13 : 12,
        std::move(handler));
}

bool Session::in_executor_thread() const noexcept { return impl_->executor->is_current(); }

void Session::async_connect(ConnectHandler handler) {
    const std::weak_ptr<Impl> weak = impl_;
    impl_->executor->post([weak, handler = std::move(handler)]() mutable {
        const auto self = weak.lock();
        if (!self || self->state == State::closed) {
            deliver(std::move(handler), Result<apdu::ConnectResponse>{Error{
                                            ErrorCode::closed, 0, "session destroyed/closed"}});
            return;
        }
        if (self->options.role != Role::client || self->state != State::preconnected) {
            deliver(std::move(handler), Result<apdu::ConnectResponse>{Error{
                                            ErrorCode::not_associated, 0, "CONNECT state/role"}});
            return;
        }
        apdu::ConnectRequest request;
        request.parameters = self->options.parameters;
        self->submit(Impl::Kind::connect, request,
                     [handler = std::move(handler)](Result<apdu::Apdu> r) mutable {
                         deliver(std::move(handler), typed<apdu::ConnectResponse>(std::move(r)));
                     });
    });
}

void Session::async_link(apdu::LinkRequestType type, std::uint16_t heartbeat_seconds,
                         LinkHandler handler) {
    const std::weak_ptr<Impl> weak = impl_;
    impl_->executor->post([weak, type, heartbeat_seconds, handler = std::move(handler)]() mutable {
        const auto self = weak.lock();
        if (!self || self->state == State::closed) {
            deliver(std::move(handler), Result<apdu::LinkResponse>{Error{
                                            ErrorCode::closed, 0, "session destroyed/closed"}});
            return;
        }
        if (self->options.role != Role::server ||
            (type == apdu::LinkRequestType::logout && self->state == State::associated)) {
            deliver(std::move(handler), Result<apdu::LinkResponse>{
                                            Error{ErrorCode::invalid_value, 0, "LINK role/state"}});
            return;
        }
        apdu::LinkRequest request;
        request.type = type;
        request.heartbeat_seconds = heartbeat_seconds;
        const auto now = self->calendar();
        if (!now) {
            deliver(std::move(handler), Result<apdu::LinkResponse>{Error{
                                            ErrorCode::invalid_value, 0, "calendar clock failed"}});
            return;
        }
        request.requested_at = *now;
        self->submit(Impl::Kind::link, request,
                     [handler = std::move(handler)](Result<apdu::Apdu> r) mutable {
                         deliver(std::move(handler), typed<apdu::LinkResponse>(std::move(r)));
                     });
    });
}

void Session::async_get(std::vector<model::Oad> attributes, bool list, GetHandler handler) {
    const std::weak_ptr<Impl> weak = impl_;
    impl_->executor->post(
        [weak, attributes = std::move(attributes), list, handler = std::move(handler)]() mutable {
            const auto self = weak.lock();
            if (!self || self->state == State::closed) {
                deliver(std::move(handler), Result<apdu::GetResponse>{Error{
                                                ErrorCode::closed, 0, "session destroyed/closed"}});
                return;
            }
            if (self->options.role != Role::client || self->state != State::associated) {
                deliver(std::move(handler), Result<apdu::GetResponse>{Error{
                                                ErrorCode::not_associated, 0, "GET state/role"}});
                return;
            }
            if (!(self->agreement.protocol[0] & (list ? 0x20 : 0x40))) {
                deliver(std::move(handler),
                        Result<apdu::GetResponse>{
                            Error{ErrorCode::unsupported_service, 0, "GET not negotiated"}});
                return;
            }
            apdu::GetRequest request{0, list, std::move(attributes), {}};
            self->submit(Impl::Kind::get, std::move(request),
                         [handler = std::move(handler)](Result<apdu::Apdu> r) mutable {
                             deliver(std::move(handler), typed<apdu::GetResponse>(std::move(r)));
                         });
        });
}

void Session::async_release(ReleaseHandler handler) {
    const std::weak_ptr<Impl> weak = impl_;
    impl_->executor->post([weak, handler = std::move(handler)]() mutable {
        const auto self = weak.lock();
        if (!self || self->state == State::closed) {
            deliver(std::move(handler),
                    Result<void>{Error{ErrorCode::closed, 0, "session destroyed/closed"}});
            return;
        }
        if (self->options.role != Role::client || self->state != State::associated) {
            deliver(std::move(handler),
                    Result<void>{Error{ErrorCode::not_associated, 0, "RELEASE state/role"}});
            return;
        }
        // 在发出释放前先改变状态，再交付取消回调，阻止回调重入提交新读取。
        self->state = State::releasing;
        self->complete(Error{ErrorCode::cancelled, 0, "transaction released"});
        self->submit(Impl::Kind::release, apdu::ReleaseRequest{},
                     [handler = std::move(handler)](Result<apdu::Apdu> r) mutable {
                         deliver(std::move(handler), r ? Result<void>{} : Result<void>{r.error()});
                     });
    });
}

void Session::cancel() {
    const std::weak_ptr<Impl> weak = impl_;
    impl_->executor->post([weak] {
        if (const auto self = weak.lock())
            if (self->pending) self->shutdown({ErrorCode::cancelled, 0, "session cancelled"});
    });
}

void Session::close() {
    const std::weak_ptr<Impl> weak = impl_;
    impl_->executor->post([weak] {
        if (const auto self = weak.lock()) self->shutdown({ErrorCode::closed, 0, "session closed"});
    });
}

State Session::state() const noexcept { return impl_->state.load(); }
}  // namespace dlt698::session
