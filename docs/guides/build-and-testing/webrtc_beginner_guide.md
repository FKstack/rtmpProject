# WebRTC 新手指南：从“能连上”到理解 RtmpMonitor 双模式

> 本文是 V2 学习材料，不表示 Qt WebRTC 功能已经实现。示例只使用回环地址或符号占位符，
> 不要把真实 Token、SDP、candidate、公网地址或设备端点复制到仓库和问题单。

## 1. 先建立正确的心智模型

WebRTC 不是单一“视频传输协议”，而是一组协作机制：

```text
业务身份与信令：谁要和谁通话，交换 Offer / Answer / ICE candidate
连接建立：ICE 使用 host / srflx / relay 候选路径选出 candidate pair
传输安全：DTLS 协商密钥，SRTP 加密 RTP/RTCP 媒体
媒体格式：H.264 被拆成 RTP packet，接收端重组后交给 FFmpeg 解码
```

信令格式和服务器并不由 WebRTC 标准强制指定，所以项目需要独立 Go WSS 服务。信令服务只转发
建立连接所需的描述和候选信息；P2P 正常媒体不会经过它。TURN 是例外：当直连失败时，媒体通过
TURN 中继，但这仍不同于信令转发。

## 2. Offer、Answer 与 SDP

发起端创建 Offer，回答端根据 Offer 创建 Answer。两者通常以 SDP 文本表达：

- 支持的媒体类型和方向，例如 video/recvonly。
- 编解码器和 payload type，例如 H.264/90000。
- H.264 fmtp，例如 packetization-mode 和 profile-level-id。
- ICE username/password、DTLS fingerprint 和媒体 mid。

SDP 不是视频数据，但可能包含网络地址、临时凭据、设备能力和证书指纹，因此项目禁止保存完整
SDP 或把它写入日志。排障只记录状态、长度、经过脱敏的 codec 摘要和错误类别。

```text
发布端                         观看端
  | ------ Offer SDP ----------> |
  | <----- Answer SDP ---------- |
  | ---- ICE candidate --------> |
  | <--- ICE candidate --------- |
```

## 3. ICE 与 candidate pair

ICE 的目标不是“生成一个公网地址”，而是收集多条可能路径、做连通性检查，并选出一对本地/远端
candidate。常见 candidate：

| 类型 | 来源 | 典型含义 |
| --- | --- | --- |
| host | 本机网卡 | 同局域网最直接，通常延迟最低 |
| srflx | STUN 返回的公网映射 | NAT 允许时可以公网直连 |
| relay | TURN 分配的中继地址 | 直连失败时保底，但增加流量成本和延迟 |

ICE 常见状态是 New → Checking → Connected/Completed；失败进入 Failed，网络切换可能进入
Disconnected。产品 UI 必须根据最终 selected candidate pair 显示 Direct 或 Relayed，不能只凭
“配置了 TURN”推断实际路径。

## 4. STUN 与 TURN

STUN 回答“外部看到我的地址是什么”，通常不承载媒体。TURN 在服务器上为客户端分配 relay 地址，
并转发媒体。对称 NAT、企业防火墙、运营商网络或 UDP 受限环境常需要 TURN。

```text
直连：Publisher ================= Viewer
中继：Publisher ===== TURN ====== Viewer
信令：Publisher ---- WSS -------- Viewer
```

TURN 需要公网带宽和容量规划。Beta 必须分别测试 Direct 和强制 Relay，并记录连接类型、延迟、丢包、
TURN 流量和失败状态。

## 5. DTLS-SRTP：媒体为什么默认加密

ICE 选出路径后，双方通过 DTLS 验证 SDP 中的 fingerprint 并协商密钥，随后用 SRTP/SRTCP 加密
RTP/RTCP。应用不应该自行关闭 fingerprint 验证，也不应该在业务层发明另一套媒体加密。

短期 Token 解决“谁有权进入 room/请求 WHEP”，DTLS-SRTP 解决“媒体在传输中如何加密”。两者
职责不同，不能互相替代。

## 6. RTP 基础

H.264 WebRTC 视频通常使用 90 kHz RTP 时钟。RTP header 中最重要的字段：

- sequence number：每个 packet 递增，用于发现丢包和乱序；16 位会回绕。
- timestamp：同一访问单元的 packet 通常共享时间戳；32 位会回绕。
- marker：视频中常用于标记一个访问单元的最后一个 packet。
- payload type：映射到 SDP 协商的 H.264 codec。
- SSRC：标识一路 RTP 同步源。

RTP 时间戳不是电脑墙上时间。接收端要把 90 kHz 时钟扩展为不回退的时间线，再换算为媒体时间；
不能直接拿 32 位值做长时间差值。

## 7. H.264 如何装入 RTP

RFC 6184 中本项目首批需要支持三种常见形式：

| 形式 | 含义 | 接收端工作 |
| --- | --- | --- |
| Single NAL | 一个 packet 放一个完整 NAL | 加 Annex-B start code 后写入 AU |
| STAP-A | 一个 packet 聚合多个小 NAL | 按 16 位长度逐个拆出 |
| FU-A | 一个大 NAL 被拆成多个 packet | 重建 NAL header，并按序拼接 start/middle/end |

