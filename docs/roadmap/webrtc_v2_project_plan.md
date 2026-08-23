# RtmpMonitor 0.2.0-beta.1 WebRTC 双客户端优先研发总计划

> 文档状态：经确认的 R3 架构路线；Week 2 开发者技术基础与 Week 3 H.264/解码边界已实施，
> 产品 WebRTC Track 与双客户端媒体路径仍未开始。
>
> 当前稳定能力仍是 `0.1.0-alpha.1` RTMP 路径。本文中的 Week 4～10 类型、模块、CLI、
> 性能指标和交付物均为待实现目标，不能作为 WebRTC 已交付、已部署或已通过 ARM 真机验证的证据。

## 1. 决策摘要

V2 第一阶段优先交付一个可在两台 Windows 电脑运行的相同测试客户端：

```text
rtmp_monitor_webrtc_client --media-role=publisher --signaling-role=offer|answer
                 H.264 / WebRTC P2P
rtmp_monitor_webrtc_client --media-role=viewer    --signaling-role=answer|offer
```

- `publisher` 首先读取经许可的固定 MP4，后续复用同一 source port 接入电脑摄像头。
- `viewer` 将收到的 H.264 接入现有 FFmpeg 解码、容量 1 最新帧邮箱和 render/UI。
- Offerer/Answerer 是信令角色，publisher/viewer 是媒体角色；二者必须正交组合。
- 本阶段只实现 `SendOnly` 和 `ReceiveOnly`，不预建双方同时收发视频。
- 同一程序可切换角色是开发与社团测试能力，不把产品定位改成视频通话。

安防机器人产品拓扑仍是“机器人/边缘端发布，操作员观看”。P2P 是可选的一对一低延迟实时观看
路径；RTMP/SRS 继续承担稳定接入、多观看端和现有证据链。WebRTC 会话不构成设备身份、控制授权
或命令回执，DataChannel 不替代 MQTT 控制与安全门禁。

## 2. 已验证基线与纠正项

### 2.1 可复用事实

- V1 已有稳定 StreamId、共享 DecodeWorkerPool、容量 1 最新帧邮箱、多路 render、指标和幂等停止。
- Week 2 已交付默认 OFF 的隔离 signaling/probe/test 边界、一次性 schema v1 和 host-only 回环。
- `libdatachannel 0.24.5` 的真实 CMake target 为 `LibDataChannel::LibDataChannel`。
- 当前 `FFmpegPlayer` 同时拥有 RTMP 网络输入和私有解码会话；要复用解码必须先在 media 内聚地提取。
- 当前控制安全以同一 StreamId 最近真实呈现帧不超过 **1,000 ms** 为新鲜；不得改成 100 ms。
- 当前本机 `test.mp4` 约 18 MB 且未被 Git 跟踪，不能作为社团可复现交付的隐含前置。

### 2.2 本次纠正

| 原计划问题 | 决策 |
| --- | --- |
| 独立参考发布器固定为 Offerer，正式客户端固定 recv-only Answerer | 改为同一测试客户端，信令角色与媒体方向正交 |
| `ui → render → media → transport` | media 与 transport 作为兄弟模块，只通过低层 H.264 契约交接 |
| `profiles → transport` | profiles 只保存产品 DTO；app 组合根负责映射，不依赖协议资源 |
| Week 3 提前创建 `MediaSource`、`PeerSource.peerId` | 删除；人工信令阶段不虚构长期 peer 身份或统一 source variant |
| Week 8 提前迁移保存流 schema v2 | 后置到具备身份、信令和产品档案语义的独立阶段 |
| 先关闭 PeerConnection 再递增 generation | 先 closing/generation/回调失效，再关闭 Track/PeerConnection |
| 100 ms 安全新鲜度 | 以实际代码和已批准产品设计的 1,000 ms 为准 |

## 3. 范围与成功标准

### 3.1 十周内包含

- 同一测试客户端的 publisher/viewer 两种媒体角色和 offer/answer 两种信令角色。
- H.264 单向视频；固定 MP4 先行，摄像头后置。
- 手工 non-trickle Offer/Answer 文件交换；Week 2 schema v1 保持不变。
- 单机双进程、两机 LAN、获授权的真实跨公网 Direct 或诚实的 Needs Relay 结论。
- 一次性运行时接入正式客户端，不迁移保存流 schema，不保存 SDP/candidate/session。
- 默认关闭的 WebRTC 构建、RTMP/AAC/MQTT/安全/渲染完整回归。
- Windows Beta 资格；Linux ARM64 仅交叉构建 Engineering Preview。

### 3.2 十周内不包含

- 同一客户端同时发送和接收视频、双向视频通话、Opus 对讲或 DataChannel 控车。
- 自建 WSS 信令、TURN/coturn、SRS WHIP/WHEP、SFU 或多观看者 P2P。
- 长期 peer ID、P2P 保存档案、schema v2、真实设备 SDK、设备身份认证或控制授权。
- Android/iOS、生产 RBAC、静默回滚 RTMP 或默认公网端点。

### 3.3 成功标准

