# V2 Week 05：公网服务器单路 WHEP PoC

## 本周目标

完成参考发布器 → WHIP → SRS → WHEP → Qt 的单路 H.264 路径。

## 知识

- recv-only PeerConnection 如何生成 Offer。
- ICE gathering 完成后再提交非 trickle WHEP Offer 的原因。
- WHEP 响应 SDP、Location resource 和 DELETE 生命周期。
- libdatachannel 回调线程与 Qt 对象线程的边界。

## 实验

- 在回环 SRS 中比较浏览器 WHEP SDP 与 Qt 计划生成的最小 H.264 SDP 摘要。
- 用参考 H.264 720p30 源验证 SPS/PPS、IDR 周期和 packetization-mode。

## 开发任务

- CMake 开关 ON 时发现并链接 libdatachannel 0.24.5，OFF 时保持零依赖。
- 实现 WHEP Offer、HTTP SDP 交换、Answer 应用、Track/RTP 接收和 DELETE。
- Authorization 只存在内存和 header，不进入 URL、日志或保存流。
- 回调经 generation 检查后进入现有解码队列。
- 提供 H.264 WHIP 参考发布器和回环运行步骤。

## 验收

- 单路 720p30 能进入现有 FFmpeg 解码和渲染路径。
- 停止幂等，WHEP resource、PeerConnection 和 Track 无残留。
- SRS 停止/恢复、发布端断开/恢复、客户端重连无旧 session 帧。
- RTMP、AAC、渲染和安全停车完整回归通过。

## 风险与停止条件

- 不记录原始 SDP、Location、Authorization 或 endpoint。
- codec/fmtp 不兼容时修复协议协商，不在 UI 中硬编码旁路。
- 未达到单路稳定前不开始 P2P。

## 下周入口

建立只在内存中路由会话消息的 Go WSS 信令服务。
