# RtmpMonitor 0.2.0-beta.1 WebRTC 双模式 12 周研发计划

> 状态：已批准的研发计划，尚未进入生产代码实施。
>
> 当前稳定能力仍是 `0.1.0-alpha.1` RTMP 路径。本文描述目标、顺序和验收门禁，不能作为
> WebRTC 已交付、已部署或已通过 ARM 真机验证的证据。

## 1. 目标与产品边界

V2 在保留 RTMP 稳定路径的前提下新增两种 H.264 WebRTC 输入：

| 模式 | 媒体路径 | 辅助服务 | 典型用途 |
| --- | --- | --- | --- |
| 公网服务器 | 参考发布器/设备 → WHIP → SRS → WHEP → Qt | Nginx、SRS 6.0.184 | 多观看端、统一公网入口、集中运维 |
| P2P | 参考发布器/设备 ↔ Qt，失败时经 TURN | Go WSS 信令、STUN、coturn | 一对一低延迟、局域网或可直连公网 |

首个 Beta 里程碑只支持 H.264 视频。RTMP/AAC 继续作为稳定路径；Opus、双向语音、真实设备
SDK、多观看者 P2P、集群 SFU、移动端和完整账号/RBAC 均不在本轮实现。

公网服务器模式的资格目标为 1/4/8 路 720p30，16 路只记录能力。P2P 资格目标为 1/4 路，分别
验证 Direct 与 TURN Relay。默认网络功能关闭，仓库不得包含真实公网地址、Token、密码或证书。

## 2. 固定技术基线

- Qt 客户端使用 libdatachannel 0.24.5 管理 ICE、DTLS-SRTP、PeerConnection 和 Track。
- FFmpeg 继续承担 H.264 解码，不创建第二套解码、音频、指标或渲染框架。
- 公网媒体服务器固定 SRS 6.0.184，使用 WHIP/WHEP，不采用 SRS 7/8 开发线。
- P2P 信令采用 Go 1.22 单进程服务和 Gorilla WebSocket 1.5.3；正常媒体不经过信令服务。
- TURN 使用 coturn；Nginx 是唯一公网 HTTPS/WSS 入口，SRS HTTP API 仅在内部网络可达。
- 12 周内使用参考发布器；真实设备端等待 SDK、编码接口、身份和固件生命周期契约。

协议依据：