1. 同一二进制能完成 publisher/Offerer ↔ viewer/Answerer。
2. 同一二进制能完成 viewer/Offerer ↔ publisher/Answerer。
3. 两种组合均能证明 selected pair、DTLS、H.264 AU、解码和真实呈现。
4. 晚加入 viewer 能在当前 generation 的 SPS/PPS+IDR 后起播。
5. Stop、单端退出、失败和重建后无旧帧、残留线程或无界队列。
6. RTMP 默认路径、AAC、MQTT 控制和 1,000 ms 安全语义不变。

## 4. 目标架构

### 4.1 编译依赖

```text
app / test-client composition root
├─ webrtc transport ───────→ signaling + libdatachannel + H.264 contracts
├─ publisher source ───────→ FFmpeg/camera + H.264 contracts
├─ media playback ─────────→ FFmpeg decoder + H.264 contracts
├─ ui → render → media playback
└─ diagnostics ────────────→ transport/media/render 只读快照
```

固定规则：

- transport 与 media 互不依赖，均不得包含 app、profiles、render、server 或 ui。
- publisher source 不创建 PeerConnection，不操作 viewer decoder 或 QWidget。
- profiles 不包含 transport 类型；运行时协议对象只在组合根创建。
- OFF 构建不发现、不链接、不部署 libdatachannel，也不创建测试客户端入口。
- Week 2 `webrtc_dev` 保持开发者边界；正式模块只能在 W2 人工门禁和 W3 R3 门禁后提取或复用。

### 4.2 最小契约

计划冻结以下概念，而不是提前定义通用媒体平台：

```cpp
enum class SignalingRole { Offerer, Answerer };
enum class VideoDirection { SendOnly, ReceiveOnly };

struct H264AccessUnit {
    std::vector<std::uint8_t> annexB;
    std::int64_t mediaTimestampUs;
    bool keyFrame;
};

struct SessionMediaSample {
    std::uint64_t generation;
    H264AccessUnit accessUnit;
};

struct WebRtcSessionConfig {
    SignalingRole signalingRole;
    VideoDirection videoDirection;
    IceRuntimeConfig ice;
};
```

- `H264AccessUnit` 是协议无关值，不包含 RTP timestamp、SDP、candidate、peer ID 或设备身份。
- RTP timestamp、sequence、candidate pair 和 DTLS 细节留在 transport 内部及脱敏快照。
- transport 的接收 sink 提交 `SessionMediaSample`；publisher source 向 transport 提交纯
  `H264AccessUnit`，由会话所有者绑定当前 generation。
- source/sink port 都必须返回明确的 accepted/dropped/closed/invalid-generation 结果，容量和
  超限策略由拥有队列的实现固定，不能用 `void` 隐藏丢弃。
- `IceRuntimeConfig` 默认空，仅由显式运行时输入创建；不保存真实 STUN URL 或临时凭据。

不在 Week 3 创建 `MediaTransportMode`、`MediaSource`、`RtmpSource`、`PeerSource` 或 schema v2。
正式客户端后续只新增一次性 `WebRtcSessionRequest`，原 RTMP URL façade、CLI 和保存流 schema v1
继续不变。

### 4.3 所有权、线程与停止

| 对象 | 唯一所有者 | 线程/写入 | 生命周期终点 |
| --- | --- | --- | --- |
| `WebRtcEndpointSession` | 测试或产品组合根 | 控制线程；库回调只提交弱状态 | `close()` 完成并释放 PC/Track |
| `H264PublisherSource` | publisher 组合对象 | 独立读取/节奏线程 | stop token 唤醒、汇合线程 |
| `EncodedVideoDecodeSession` | media playback manager | 固定 DecodeWorkerPool worker | 队列清空、decoder/mailbox 注销 |
| session package | signaling | 原子文件 I/O | 成功导入、正常退出或过期清理 |
| metrics snapshot | 各生产者 | 原子/锁保护只读复制 | 读取方不延长协议资源生命周期 |

固定停止顺序：

```text
停止新输入和新重连
→ 标记 closing、递增 generation、失效回调
→ 停止并汇合 publisher source（如有）
→ 关闭 Track 和 PeerConnection
→ 清空有界发送/接收/解码队列
→ 注销 decoder mailbox 与媒体时钟
→ 销毁协议、媒体和线程资源
```

不得在持有库回调需要的锁时同步等待关闭。重复 close/stop 必须安全；旧 generation 的回调、AU、
状态和帧只能丢弃。任一路失败不得停止其他会话。

### 4.4 身份与安防边界

- `sessionId` 只关联一次 Offer/Answer；过期后不能复用。
- `StreamId` 只关联本机播放、邮箱、渲染和媒体新鲜度。
- `deviceId/cameraId` 仍由产品应用层和现有设备档案管理，不从 SDP、candidate 或 sessionId 推断。
- WebRTC Connected/Direct 不表示机器人在线、可控或已执行命令。
- P2P 失败不得自动解锁控制、伪造恢复或静默启动 RTMP。
- 安全新鲜度只认当前 StreamId、当前 generation 的真实呈现帧，阈值保持 1,000 ms。

## 5. 手工信令、CLI 与测试资产

### 5.1 schema v1 保持冻结

