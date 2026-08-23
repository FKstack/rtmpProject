# WebRTC V2 Week 3 实施摘要

## 结论

Week 3 的 H.264 契约、可复用解码入口和 RTMP 兼容工作已完成，`W3-GATE` 记录为“自动技术门禁
通过”。本周没有实现 WebRTC Track、publisher、viewer、双客户端 executable、产品 UI 或网络媒体
收发；RTMP/SRS 仍是唯一稳定产品路径。

`W2-GATE` 的双控制台人工复核仍未执行。2026-08-21 用户明确要求先完成 Week 3、稍后补手工
验证，因此本周按该先行授权实施，但没有反向把 Week 2 人工项标为通过。

## 实际完成

- 新增协议无关的 `H264AccessUnit` 与 `SessionMediaSample`。AU 只保存 Annex-B 字节、微秒媒体
  时间戳和关键帧标志；generation 位于会话信封，不包含 RTP、SDP、candidate、peer/device 身份。
- 冻结正交的 `SignalingRole`、`VideoDirection` 和运行时 `WebRtcSessionConfig`；第一阶段只存在
  `SendOnly`/`ReceiveOnly`，没有预建 `SendReceive`。
- 新增显式 `H264SubmitResult`，区分 accepted、替换积压后 accepted、容量/等待关键帧丢弃、
  closed、invalid-generation、invalid-AU 和资源失败。
- 从 `FFmpegPlayer` 迁出 decoder、压缩队列、DecodeWorkerPool 归属、解码指标和容量 1 邮箱，统一
  由 `EncodedVideoDecodeSession` 创建、停止和销毁。`FFmpegPlayer` 保留 RTMP URL 校验、网络输入、
  重连、AAC 分流和 Qt signal façade。
- `MultiStreamPlaybackManager` 新增 move-only `EncodedVideoInputHandle`。manager 拥有 StreamId 和
  解码会话；handle 只拥有一个运行时 generation 的提交/关闭权。关闭先失效 generation，再等待
  解码任务、清队列和邮箱；关闭后及旧 generation 提交均有明确返回。
- `addStream(displayName, rtmpUrl)`、RTMP URL 规则、StreamId 递增、重连、音频选择和保存流 schema
  保持不变。外部 H.264 流不支持音频选择，也不写入保存档案。
- 层依赖门禁新增 H.264/WebRTC contract 检查；依赖保持
  `media -> h264 contracts`，`webrtc_dev` 与 media 仍互不依赖。

## 生命周期与故障收敛

```text
停止新 RTMP/外部 AU 输入
→ 推进或失效当前 generation
→ 等待固定 worker 上的有界解码批次
→ 释放 AVCodecContext，清压缩队列和容量 1 邮箱
→ 使晚到状态、错误、AU 和帧不可见
```

重复关闭安全。解码失败会清理当前积压并回到等待下一关键帧的可恢复状态；manager 只接受当前
generation 的状态，关闭后晚到 Connecting/Playing 不会复活。具体 callback 仍是只读观察，owner
通过 Qt queued invocation 回到控制线程，不从 worker 直接访问 UI。

## 架构影响

- 风险等级：在用户确认的 R3 总方向内执行 R2 模块/公共契约/生命周期变更。
- 职责：decoder/queue/mailbox 从 `FFmpegPlayer` 迁到单一 `EncodedVideoDecodeSession`；RTMP 网络和
  重连继续由 `FFmpegPlayer` 持有；外部 generation 句柄由 manager 创建和撤销。
- 依赖：新增纯 C++ H.264 contract 边，未新增 media↔transport、profiles→transport 或产品 WebRTC
  链接。
- 兼容：主程序 CLI、RTMP/AAC/MQTT/UI/schema、默认 OFF 网络行为均不变。
- 下一阶段：Week 4 才允许创建 endpoint session、Track、MP4 publisher 和同一测试客户端组合根；
  本周接口不构成这些能力已交付的证据。

## WBS 状态

`W3-ARC-01`、`W3-CON-01`～`03`、`W3-MED-01`～`03`、`W3-RTM-01`～`02`、`W3-LIF-01`、
`W3-TST-01`～`02`、`W3-DOC-01` 与 `W3-GATE` 均通过。没有发现需要创建 Week 3
`known_issues.md` 的可复现未解决问题。一次测试观察者析构顺序缺陷已由 MSVC AddressSanitizer
定位并修复；修复后 lifecycle 测试在 ASan 下连续 5 轮通过。最终独立 Offer/Answer 双进程复核
再次完成真实文件交换、连接和零残留清理，证明 Week 2 信令未被本周变更破坏。