- [W3C WebRTC](https://www.w3.org/TR/webrtc/)
- [ICE RFC 8445](https://www.rfc-editor.org/rfc/rfc8445.html)
- [SRS WebRTC](https://ossrs.io/lts/en-us/docs/v6/doc/webrtc)
- [libdatachannel](https://github.com/paullouisageneau/libdatachannel)
- [coturn](https://github.com/coturn/coturn)

## 3. 架构和所有权

目标依赖方向：

```text
app 组合根
 ├─ ui → render → media → transport
 ├─ profiles → transport
 ├─ server
 └─ diagnostics（只读组合 media/render/transport 指标）

transport → Qt Core/Network + 可选 libdatachannel
```

新增 `rtmp_monitor_transport`，拥有协议无关来源描述、WebRTC 输入会话、信令客户端、RTP/H.264
重组和协议状态。它不得依赖 app、media、profiles、server、render 或 ui；对象组装和跨层信号连接
只允许发生在 app 组合根。

每路输入会话唯一拥有 PeerConnection、Track、信令状态、重连代次和协议回调。共享
DecodeWorkerPool、容量 1 最新帧邮箱、音频引擎、指标和渲染仍由现有 media/render 边界拥有。

统一停止顺序：

```text
停止新的重连调度
→ 关闭 WHEP/信令/PeerConnection
→ 递增 generation 并废弃旧回调
→ 清空有界压缩媒体队列
→ 注销邮箱/时钟源
→ 销毁 Track、PeerConnection 和网络资源
```

## 4. 公共契约与兼容策略

计划新增以下类型：

```cpp
enum class MediaTransportMode { Rtmp, Whep, PeerToPeer };

struct RtmpSource { QString url; };
struct WhepSource {
    QString serverConfigurationId;
    QString streamKey;
};
struct PeerSource {
    QString signalingConfigurationId;
    QString peerId;
};
using MediaSource = std::variant<RtmpSource, WhepSource, PeerSource>;

struct EncodedVideoAccessUnit {
    QByteArray annexBData;
    qint64 mediaTimestampUs;
    quint32 rtpTimestamp;
    bool keyFrame;
    quint64 sessionGeneration;
};
```

`MultiStreamPlaybackManager` 增加接收 `MediaSource` 的 overload。原
`addStream(displayName, rtmpUrl)`、`QStringList` 构造入口和 `--url` 保持兼容；RTMP URL 仍在原
启动时点校验，避免改变 StreamId 和单路故障隔离语义。

保存流 schema v2 采用带 mode 的 source 对象：

- v1 `streamUrl` 读取时迁移为 `RtmpSource`，只有成功保存后才写回 v2。
- WHEP 只保存 `serverConfigurationId` 与 `streamKey`。
- P2P 只保存 `signalingConfigurationId` 与 `peerId`。
- Token、TURN 密码、Authorization header、SDP 和 ICE candidate 永不持久化，也不进入 CLI。
- WebRTC 失败不得静默切换 RTMP；只有档案显式配置回滚地址且用户确认后才允许。

连接呈现状态固定为 `Connecting`、`Server`、`Direct`、`Relayed`、`Reconnecting`、`Error`。
`Direct` 只用于已选择 host/srflx candidate pair，`Relayed` 只用于 relay candidate pair。

## 5. 模块和周次

| 模块 | 主要职责 | 周次 | 退出门禁 |
| --- | --- | --- | --- |
| M0 基线与学习 | 知识、回环实验、开关、版本 | Week 1～2 | RTMP 基线不变；WebRTC 默认关闭 |
| M1 Transport | 类型、RTMP 适配、schema v2 | Week 3 | RTMP 行为兼容；依赖门禁通过 |
| M2 公网服务器 | RTP/H.264、WHEP、WHIP 参考发布器 | Week 4～5 | 单路 720p30、重连、代次隔离 |
| M3 P2P | Go WSS、PeerConnection、STUN/TURN | Week 6～7 | host/srflx/relay 与故障场景通过 |
| M4 安全部署 | Nginx、SRS、信令、coturn | Week 8 | TLS/WSS、短期凭据、内部 API |
| M5 Qt 产品集成 | 三模式 UI、事件、安全新鲜度 | Week 9～10 | schema/旧 CLI/MQTT 兼容 |
| M6 性能发布 | 多路、恢复、Windows/ARM64、打包 | Week 11～12 | 30 分钟 smoke 与 600 秒门禁 |
| M7 后续 | Opus、设备 SDK、对讲、多观看者 | 后续 | 另立计划，不建空接口 |

详细周任务见 `docs/versions/webrtc-v2/weeks/`。

## 6. 信令和安全默认值

Beta 的 P2P room 只允许一个 publisher 和一个 viewer。服务默认房间 TTL 10 分钟、单消息最大
256 KiB、每连接发送队列 32、最大活动房间 1024；超限明确拒绝，不建立无界缓冲。

短期 Token 绑定 room、peer、role 和过期时间，通过 `Authorization: Bearer` 传递，不放在 URL。
设备/观看端 Token 默认 5 分钟，TURN 临时凭据默认 10 分钟。信令服务不记录 SDP、candidate、
candidate IP、Token、完整设备端点或消息 payload。

部署模板只使用 `<webrtc-host>`、`<turn-host>` 等占位符。真实域名、DNS、证书、Secret 和公网地址
由部署人员在目标机输入，只能存在于环境变量、secret store 或仓库忽略的本机配置。

## 7. 测试与发布门禁

单元测试覆盖：

- H.264 单 NAL、STAP-A、FU-A、SPS/PPS、IDR 恢复。
- 丢包、乱序、序号和时间戳回绕、非法长度、超大 AU。
- generation 隔离、幂等停止、有界队列和旧回调废弃。
- MediaSource 校验、schema v1→v2、日志与错误文本脱敏。
- Token 过期、身份不匹配、房间隔离、TTL、容量和背压。

集成测试覆盖：

- WHIP → SRS → WHEP → Qt。
- P2P host、srflx 和强制 relay。
- SRS、信令、coturn、发布器和客户端网络分别停止/恢复。
- RTMP 视频、AAC、重连、MQTT 控制安全和渲染完整回归。

性能门槛：

- 受控局域网 P2P 单路源到呈现 P95 ≤ 200 ms。
- 同区域公网服务器模式 P95 ≤ 300 ms。
- 中间 smoke 至少 30 分钟；发布门禁预热后连续采样 600 秒。
- 运行期间不得出现无界队列、退出残留或旧 session 帧提交。
- 公网结果必须来自真实公网，交叉构建不得替代 ARM 真机结论。

## 8. 风险与停线条件

- SRS/WHEP 或 libdatachannel 对 H.264 fmtp/packetization-mode 不兼容时，先固定最小 SDP 和参考流，
  不通过 UI 特判掩盖协议问题。
- TURN 不可用必须显示 Error，不得伪装 Direct；relay 成本和延迟单独记录。
- 发现 SDP、candidate IP、Token 或真实公网端点进入日志/仓库时立即停止发布并脱敏。
- RTMP 回归、控制安全或停止生命周期任一失败时停止本周模块，不进入下一周。
- ARM 只完成交叉构建时继续标记 Engineering Preview，不声明板级 WebRTC 支持。

## 9. 每周完成定义

每周必须同时具备：知识笔记、可重复实验、单一内聚代码提交、自动测试、人工验收步骤、风险记录、
项目快照和下一周入口。没有对应运行证据的结论统一写为“待验证”，不得把配置、代码存在或命令退出
码表述为功能已通过。