一次性会话包继续严格只含：`schemaVersion`、`sessionId`、`role`、`createdAtUtc`、
`expiresAtUtc`、`descriptionType`、`sdp`。`role` 只表示 offer/answer，不表示 publisher/viewer。
现有 10 分钟有效期、2 分钟未来时钟容差、256 KiB 文件上限、192 KiB SDP 上限、原子写入、
当前用户 DACL 和受限清理全部保持。

### 5.2 计划 CLI

```text
rtmp_monitor_webrtc_client
  --media-role=publisher|viewer
  --signaling-role=offer|answer
  --source=sample|camera        # 仅 publisher；camera 从 Week 9 开始
```

- LAN 默认空 ICE server。
- 公网测试的 STUN 只从仓库忽略的本机受限配置或交互式输入读取，不进入命令历史、日志或包。
- CLI 不接受 SDP、candidate、Token 或会话包内容；交换目录继续固定。
- viewer 模式创建显示窗口；publisher 可无窗口运行，但仍是同一个可执行文件和版本。

### 5.3 社团测试资产

进入 Week 6 前必须获得可分发的固定 H.264 样本，并记录来源、许可、文件名、大小、编码属性和
版本校验元数据。样本可作为独立受控测试包资产，不要求提交大视频到 Git；若许可或分发方式未确认，
Week 6 包装门禁保持阻塞，不能让测试者依赖开发机上的未跟踪文件。

## 6. 十周 WBS

### 6.1 规则与汇总

表中每项只产生一个主要输出；“验收/停线”同时定义完成标准与不得绕过的失败条件。状态只使用
`未开始`、`进行中`、`阻塞`、`通过`、`失败`。网络等待、30 分钟 smoke 和 600 秒采样本身不计
人工工时。2026-08-21 用户以“完成 Week 3”的明确指令授权先行实施，并要求后补手工验证；该授权
只解除开发等待，不把仍未执行的 `W2-GATE` 双控制台人工项改写为通过。Week 4 仍需新的实施指令。

| 周次 | 任务数 | 人时 | 主要输出 |
| --- | ---: | ---: | --- |
| Week 1 | 14 | 36 | 学习、环境与隐私基线 |
| Week 2 | 19 | 66 | 默认关闭的开发者信令与回环基础 |
| Week 3 | 14 | 49 | H.264 契约、解码入口与 RTMP 兼容 |
| Week 4 | 13 | 45 | 对称 endpoint session 与 MP4 publisher |
| Week 5 | 12 | 42 | 同一客户端 viewer 与双角色媒体回环 |
| Week 6 | 12 | 37 | 社团双机 LAN 测试包与矩阵 |
| Week 7 | 11 | 32 | 公网 Direct 或 Needs Relay 证据 |
| Week 8 | 13 | 45 | 正式客户端一次性接收集成 |
| Week 9 | 13 | 45 | 摄像头、四路和故障隔离 |
| Week 10 | 14 | 43 | 性能、包、跨平台和 Beta 资格 |
| **合计** | **135** | **440** | **双客户端优先 P2P Beta** |

### Week 1：知识、环境与隐私基线（状态保持）

| ID | 状态 | h | 目标 |
| --- | --- | ---: | --- |
| W1-BAS-01 | 未开始 | 3 | 记录 Git、版本、CTest 和 RTMP 当前基线 |
| W1-KNW-01 | 未开始 | 2 | 建立 RTMP Server 与 PeerConnection 对照表 |
| W1-KNW-02 | 未开始 | 2 | 解释 Offer、Answer 和 SDP 的职责 |
| W1-KNW-03 | 未开始 | 3 | 解释 ICE candidate、candidate pair 和状态机 |
| W1-KNW-04 | 未开始 | 3 | 解释 NAT、CGNAT、STUN 和 TURN |
| W1-KNW-05 | 未开始 | 3 | 解释 DTLS、SRTP、RTP 和 RTCP |
| W1-DES-01 | 未开始 | 2 | 绘制信令路径 |
| W1-DES-02 | 未开始 | 3 | 绘制 ICE 协商与媒体路径 |
| W1-DES-03 | 未开始 | 3 | 绘制接收、解码和渲染路径 |
| W1-ENV-01 | 未开始 | 2 | 采集移动网络电脑的脱敏环境信息 |
| W1-ENV-02 | 未开始 | 2 | 采集公司电脑的脱敏环境信息 |
| W1-ENV-03 | 未开始 | 3 | 核对时钟、防火墙、UDP、IPv4 和 IPv6 条件 |
| W1-TST-01 | 未开始 | 3 | 建立网络失败分类表 |
| W1-GATE | 未开始 | 2 | 执行知识、环境和隐私人工门禁 |

Week 1 学习任务未因开发优先而自动完成；历史计划和当前状态不得改写为通过。

### Week 2：开发者信令与回环基础（状态保持）

