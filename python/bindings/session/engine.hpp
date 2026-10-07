/** @file expert.hpp
 * @brief 由调用线程驱动的异步会话，原生完成通知通过拥有型队列返回。
 */
#pragma once
#include <dlt698/app/connection.hpp>
#include <dlt698/service/advanced.hpp>
#include <dlt698/transport/tcp.hpp>
#include <thread>

#include "bindings.hpp"
#include "events.hpp"
#include "trans.hpp"

namespace dlt698::python {
struct Completion {
    std::uint64_t token = 0;
    std::uint64_t connection = 0;
    std::string kind;
    std::optional<Error> error;
    std::optional<protocol::apdu::Apdu> message;
    std::optional<session::State> state;
};

/** @brief 单线程驱动的高级会话，不创建后台线程。
 * @note 构造、poll、请求和关闭必须在同一 Python 线程调用。Python provider/backend
 * 仅在该线程的 poll 中执行，必须及时返回且不得同步等待本运行时。
 */
class Engine {
   public:
    /** @brief 创建独立原生运行时。
     * @param[in] options 原生会话配置，角色按连接或监听场景设置。
     * @param[in] objects 服务器目录；客户端可为空。
     * @param[in] queue_limit 待取出完成通知的数量上限，必须非零。
     * @param[in] advanced 高级服务配置，与当前 C++ 服务一致。
     * @param[in] queue_bytes 完成结果编码字节总预算，必须非零；不含对象固定开销。
     * @param[in] transparent 可选透明端口桥，由应用 drain 并异步完成。
     */
    Engine(session::SessionOptions options = {},
           std::shared_ptr<service::ObjectRegistry> objects = nullptr,
           std::size_t queue_limit = 1024, service::AdvancedServiceOptions advanced = {},
           std::size_t queue_bytes = 16 * 1024 * 1024,
           std::shared_ptr<TransBridge> transparent = nullptr);
    /** @brief 同线程释放所有 socket、计时器和完成任务，不调用后台 Python。 */
    ~Engine();
    /** @brief 异步连接 TCP，后续必须 poll。
     * @param[in] host 主机名或 IP。
     * @param[in] port 非零远端端口。
     * @param[in] profile 关联场景。
     * @param[in] channel 原始通道的读写资源预算。
     * @return 完成通知 token。
     */
    std::uint64_t connect_tcp(std::string host, std::uint16_t port, app::ConnectionProfile profile,
                              transport::ChannelOptions channel = {});
    /** @brief 启动专家服务器监听，持续接入各自独立的会话。
     * @param[in] address 数字 IP 地址。
     * @param[in] port 零或实际端口。
     * @param[in] profile 协议场景。
     * @param[in] max_connections 活动连接上限，必须非零。
     * @return 系统分配或指定的监听端口。
     */
    std::uint16_t listen(std::string address, std::uint16_t port, app::ConnectionProfile profile,
                         std::size_t max_connections = 16);
    /** @brief 从唯一驱动线程推进原生异步 I/O。
     * @param[in] budget 本次驱动预算，单位秒，毫秒精度。
     * @return 本次完成队列的拥有型快照。
     * @throws std::runtime_error 非拥有线程、重入或完成队列溢出。
     */
    std::vector<Completion> poll(double budget = 0.001);
    /** @brief 发起公共 CONNECT。
     * @param[in] connection 已接入的连接标识。
     * @return 完成 token。
     */
    std::uint64_t connect(std::uint64_t connection = 1);
    /** @brief 发起普通/列表读取。
     * @param[in] attributes 非空 OAD 列表。
     * @param[in] list 是否使用列表请求。
     * @param[in] connection 连接标识。
     * @return 完成 token。
     */
    std::uint64_t get(std::vector<model::Oad> attributes, bool list, std::uint64_t connection = 1);
    /** @brief 发起远端写入，不重试。
     * @param[in] attributes 属性值列表。
     * @param[in] list 是否列表。
     * @param[in] connection 连接标识。
     * @return 完成 token。
     */
    std::uint64_t set(std::vector<protocol::apdu::SetAttribute> attributes, bool list,
                      std::uint64_t connection = 1);
    /** @brief 发起方法调用，不重试。
     * @param[in] methods 方法列表。
     * @param[in] list 是否列表。
     * @param[in] connection 连接标识。
     * @return 完成 token。
     */
    std::uint64_t action(std::vector<protocol::apdu::ActionMethod> methods, bool list,
                         std::uint64_t connection = 1);
    /** @brief 发起记录查询，分块由原生会话管理。
     * @param[in] records 查询列表。
     * @param[in] list 是否列表。
     * @param[in] connection 连接标识。
     * @return 完成 token。
     */
    std::uint64_t get_record(std::vector<protocol::apdu::GetRecord> records, bool list,
                             std::uint64_t connection = 1);
    /** @brief 发起 MD5、ThenGet、REPORT 或 PROXY 高级交换。
     * @param[in] request 有类型的 APDU。
     * @param[in] connection 连接标识。
     * @return 完成 token。
     */
    std::uint64_t exchange(protocol::apdu::Apdu request, std::uint64_t connection = 1);
    /** @brief 幂等取消指定连接的在途事务。
     * @param[in] connection 连接标识。
     */
    void cancel(std::uint64_t connection = 1);
    /** @brief 关闭所有连接并自然排空原生 I/O；必须从 poll 之外调用。 */
    void close();
    /** @brief 查找一个接入会话供原生代理路由共享。
     * @param[in] connection 连接标识。
     * @return 保持该会话存活的共享引用。
     * @throws std::out_of_range 不存在的连接。
     */
    std::shared_ptr<session::Session> session_at(std::uint64_t connection = 1);

