# WebRTC V2 Week 4 实施摘要

## 结论

`W4-GATE` 已通过自动技术门禁。默认关闭的 Week 4 路径现在可以从固定 MP4 样本读取 H.264，转换为
Annex-B AU，并由同一无窗口客户端以 Offerer 或 Answerer 身份通过真实 WebRTC video Track 发布。
测试专用 ReceiveOnly peer 已用 libdatachannel 重组 RTP，并验证首个可恢复关键帧同时包含
SPS、PPS 和 IDR。

这不是 viewer 出画、双机 LAN、公网 Direct/Relay、发布包样本或 ARM 真机的完成证据。Week 2
双控制台人工复核仍标记为待执行。

## 实际实现

- `rtmp_monitor_webrtc_contracts` 保存与媒体方向正交的 session 配置契约。
- `rtmp_monitor_webrtc_transport` 由 `WebRtcEndpointSession` 唯一拥有 PeerConnection、Track、
  generation、容量 2 的发送队列和 sender worker；提交端口只捕获 weak state 与 generation。
- transport 固定 H.264 payload type 102、90 kHz、最大 RTP 分片 1200、Annex-B、
  `profile-level-id=42e01f`；单 AU 上限 4 MiB。
- `rtmp_monitor_h264_publisher_source` 独占 FFmpeg demux、`h264_mp4toannexb` BSF 和 pacing worker，
  以 DTS 排序/节拍、PTS 生成微秒媒体时间戳，并使用可中断的绝对 `steady_clock` deadline。
- `rtmp_monitor_webrtc_client` 是唯一生产组合根，只接受 publisher、sample、offer/answer 和有界超时；
  文件信令仍沿用 schema v1，样本只能位于可执行文件旁 `webrtc-assets/sample.mp4`。
- BUILD_TESTING 下的测试 peer 独立拥有 ReceiveOnly PC/Track、RTCP receiving session 和 H.264
  depacketizer；它不安装、不进入产品接收路径。

## 生命周期与依赖

关闭顺序是：停止信令输入，令 closing/generation 生效，使回调和提交端口失效，停止并汇合 source，
汇合 sender，重置回调并关闭 Track/PeerConnection，清队列，最后执行一次有界 `rtc::Cleanup()`。
transport 不依赖 publisher、media 或 UI；publisher source 不依赖 transport；只有 client 组合根连接
source、transport 与文件信令。`RTMP_MONITOR_ENABLE_WEBRTC=OFF` 时实现、客户端、测试 peer 和
WebRTC DLL 均不进入产品产物。

## 未完成

- Week 5 的产品 ReceiveOnly depacketize、decoder/mailbox 接入和 viewer 出画。
- 双机 LAN、公网、STUN/TURN、真实网络 candidate pair 与 ARM 真机。
- 样本分发和许可；资格样本只在忽略目录本地生成。