| ID | 状态 | h | 已有输出 |
| --- | --- | ---: | --- |
| W2-LDC-01 | 通过（自动证据） | 2 | libdatachannel 0.24.5 与许可证 |
| W2-LDC-02 | 通过（自动证据） | 3 | 头文件、库、运行 DLL 和 imported target |
| W2-LDC-03 | 通过（隔离 probe） | 4 | 最小 PeerConnection 构建 |
| W2-LDC-04 | 通过（自动回环） | 4 | 无 STUN/TURN host 回环 |
| W2-LDC-05 | 通过（离线样本） | 3 | H.264 packetizer API |
| W2-LDC-06 | 通过（离线样本） | 3 | H.264 depacketizer API |
| W2-BLD-01 | 通过（R2） | 3 | 默认关闭的 developer target 边界 |
| W2-BLD-02 | 通过（OFF 审计） | 4 | OFF 不发现、不链接、不部署 |
| W2-BLD-03 | 通过（ON/负向配置） | 4 | 精确依赖发现与脱敏错误 |
| W2-SIG-01 | 通过 | 3 | 一次性 schema v1 |
| W2-SIG-02 | 通过 | 4 | Offer 原子导出 |
| W2-SIG-03 | 通过 | 4 | Answer 原子导出 |
| W2-SIG-04 | 通过 | 4 | UTF-8、JSON、字段与类型校验 |
| W2-SIG-05 | 通过 | 3 | 角色、时间、大小与关联校验 |
| W2-SIG-06 | 通过 | 4 | 导入、退出、过期和启动清理 |
| W2-SEC-01 | 通过 | 3 | 字段允许列表与敏感扫描 |
| W2-TST-01 | 通过 | 4 | ON session/H.264/loopback 测试 |
| W2-TST-02 | 通过 | 4 | OFF CTest/target/产物/入口审计 |
| W2-GATE | **阻塞/等待用户人工复核** | 3 | 自动技术门禁已通过，双控制台人工项未完成 |

详细自动证据见 `../versions/webrtc-v2/weeks/week02/test_results.md`。W2 人工项仍待用户补充；
Week 3 的先行实施依据上方明确授权单独记录，不反推 W2 人工门禁完成。

### Week 3：H.264 契约、解码入口与 RTMP 兼容

| ID | 状态 | h | 前置 | 单一输出与验收 | 失败/停线 |
| --- | --- | ---: | --- | --- | --- |
| W3-ARC-01 | 通过 | 4 | W2-GATE | 经确认的 R3 依赖、所有权、线程和兼容记录 | 未确认或出现 media↔transport 依赖即停止 |
| W3-CON-01 | 通过 | 3 | W3-ARC-01 | 冻结 `H264AccessUnit`/`SessionMediaSample`；无 RTP/身份字段 | 需要 SDP、peerId 或协议指针即退回设计 |
| W3-CON-02 | 通过 | 3 | W3-ARC-01 | 冻结 `SignalingRole`、`VideoDirection`、`WebRtcSessionConfig` | 信令角色与媒体方向耦合即失败 |
| W3-CON-03 | 通过 | 3 | W3-CON-01 | 冻结 source/sink 的有界提交结果和关闭语义 | `void` 丢弃、无容量或所有权不明即失败 |
| W3-MED-01 | 通过 | 4 | W3-ARC-01 | RTMP façade、signal 顺序、StreamId 和停止特征测试 | 无法锁定现状不得重构 |
| W3-MED-02 | 通过 | 4 | W3-MED-01,W3-CON-01 | media 内聚的 `EncodedVideoDecodeSession` | decoder/queue/mailbox 分属多 owner 即失败 |
| W3-MED-03 | 通过 | 4 | W3-MED-02,W3-CON-03 | manager 创建/撤销外部 H.264 输入的 generation handle | 暴露 FFmpeg/transport 对象或句柄可越代即失败 |
| W3-RTM-01 | 通过 | 4 | W3-MED-02 | `FFmpegPlayer` 通过内部入口复用解码会话 | 改变 URL 验证、重连或音频行为即停止 |
| W3-RTM-02 | 通过 | 4 | W3-RTM-01,W3-MED-03 | 原 `addStream(displayName,url)` façade 完全兼容 | 调用方被迫迁移到 MediaSource 即失败 |
| W3-LIF-01 | 通过 | 3 | W3-MED-03 | 解码入口幂等关闭、过期样本和队列清理契约 | Stop 后仍提交或 worker 残留即失败 |
| W3-TST-01 | 通过 | 4 | W3-CON-03,W3-LIF-01 | 契约、容量、generation、空/超大 AU 自动测试 | 丢弃不可观察或越界即失败 |
| W3-TST-02 | 通过 | 4 | W3-RTM-02 | Windows Debug 全量 CTest、RTMP/AAC 与依赖门禁 | 任一既有行为变化即停止 |
| W3-DOC-01 | 通过 | 2 | W3-TST-02 | Week 3 实际边界与迁移记录 | 把目标写成已交付即失败 |
| W3-GATE | **通过（自动技术门禁）** | 3 | W3-TST-01,W3-TST-02,W3-DOC-01 | `通过`或`失败`的 M1 门禁 | 无完整 RTMP 回归不得进入 Week 4 |

实际边界和命令证据见 `../versions/webrtc-v2/weeks/week03/`。该结论只证明契约、解码入口与 RTMP
兼容；WebRTC Track、publisher、viewer、真实 P2P 视频和产品 UI 均未在 Week 3 实现。

### Week 4：对称 endpoint session 与 MP4 publisher