   private:
    /** @brief 检查唯一驱动线程及关闭状态。 */
    void check() const;
    /** @brief 安装持续接入操作，不同步调用 Python。 */
    void accept();
    /** @brief 为已连接通道安装原生服务、事件及状态观察器。
     * @param[in] channel 已连接的通道。
     * @param[in] id 新连接标识。
     */
    void attach(std::shared_ptr<transport::IChannel> channel, std::uint64_t id);
    /** @brief 将通知放入有界队列；超限标记失败，不能无声丢掉异步完成。
     * @param[in] completion 拥有型完成结果。
     */
    void push(Completion completion);

    /** @brief 创建不会访问 Python 的完成处理器。
     * @tparam T 原生响应类型。
     * @param[in] id 连接标识。
     * @param[in] token 操作标识。
     * @return 完成回调，只在拥有线程驱动的运行时执行。
     */
    template <class T>
    std::function<void(Result<T>)> handler(std::uint64_t id, std::uint64_t token) {
        return [this, id, token](Result<T> result) {
            Completion done;
            done.token = token;
            done.connection = id;
            done.kind = "complete";
            if (!result)
                done.error = result.error();
            else
                done.message = protocol::apdu::Apdu(std::move(result).value());
            push(std::move(done));
        };
    }

    std::thread::id owner_ = std::this_thread::get_id();
    std::shared_ptr<transport::IoRuntime> runtime_;
    std::shared_ptr<transport::TcpListener> listener_;
    std::shared_ptr<transport::TcpChannel> pending_channel_;
    session::SessionOptions options_;
    std::shared_ptr<service::ObjectRegistry> objects_;
    service::AdvancedServiceOptions advanced_;
    std::map<std::uint64_t, std::shared_ptr<session::Session>> sessions_;
    std::vector<Completion> completions_;
    std::size_t queue_limit_, max_connections_ = 16;
    std::size_t queue_bytes_limit_, queued_bytes_ = 0;
    std::uint64_t next_token_ = 1, next_connection_ = 1;
    bool closed_ = false, polling_ = false, overflow_ = false;
};
}  // namespace dlt698::python
