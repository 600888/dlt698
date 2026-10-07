# REPORT、ThenGet、MD5 与 PROXY 接入

更新日期：2026-10-07。依据仓库内 DL/T 698.45—2017 第 6、7 章及附录 D 实现。以下入口位于分层 `Session` / `service` API；托管 `app::Client/Server` 暂未增加相应便捷方法。

## 服务器后端

```cpp
#include <dlt698/service/advanced.hpp>

// server 为已配置的服务器 Session，executor 是该 Session 的串行执行器。
// objects 的 provider 必须显式允许远端写入和方法执行。
dlt698::service::ServerService ordinary(server, objects);
dlt698::service::AdvancedServiceOptions advanced_options;
advanced_options.default_read_delay_seconds = 1;
dlt698::service::AdvancedService advanced(server, objects, executor, advanced_options);
```

普通服务和高级服务的处理器分别安装，构造辅助对象后可释放辅助对象；会话处理器持有目录和选项。每个会话最多执行一个高级请求。`set_advanced_handler` 可替换默认实现，收到拥有型请求并返回取消函数；完成回调可以来自任意线程，结果在会话执行器串行处理，重复或关闭后的结果被丢弃。

`GetMd5Request` 对读到的完整 A-XDR `Data` 编码计算 16 字节 MD5，包括类型标签和长度字段；读取失败保留原始 DAR。MD5 只用于标准规定的属性一致性查询，不提供安全认证。

ThenGet 每项依次执行写/方法、单调定时延时、读，然后进入下一项；延时零采用服务器默认值。写/方法失败仍独立执行读，分别返回结果。方法无数据与返回 `NULL` 保持区分。没有回滚、隐式重试或副作用撤销；取消后停止后续项。

```cpp
namespace apdu = dlt698::protocol::apdu;
client->async_exchange(apdu::GetMd5Request{0, {0x4001, 2, 0}, {}},
    [](dlt698::Result<apdu::Apdu> result) {
        if (!result) return; // 实际应用记录错误或完成业务等待。
        const auto& response = std::get<apdu::GetMd5Response>(result.value());
        // response.result 为 DAR 或 std::array<uint8_t, 16>。
    });
```

PIID 与请求 TimeTag 由会话分配。所有回调不得同步等待自身执行器。客户机和服务器 `SessionOptions::request_timeout` 都要覆盖 ThenGet 各项总延时、PROXY 总预算及通信余量；服务器高级后端长期不完成也会超时关闭。超时/取消后远端已发生的副作用结果可能未知。

## 主动上报、FollowReport 和 ACD

三类 `ReportNotification::payload` 分别为普通属性结果列表、记录结果列表、`TransData`（端口 OAD 和原始字节列表）。服务器通过 `async_exchange(notification, handler)` 等待确认，客户机通过 `set_report_handler` 接收。

```cpp
client->set_report_handler([](const apdu::ReportNotification& notification) {
    // 在回调期间接收/持久化拥有型内容；跨回调保存时自行复制。
    return true; // 成功接收后才确认；false 或异常不确认。
});
client->set_follow_handler([](const apdu::FollowReport& follow) {
    // follow 为普通属性列表或记录列表，借用仅在回调期间有效。
});
client->set_acd_handler([] {
    // 按应用设备配置投递事件查询，不在这里同步阻塞。
});
server->set_access_demand(true);
```

REPORT 确认独立于客户机普通事务，不抢占 GET。默认未确认重发两次，最终超时关闭。已确认且内容相同的重发在 `id_reuse_delay` 内只重发确认，不再次交付；缓存按 64 个 PIID 有界保存，释放关联时清除。双方隔离时间须覆盖实际重发和迟到响应寿命。未安装接收器不会虚假确认。

普通/记录 FollowReport 已支持所有服务器消息尾部。客户端在合法通知或已匹配响应中观察跟随上报；GET 分块最终快照保留末块尾部，其他块尾部在完整事务校验成功后交付观察器。FollowReport、ACD 分别受 CONNECT 能力位 18、19 控制。

ACD 只表示设备有请求访问的业务，不指定事件 OAD。应用按设备事件配置读取并在业务处理完毕后清除服务器待访问状态。

## 七类代理与目标 provider

`ProxyRequestPayload` / `ProxyResponsePayload` 按标准顺序包含 GetList、GetRecord、SetList、SetThenGetList、ActionList、ActionThenGetList、TransCommand。列表目标包含单地址 TSA，响应精确核对地址、完整 OAD/OMD、顺序和记录列。编码拒绝组/通配 TSA、空目标列表、零总超时和数量超限。

```cpp
auto router = std::make_shared<dlt698::service::ProxyRouter>();
auto bound = router->bind(dlt698::model::Tsa{{0x00, 0x01}}, target_client);
// 检查 bound；target_client 必须由应用完成物理连接和协议关联。
dlt698::service::AdvancedServiceOptions options;
options.proxy = router;
dlt698::service::AdvancedService backend(server, objects, executor, options);
```

`ProxyRouter` 不自动拨号。它按精确 TSA 路由到已关联客户机 Session，由目标会话独立分配 PIID、检查安全策略和完成分块。同一目标的其他在途事务返回 busy；路由取消会关闭目标会话以隔离迟到响应，应用负责重建。

`IProxyProvider::async_request` 可接入独立通道池、采集器或厂商后端。调用必须非阻塞，回调返回对应普通服务/ThenGet 响应或错误，返回非阻塞幂等取消函数。高级服务串行执行各目标，目标与总超时都使用单调定时器。部分结果保留，未完成结果为 DAR 2，未知路由为 DAR 4，异常/错误服务匹配为 DAR 255。SET/ACTION 不重试。

TransCommand 通过 `AdvancedServiceOptions::trans` 接入原始端口后端，传递完整 OAD、COMDCB、响应超时、字节超时和命令字节。驱动负责应用端口配置、实际写入/收齐和字节间超时；库控制总体响应预算、取消及返回端口匹配。没有安装端口后端返回 DAR 4，不能把字节回显当作实际转发。

新增 F100（ESAM 属性 1–15）、F101（安全模式属性 2–4）、F200（RS-232）和 F201（RS-485）元数据及结构校验。可使用 `register_standard_object` 绑定 `IObjectProvider` 或 `MemoryObject`；端口配置为可变长度结构数组，包含端口说明、COMDCB 与功能枚举。本批这些属性只读，不隐式提供硬件值、修改安全策略或密钥。目录共 131 个 OI；其他型号专用 OI/方法需根据目标设备资料扩展。

安全接入与未完成的硬件验收见 [ESAM 接入](esam-integration.md)。