| ID | h | 前置 | 单一输出与验收 | 失败/停线 |
| --- | ---: | --- | --- | --- |
| W4-ARC-01 | 3 | W3-GATE | session/source/test-client 目标与依赖冻结 | transport 创建 source/UI 即停止 |
| W4-SES-01 | 3 | W4-ARC-01,W3-CON-02 | 两种信令角色×两种媒体方向的合法配置表 | 固定 publisher=Offerer 即失败 |
| W4-SES-02 | 4 | W4-SES-01 | `WebRtcEndpointSession` 唯一拥有 PC/Track/generation | 多 owner 或全局 PC 即失败 |
| W4-SES-03 | 4 | W4-SES-02 | 弱状态、有界回调和控制线程投递 | 裸 this、回调访问 UI 即停止 |
| W4-SES-04 | 4 | W4-SES-03 | closing→generation→callback invalidation→close | 关闭顺序倒置或死锁即失败 |
| W4-PUB-01 | 3 | W4-ARC-01 | 固定 MP4 的许可、编码属性和分发阻塞记录 | 未知许可样本不得进入测试包 |
| W4-PUB-02 | 4 | W4-PUB-01,W3-CON-01 | MP4 H.264 demux、AVCC→Annex-B 与 SPS/PPS/IDR 输出 | 无界 AU 或错误格式即失败 |
| W4-PUB-03 | 4 | W4-PUB-02 | 基于媒体时间和单调时钟的可中断 pacing | 固定 sleep 累积漂移即失败 |
| W4-PUB-04 | 3 | W4-PUB-03,W3-CON-03 | 有界发送入口、超限策略和关键帧恢复 | source 阻塞 libdatachannel 回调即失败 |
| W4-CLI-01 | 4 | W4-SES-04,W4-PUB-04 | 同一 `rtmp_monitor_webrtc_client` 的 publisher shell | 新建独立 publisher executable 即失败 |
| W4-TST-01 | 4 | W4-CLI-01 | sender/offer 与 sender/answer、停止和错误测试 | 只覆盖固定 Offerer 即失败 |
| W4-DOC-01 | 2 | W4-TST-01 | MP4 publisher 结果和资产边界 | 未分发样本写成可复现即失败 |
| W4-GATE | 3 | W4-TST-01,W4-DOC-01 | publisher/session 门禁 | 未证明角色正交或幂等关闭不得进入 Week 5 |

### Week 5：同一客户端 viewer 与双角色媒体回环

| ID | h | 前置 | 单一输出与验收 | 失败/停线 |
| --- | ---: | --- | --- | --- |
| W5-RCV-01 | 4 | W4-GATE | ReceiveOnly H.264 Track 和 codec/fmtp 校验 | ICE 成功但无兼容 Track 不得伪装成功 |
| W5-RCV-02 | 4 | W5-RCV-01 | libdatachannel depacketize 后的 Annex-B AU 边界 | 自行复制 RFC 6184 状态机即停止 |
| W5-RCV-03 | 3 | W5-RCV-02 | AU 大小、SPS/PPS、IDR 和畸形输入策略 | 无界重组或旧参数集复用即失败 |
| W5-MED-01 | 4 | W5-RCV-03,W3-MED-03 | 当前 generation AU 接入现有 decoder/mailbox | 创建第二套 decode/render 框架即失败 |
| W5-CLI-01 | 4 | W5-MED-01 | 同一测试客户端的 viewer 窗口模式 | viewer 使用不同 executable/协议栈即失败 |
| W5-MAT-01 | 4 | W5-CLI-01 | publisher/Offerer ↔ viewer/Answerer 自动双进程 | 无真实文件交换或无真实帧即失败 |
| W5-MAT-02 | 3 | W5-CLI-01 | viewer/Offerer ↔ publisher/Answerer 自动双进程 | receiver 不能创建 Offer 即失败 |
| W5-MED-02 | 3 | W5-MAT-01,W5-MAT-02 | 晚加入等待当前 SPS/PPS+IDR 后起播 | 用旧 generation 或非 IDR 起播即失败 |
| W5-LIF-01 | 4 | W5-MAT-01,W5-MAT-02 | 两端重复关闭、单端退出和晚回调矩阵 | 退出残留、崩溃或旧帧即失败 |
| W5-TST-01 | 4 | W5-MED-02,W5-LIF-01 | 同机 E2E、媒体错误、容量和脱敏测试 | 只证明 ICE 不证明出画即失败 |
| W5-DOC-01 | 2 | W5-TST-01 | 两种角色组合的实际结果 | 混写为 LAN/公网结果即失败 |
| W5-GATE | 3 | W5-TST-01,W5-DOC-01 | 单机双客户端媒体门禁 | 两种组合任一未过不得进入 LAN 包装 |

### Week 6：社团双机 LAN 测试包与矩阵