SPS 描述编码参数，PPS 描述图像参数，IDR 是可独立恢复的关键帧。丢包或乱序破坏 FU-A 时，不得把
残片交给 FFmpeg；应丢弃当前访问单元、请求或等待新的关键帧。缓存的 SPS/PPS 可以在恢复 IDR 前
补齐，但必须有大小上限和 session generation 隔离。

## 8. WHIP、WHEP、SFU 与 SRS

- WHIP：发布端通过 HTTP 提交 WebRTC Offer，把媒体发布给服务器。
- WHEP：观看端通过 HTTP 提交 Offer，从服务器订阅媒体。
- SFU：接收发布端媒体并选择性转发给观看端，通常不做完整转码。
- SRS：本项目公网服务器模式中的成熟媒体服务器，固定 6.0.184。

```text
参考发布器 -- WHIP --> SRS 6.0.184 -- WHEP --> RtmpMonitor
                         |
                    内部 HTTP API

公网仅暴露 Nginx HTTPS，SRS API 不直接暴露。
```

WHEP 会话通常还需要保存响应 Location，以便停止时发送 DELETE。Location、Authorization 和 SDP
都属于会话资源，不应进入保存流档案。

## 9. 公网服务器模式与 P2P

| 维度 | 公网服务器模式 | P2P |
| --- | --- | --- |
| 媒体路径 | 经过 SRS | 优先端到端，必要时经过 TURN |
| 多观看端 | 更自然 | Beta 只允许一个观看端 |
| 基础设施 | Nginx + SRS | WSS + STUN/TURN |
| 延迟目标 | 同区域 P95 ≤ 300 ms | 受控局域网 P95 ≤ 200 ms |
| 运维重点 | 服务容量和入口安全 | NAT 成功率和 TURN 成本 |

两种模式不是自动降级关系。WebRTC 失败时如果应用悄悄切 RTMP，操作者会误判真实链路，因此必须
明确报错；只有档案配置回滚地址且用户确认时才切换。

## 10. WSS、TLS、Token 和日志安全

公网入口必须使用 HTTPS/WSS。Token 默认 5 分钟，绑定 room、peer、role 和过期时间；放在
Authorization header，不放 URL。TURN 使用 10 分钟临时凭据，客户端不内置静态共享密码。

禁止日志内容：

- 完整 Offer/Answer SDP。
- ICE candidate 和 candidate IP。
- Authorization、Token、TURN 用户名/密码。
- 完整 WHEP/WHIP URL、设备端点和用户公网地址。

允许记录：连接模式、状态迁移、错误类别、codec 摘要、消息长度、候选类型、是否 Direct/Relayed、
重试次数和脱敏配置引用。

## 11. PeerConnection 生命周期与 generation

libdatachannel 的回调可能来自其内部线程。回调不能直接操作 QWidget，也不能在会话销毁后继续提交
帧。每次 start/reconnect 分配递增 generation；回调携带创建时 generation，只有与当前值一致才可
进入队列。

```text
Qt/UI thread        transport session         libdatachannel callback
     | start(g=8) -------->|                           |
     |                     |<---- RTP(g=8) ------------|
     |                     | validate generation       |
     | stop → g=9 -------->| close/reset callbacks     |
     |                     |<---- late RTP(g=8) --------|
     |                     | discard                    |
```

所有跨线程队列必须有容量。视频以实时性优先，旧数据被新数据覆盖或丢弃，不能为了“完整”形成无界
延迟。停止必须幂等，析构前必须完成回调废弃和资源回收。

## 12. 回环学习实验设计

正式实现前的实验全部使用 `127.0.0.1`：

1. 启动固定 SRS 6.0.184 回环配置。
2. 在官方示例页完成浏览器 WHIP 发布与 WHEP 播放。
3. 观察但不保存 Offer/Answer、ICE gathering 和 selected candidate pair。
4. 验证停止发布后观看端断开，重新发布后新 session 不接收旧媒体。
5. 写实验摘要时只记录 codec、状态、candidate 类型和耗时，不粘贴原始 SDP/candidate。

该实验只证明浏览器与 SRS 回环链路，不证明 Qt 客户端、P2P、公网、TURN 或 ARM 真机已实现。

## 13. 初学者排障顺序

遇到“WebRTC 黑屏”时按层检查：

1. 身份层：Token 是否有效、role/room/peer 是否匹配。
2. 信令层：Offer/Answer 是否完成，WSS/HTTP 状态是否成功。
3. ICE 层：是否选出 candidate pair，Direct 还是 Relay。
4. DTLS 层：fingerprint 验证和连接状态是否成功。
5. RTP 层：sequence、timestamp、SSRC 和 payload type 是否正确。
6. H.264 层：是否收到 SPS/PPS/IDR，FU-A 是否丢片。
7. 解码层：FFmpeg 是否接受 Annex-B AU。
8. 呈现层：邮箱、generation、渲染和真实帧新鲜度是否更新。

不要一开始就修改 UI 或增加重试次数；先定位失败所属层，再修改该层唯一职责。

## 14. 学习完成标准

进入 Week 3 编码前，维护者应能用自己的话解释：

- 为什么 P2P 仍需要信令和可能需要 TURN。
- SDP、ICE candidate 和 Token 为什么不能写日志。
- WHIP/WHEP 与 RTMP URL 的不同生命周期。
- Direct、Relayed、Server 三种状态如何由事实决定。
- H.264 FU-A 丢包后为什么需要等待 IDR。
- session generation 如何阻止停止后的旧帧污染。
