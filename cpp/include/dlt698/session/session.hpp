/** @file session.hpp
 * @brief 单个协议对端的连接、事务、持续接收和单调超时管理。
 */
#pragma once
#include <dlt698/common/executor.hpp>
#include <dlt698/protocol/apdu/apdu.hpp>
#include <dlt698/protocol/link/fragment.hpp>
#include <dlt698/protocol/link/frame.hpp>
#include <dlt698/session_export.hpp>
#include <dlt698/transport/channel.hpp>

namespace dlt698::session {
enum class Role { client, server };
enum class State { disconnected, preconnected, associating, associated, releasing, closed };

struct SessionOptions {
    Role role = Role::client;
    protocol::link::ServerAddress server;
    std::uint8_t client_address = 0;
    Limits limits;
    protocol::apdu::AssociationParameters parameters{
        0x0010, {0xf3, 0x8c, 0x08}, {}, 1024, 1024, 1, 1024, 100};
    // function 默认全零；应用须显式配置自身实际支持的 C.2 业务位，CONNECT 取双方交集。
    protocol::apdu::FactoryVersion factory;
    std::chrono::milliseconds request_timeout{5000};
    std::chrono::milliseconds id_reuse_delay{
        120000};                      ///< 成功事务的序号隔离期，须覆盖对端最大响应寿命。
    bool require_login = false;       ///< 远程场景可要求先由协议服务器发起 LINK 登录。
    bool preset_association = false;  ///< 显式启用本地通道的预设连接，使用本地能力和限制。
    bool clock_trusted = false;
    std::uint16_t heartbeat_seconds =
        0;  ///< 服务器登录成功后自动心跳周期，零为关闭；失败关闭通道。
    std::optional<model::Ti> request_time_tag;  ///< 客户机请求自动添加时间标签；响应须原样回传。
    std::chrono::milliseconds fragment_timeout{1000};    ///< 单片确认的单调超时。
    std::chrono::milliseconds reassembly_timeout{5000};  ///< 重组没有进展时的单调超时。
    unsigned fragment_retries = 2;  ///< 0 至 16 次，仅重发未获确认的相同片段，不重放应用请求。
    bool prefer_get_blocks = true;  ///< 超长 GET 优先按完整属性/记录行应用分块，其块仍可链路分帧。
    std::function<model::DateTime()>
        calendar_clock;  ///< 空时使用 UTC；注入时钟在执行器中调用，抛异常会关闭会话并返回 invalid_value。
};

/**
 * @brief 一个通道上的会话，支持公共连接、读写/方法、记录及两类分段事务。
 * @note 所有状态和回调在提供的串行执行器上处理；TCP 拨号方向与协议角色独立。
 * 超时或取消关闭物理通道，以隔离线上没有 generation 字段的迟到响应。
 * 析构时排队任务不保活会话；已提交请求仍须驱动执行器才能收到 closed 回调。
 */
class Session {
   public:
    using ConnectHandler = std::function<void(Result<protocol::apdu::ConnectResponse>)>;
    using GetHandler = std::function<void(Result<protocol::apdu::GetResponse>)>;
    using SetHandler = std::function<void(Result<protocol::apdu::SetResponse>)>;
    using ActionHandler = std::function<void(Result<protocol::apdu::ActionResponse>)>;
    using SetRequestHandler =
        std::function<protocol::apdu::SetResponse(const protocol::apdu::SetRequest&)>;
    using ActionRequestHandler =
        std::function<protocol::apdu::ActionResponse(const protocol::apdu::ActionRequest&)>;
    using LinkHandler = std::function<void(Result<protocol::apdu::LinkResponse>)>;
    using ReleaseHandler = std::function<void(Result<void>)>;
    using RequestHandler =
        std::function<protocol::apdu::GetResponse(const protocol::apdu::GetRequest&)>;
    using RecordHandler = std::function<void(Result<protocol::apdu::GetRecordResponse>)>;
    using RecordRequestHandler =
        std::function<protocol::apdu::GetRecordResponse(const protocol::apdu::GetRecordRequest&)>;
    using DiagnosticHandler = std::function<void(const Error&)>;
    /** @brief 创建尚未启动的会话。
     * @param[in] channel 已连接的字节通道。
     * @param[in] executor 所有会话操作及定时器共用的串行执行器。
     * @param[in] options 协议角色、精确地址、能力与超时配置。
     * @throws std::invalid_argument 通道/执行器为空、地址非单地址或限制配置非法。
     */
    DLT698_SESSION_API Session(std::shared_ptr<transport::IChannel> channel,
                               std::shared_ptr<IExecutor> executor, SessionOptions options = {});
    /** @brief 关闭通道，未完成事务以 closed 结束；仍需驱动执行器交付回调。 */
    DLT698_SESSION_API ~Session();
    /** @brief 禁止复制会话身份及事务。 */
    Session(const Session&) = delete;
    /** @brief 禁止复制赋值会话。 */
    Session& operator=(const Session&) = delete;
    /** @brief 启动持续接收泵；预设连接只在显式配置后启用，重复启动无额外效果。 */
    DLT698_SESSION_API void start();
    /** @brief 注册服务器 GET 处理器。
     * @param[in] handler 在会话执行器内同步调用的处理器，不得阻塞等待同一执行器。
     */
    DLT698_SESSION_API void set_request_handler(RequestHandler handler);
    /** @brief 注册记录查询处理器，返回拥有全部行数据的快照。
     * @param[in] handler 在串行执行器内调用，不得阻塞；缺省返回 DAR=4。
     * @note Session 缓存本次结果直到分块完成/超时，后续分页不再次调用 provider。
     */
    DLT698_SESSION_API void set_record_handler(RecordRequestHandler handler);
    /** @brief 读取记录或记录列表，并自动收齐 GET Next 数据块。
     * @param[in] records 非空查询；每项保存完整 RSD/RCSD。
     * @param[in] list false 恰好一项，true 使用 RecordList。
     * @param[in] handler 返回有精确列类型的完整快照或错误。
     */
    DLT698_SESSION_API void async_get_record(std::vector<protocol::apdu::GetRecord> records,
                                             bool list, RecordHandler handler);
    /** @brief 注册服务器 SET 处理器。
     * @param[in] handler 在执行器内同步执行，不得阻塞；未注册时逐项返回拒绝 DAR。
     */
    DLT698_SESSION_API void set_set_handler(SetRequestHandler handler);
    /** @brief 注册服务器 ACTION 处理器。
     * @param[in] handler 在执行器内同步执行，不得阻塞；未注册时逐项返回拒绝 DAR。
     */
    DLT698_SESSION_API void set_action_handler(ActionRequestHandler handler);
    /** @brief 注册不会抢占当前事务的诊断回调。
     * @param[in] handler 接收损坏帧、不匹配地址/方向、迟到响应等诊断。
     */
    DLT698_SESSION_API void set_diagnostic_handler(DiagnosticHandler handler);
    /** @brief 由协议客户机发起公共 CONNECT。
     * @param[in] handler 返回协商响应或本地错误，远端拒绝原码保存在响应 result 中。
     */
    DLT698_SESSION_API void async_connect(ConnectHandler handler);
    /** @brief 由协议服务器发起登录、单次心跳或退出登录。
     * @param[in] type 预连接请求类型。
     * @param[in] heartbeat_seconds 线上心跳周期（秒），自动周期另由 options 配置。
     * @param[in] handler 返回响应或错误。
     */
    DLT698_SESSION_API void async_link(protocol::apdu::LinkRequestType type,
                                       std::uint16_t heartbeat_seconds, LinkHandler handler);
    /** @brief 由已关联客户机读取属性，最多一个在途事务。
     * @param[in] attributes 非空的精确 OAD 列表。
     * @param[in] list false 时必须恰好一个属性，true 使用 NormalList。
     * @param[in] handler 返回按原顺序保留 Data/DAR 的响应或错误。
     * @note 自动收齐分块；TimeTag 由 options 配置，超时/取消关闭通道隔离迟到结果。
     */
    DLT698_SESSION_API void async_get(std::vector<model::Oad> attributes, bool list,
                                      GetHandler handler);
    /** @brief 发起 SET 普通或列表事务，不自动重试或回滚部分成功。
     * @param[in] attributes 属性与精确 Data 值，非空。
     * @param[in] list false 时恰好一项，true 使用列表。
     * @param[in] handler 返回逐项 DAR 或本地错误，超时不能证明远端未执行。
     */
    DLT698_SESSION_API void async_set(std::vector<protocol::apdu::SetAttribute> attributes,
                                      bool list, SetHandler handler);
    /** @brief 发起 ACTION 普通或列表事务，不自动重试有副作用的方法。
     * @param[in] methods 精确 OMD 与参数 Data，非空。
     * @param[in] list false 时恰好一项，true 使用列表。
     * @param[in] handler 返回逐项 DAR/可选 Data 或本地错误，超时后远端执行结果未知。
     */
    DLT698_SESSION_API void async_action(std::vector<protocol::apdu::ActionMethod> methods,
                                         bool list, ActionHandler handler);
    /** @brief 检查当前线程是否正在驱动本会话的执行环境。
     * @return 在执行器/所属 I/O 运行时的回调中为 true，此时不得同步等待同一环境。
     */
    DLT698_SESSION_API bool in_executor_thread() const noexcept;
    /** @brief 结束在途事务的本地等待并发起应用连接释放，物理通道仍可继续使用。
     * @param[in] handler 释放完成或错误回调。
     * @note SET/ACTION 已发出或已进入通道队列时，释放不保证远端未执行。
     */
    DLT698_SESSION_API void async_release(ReleaseHandler handler);
    /** @brief 取消在途事务并关闭物理通道，避免旧响应被后续事务匹配。 */
    DLT698_SESSION_API void cancel();
    /** @brief 幂等关闭会话、通道和定时器，结束未完成事务。 */
    DLT698_SESSION_API void close();
    /** @brief 查询原子发布的会话状态。
     * @return 最近一次已执行的状态，投递但未执行的操作不立即改变状态。
     */
    DLT698_SESSION_API State state() const noexcept;

   private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
}  // namespace dlt698::session