| ID | h | 前置 | 单一输出与验收 | 失败/停线 |
| --- | ---: | --- | --- | --- |
| W6-ENV-01 | 2 | W5-GATE | 两台获准 Windows 电脑的脱敏 LAN 基线 | 未获授权或环境不可追溯即阻塞 |
| W6-PKG-01 | 4 | W6-ENV-01,W4-PUB-01 | 同版本客户端、DLL 和许可材料的阶段目录 | 依赖开发机 PATH 或缺许可即失败 |
| W6-PKG-02 | 3 | W6-PKG-01 | 样本资产 manifest 与版本校验元数据 | 样本不可合法分发即阻塞 |
| W6-GDE-01 | 3 | W6-PKG-02 | 面向社团的双机操作与失败排查手册 | 要求手改 SDP/关闭整机防火墙即失败 |
| W6-LAN-01 | 4 | W6-GDE-01 | sender/Offerer 的 host Direct 与真实呈现 | 只记录 Connected 无呈现即失败 |
| W6-LAN-02 | 4 | W6-GDE-01 | viewer/Offerer 的 host Direct 与真实呈现 | 角色互换失败即门禁失败 |
| W6-LAN-03 | 4 | W6-LAN-01,W6-LAN-02 | 每种组合连续 10 轮连接/关闭 | 任一残留或不可复现失败须记录 |
| W6-LIF-01 | 3 | W6-LAN-03 | publisher/viewer 单端退出与对端收敛 | 对端仍显示 Direct 即失败 |
| W6-SEC-01 | 2 | W6-GDE-01 | 防火墙、时钟、交换渠道最小授权清单 | 扩大系统权限或记录地址即停止 |
| W6-SEC-02 | 3 | W6-LAN-01,W6-LAN-02 | 两端输出、文件和包的敏感扫描 | 任一 SDP/IP/端点命中即停线 |
| W6-DOC-01 | 2 | W6-LAN-03,W6-LIF-01,W6-SEC-02 | 可交接 LAN 结果和限制 | 把 LAN 写成公网能力即失败 |
| W6-GATE | 3 | W6-PKG-02,W6-DOC-01 | 社团测试包资格结论 | 样本/许可/双角色/清理任一缺失即阻塞 |

### Week 7：公网 Direct 或 Needs Relay

| ID | h | 前置 | 单一输出与验收 | 失败/停线 |
| --- | ---: | --- | --- | --- |
| W7-AUT-01 | 2 | W6-GATE | 公司与移动网络、公网 STUN 的明确授权记录 | 无授权不执行公网探测 |
| W7-CFG-01 | 3 | W7-AUT-01 | 受限本机运行时 ICE 配置输入 | 真实 URL 进入源码/CLI/日志即停线 |
| W7-STN-01 | 3 | W7-CFG-01 | STUN 可达、不可达和超时分类 | 把 STUN 当媒体 Relay 即失败 |
| W7-MAT-01 | 3 | W7-STN-01 | publisher/Offerer 公网尝试 | 缺 selected pair 事实不得声明 Direct |
| W7-MAT-02 | 3 | W7-STN-01 | viewer/Offerer 公网尝试 | 角色互换未测不得通过 |
| W7-DIR-01 | 4 | W7-MAT-01,W7-MAT-02 | 非 relay pair + 当前 generation 真实呈现证据 | 仅凭配置 STUN/ICE Connected 即失败 |
| W7-NRL-01 | 3 | W7-MAT-01,W7-MAT-02 | checks 穷尽后的 Needs Relay 诚实结论 | 把 codec/配置错误误报为 Relay 需求即失败 |
| W7-FLT-01 | 3 | W7-STN-01 | 超时、错误 STUN、单端退出和网络变化矩阵 | 无限重试或状态不收敛即失败 |
| W7-SEC-01 | 3 | W7-DIR-01,W7-NRL-01 | 公网输出和会话材料敏感扫描 | 地址、URL、凭据进入结果即停线 |
| W7-DOC-01 | 2 | W7-FLT-01,W7-SEC-01 | Direct/Needs Relay 范围化报告 | 对任意网络作通用承诺即失败 |
| W7-GATE | 3 | W7-DOC-01 | 公网阶段门禁 | 无证据时允许 Needs Relay，不允许伪造通过 |

### Week 8：正式客户端一次性接收集成

| ID | h | 前置 | 单一输出与验收 | 失败/停线 |
| --- | ---: | --- | --- | --- |
| W8-ARC-01 | 4 | W7-GATE | 产品组合根、UI、事件和安全 R3 记录 | 需要 schema v2 或 profiles→transport 即停止 |
| W8-API-01 | 3 | W8-ARC-01 | 一次性 `WebRtcSessionRequest`；无 peerId/持久字段 | 运行时会话混入 SavedStreamProfile 即失败 |
| W8-UI-01 | 4 | W8-API-01 | ON 构建下显式的一次性 WebRTC 入口 | OFF 显示入口或启动自动联网即失败 |
| W8-UI-02 | 4 | W8-UI-01 | 受管目录 Offer/Answer 导入导出与取消 | 任意路径/内容进入最近记录即失败 |
| W8-APP-01 | 4 | W8-UI-02,W5-MED-01 | app 组合根组装 session、decode handle 和 video widget | transport/media 直接访问 QWidget 即失败 |
| W8-STA-01 | 4 | W8-APP-01 | Connecting/Direct/Needs Relay/Error 事实映射 | ICE Connected 无帧显示 Direct 即失败 |
| W8-EVT-01 | 3 | W8-STA-01 | 建连失败、恢复和 Needs Relay 的脱敏事件 | 事件暗示设备离线/已恢复控制即失败 |
| W8-DIA-01 | 3 | W8-STA-01 | transport/media/render 只读诊断快照 | 诊断延长 PC 或 SDP 生命周期即失败 |
| W8-SAF-01 | 4 | W8-APP-01 | 当前代真实呈现接入 1,000 ms 安全新鲜度 | 改阈值或以 RTP/解码代替呈现即停线 |
| W8-SAF-02 | 2 | W8-SAF-01 | 禁止静默 RTMP 回滚和 WebRTC 授权控制 | 失败后自动拉 RTMP/解锁即失败 |
| W8-CFG-01 | 3 | W8-API-01 | 证明 schema v1、SavedStreamProfile 和 autoConnect 不变 | 写入 P2P/session 字段即失败 |
| W8-TST-01 | 4 | W8-EVT-01,W8-DIA-01,W8-SAF-02,W8-CFG-01 | UI/状态/事件/安全/配置/RTMP/MQTT 自动回归 | 任何旧契约变化即失败 |
| W8-GATE | 3 | W8-TST-01 | 一次性产品接收门禁 | 无隐私、兼容和安全证据不得进入 Week 9 |

### Week 9：摄像头、四路和故障隔离

| ID | h | 前置 | 单一输出与验收 | 失败/停线 |
| --- | ---: | --- | --- | --- |
| W9-CAM-01 | 3 | W8-GATE | 获授权摄像头的脱敏格式能力表 | 保存用户画面或设备序列号即停线 |
| W9-CAM-02 | 3 | W9-CAM-01 | 实际可用 H.264 原生/编码器能力表 | 仅凭文档假定 encoder 即失败 |
| W9-CAM-03 | 3 | W9-CAM-02 | 原生 H.264 优先、否则已验证 encoder 的确定选择 | 无许可兼容路径则摄像头分支阻塞 |
| W9-CAM-04 | 4 | W9-CAM-03 | 可中断、幂等的摄像头采集会话 | Stop 阻塞或设备误选即失败 |
| W9-CAM-05 | 3 | W9-CAM-04 | 当前 source generation 的单调媒体时间戳 | 墙钟排序或时间倒退未处理即失败 |
| W9-CAM-06 | 3 | W9-CAM-03,W9-CAM-04 | 必要且有界的像素格式转换 | 原生 H.264 被无意义重编码即失败 |
| W9-CAM-07 | 4 | W9-CAM-05,W9-CAM-06 | 低延迟编码、GOP、SPS/PPS 和 IDR 策略 | 无法有限恢复关键帧即失败 |
| W9-CAM-08 | 4 | W9-CAM-07 | 摄像头复用既有 H264 source port | 复制 PeerConnection/session 栈即失败 |
| W9-CAM-09 | 3 | W9-CAM-08 | 单路摄像头真实呈现与隐私结果 | 仅连接无画面或保存原件即失败 |
| W9-MUL-01 | 4 | W8-GATE | 四个独立 PC/Track/generation/queue/StreamId 会话 | 共享可变协议状态即失败 |
| W9-FLT-01 | 4 | W9-MUL-01 | 单路停止、网络中断和新 generation 恢复矩阵 | 故障传播至其他三路即失败 |
| W9-RES-01 | 4 | W9-FLT-01 | 每路 CPU/内存/队列/丢弃与 30 分钟 smoke | 无界增长或归属不清即失败 |
| W9-GATE | 3 | W9-CAM-09,W9-RES-01 | 摄像头与四路隔离门禁 | 摄像头可阻塞但不得以 MP4 冒充通过 |

### Week 10：性能、包、跨平台和 Beta 资格

| ID | h | 前置 | 单一输出与验收 | 失败/停线 |
| --- | ---: | --- | --- | --- |
| W10-BAS-01 | 2 | W9-GATE | Git/工具链/依赖/样本/配置资格基线 | 测试中基线改变则结果作废 |
| W10-WIN-01 | 3 | W10-BAS-01 | Windows Debug ON 全目标构建 | 目标缺失或依赖回退即失败 |
| W10-WIN-02 | 3 | W10-WIN-01 | Windows ON 完整 CTest | 过滤、超时或失败不得忽略 |
| W10-WIN-03 | 3 | W10-WIN-02 | Windows Release 全目标构建 | 混入 Debug/旧产物即失败 |
| W10-PKG-01 | 3 | W10-WIN-03 | 最小 WebRTC/Qt/FFmpeg DLL 清单 | 依赖开发机 PATH 即失败 |
| W10-PKG-02 | 3 | W10-WIN-03 | 实际二进制对应的第三方许可证清单 | 任一许可来源不明即阻塞发布 |
| W10-PKG-03 | 3 | W10-PKG-01,W10-PKG-02 | 包内会话、端点、用户状态和调试物扫描 | 任一敏感命中立即停线重建 |
| W10-OFF-01 | 3 | W10-WIN-03 | OFF 配置/构建/CTest/目标/DLL/UI/启动行为审计 | OFF 携带 WebRTC 即失败 |
| W10-PER-01 | 4 | W10-BAS-01 | 单路预热后 600 秒 P50/P95/max 与资源 | LAN P95>200 ms 不得通过 |
| W10-PER-02 | 4 | W10-PER-01,W9-RES-01 | 四路 600 秒逐路指标与有界资源 | 用单路代替四路或资源增长即失败 |
| W10-REC-01 | 3 | W10-PER-02 | 断网、单端退出、新代恢复和旧帧审计 | 旧 generation 复活即失败 |
| W10-ARM-01 | 4 | W10-WIN-02 | ARM64 RASTER/GLES3 交叉构建与 ELF 依赖 | 交叉构建不得写成真机通过 |
| W10-DOC-01 | 2 | W10-PKG-03,W10-OFF-01,W10-PER-02,W10-REC-01,W10-ARM-01 | Beta 说明、限制和必要 known issues | 隐藏阻塞或夸大公网/ARM 即失败 |
| W10-GATE | 3 | W10-DOC-01 | `通过`、`失败`或明确外部条件`阻塞`的最终结论 | 任一硬门禁缺证据不得发布 |

## 7. 测试矩阵与发布门禁

### 7.1 自动化

- H.264 contract：空/超大 AU、Annex-B、SPS/PPS/IDR、generation 和有界丢弃。
- session：四种角色配置中只允许当前两种单向配对；closing、晚回调和重复关闭。
- 双进程：sender/Offerer 与 viewer/Offerer 两种拓扑均使用真实文件、真实 PC 和真实媒体帧。
- media：RTMP façade 特征、外部 H.264 ingress、共享解码池、容量 1 mailbox 和旧帧隔离。
- product：一次性 UI、状态、事件、诊断、1,000 ms 新鲜度、无静默回滚、schema v1 和 MQTT 不变。
- 构建：ON/OFF、缺依赖、target graph、链接命令、DLL、依赖方向和敏感扫描。

### 7.2 人工与环境

- Week 2：用户完成双控制台 offer/answer、清理和隐私复核。
- Week 6：社团使用同一包在两台电脑分别运行两种 Offer 角色组合，每种连续 10 轮。
- Week 7：只有取得网络和 STUN 授权后才执行公网测试；失败可以形成 Needs Relay 合法结论。
- Week 9：摄像头使用必须单独授权，不保存画面原件。
- ARM64：交叉构建只证明工程可构建，真机结论继续待真实设备验收。

### 7.3 隐私门禁

日志、测试输出、包和周结果扫描 SDP、candidate、ICE 凭据、fingerprint、IPv4/IPv6、端口、
STUN/TURN URL、Token、完整 UUID和会话路径模式，要求零命中。真实端点只存在于获授权、被忽略且
受限的本机配置中；不进入 CLI 历史、源码、文档示例或制品。

## 8. 风险与停线条件

| 风险 | 处理 |
| --- | --- |
| CGNAT/企业 UDP 阻断 | 诚实报告 Needs Relay；不关闭整机防火墙，不承诺通用直连 |
| H.264 fmtp/SPS/PPS/IDR 不兼容 | 固定最小 H.264 能力和批准样本，不用 UI 特判掩盖 |
| 测试资产不可分发 | Week 6 包装阻塞；不得依赖未跟踪开发机文件 |
| 回调晚于 stop | closing、generation、弱状态、有界投递和重复关闭测试 |
| RTMP 解码提取扩散 | 保留原 façade，先补特征测试，每阶段只改变一个边界 |
| 身份混淆 | sessionId、StreamId、deviceId/cameraId 分离；WebRTC 不授予控制 |
| 敏感材料泄露 | 立即停线、清理、审计引用并重建受影响制品 |
| OFF 构建污染 | 任何 WebRTC target、符号、DLL、入口或启动网络行为均使门禁失败 |

RTMP/AAC/MQTT/控制安全回归、无界队列、退出残留、旧 session 帧、敏感持久化或依赖反转中的
任一项失败，都必须停止当前阶段。不得用替代实现绕过，也不得把配置存在、编译成功或 ICE Connected
冒充真实媒体和产品资格。

## 9. 后续阶段

获得可信设备身份、自动信令、服务器基础设施和产品档案需求后另立计划：

1. 定义设备/摄像头资源与媒体会话的认证绑定。
2. 部署 WSS 信令与短期 Token，再评审是否需要长期 P2P profile/schema v2。
3. 部署 coturn，验证 forced relay、凭据轮换、带宽和成本。
4. 评估 SRS WHIP/WHEP、服务器分发、多观看端和集中证据链。
5. 按安防业务优先级独立实施 WebRTC/Opus 半双工对讲，不复用视频 DataChannel 控车。

只有出现真实第二种持久媒体来源后，才评审 `MediaSource` variant；只有出现真实同时收发用例后，
才扩展 `VideoDirection::SendReceive`。

## 10. 结果管理

本文是 Week 1～10 唯一任务规划来源。`docs/versions/webrtc-v2/weeks/week01/`～`week10/` 只保存
实际完成后的 `summary.md`、`test_results.md` 和必要的 `known_issues.md`。

大日志、构建产物、测试视频、会话包、抓包和二进制留在忽略目录或受控测试资产存储。每周结论必须
区分代码存在、构建成功、自动测试、同机双进程、双机 LAN、真实公网、交叉构建和真机验证，任何一类
证据都不能替代另一类。
