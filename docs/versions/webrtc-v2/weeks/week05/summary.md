# WebRTC V2 Week 5 实施摘要

## 结论

`W5-GATE` 已通过自动技术门禁。同一个 `rtmp_monitor_webrtc_client` 现在既能作为 publisher 或
viewer，也能独立选择 Offerer 或 Answerer；两种角色组合都已通过真实 Track、RTP、H.264 AU、
FFmpeg 解码、capacity-1 mailbox 和 CPU 画布闭环。

Week 5 完成的是测试客户端中的单画面 viewer，不是正式产品 UI。桌面窗口的人工观感、双机 LAN、
公网、样本分发和 ARM 真机仍未验收；其中人工观感按本轮完成口径标为“待用户执行”，不阻塞自动
技术门禁。

## Week 5 主要做了什么

接收端不再以“ICE 已连接”代表成功，而是要求下面整条链路都产生当前 generation 的证据：

```text
RTP
  → libdatachannel H264RtpDepacketizer
  → Annex-B access unit 恢复门控
  → EncodedVideoInputHandle
  → 既有 FFmpeg EncodedVideoDecodeSession
  → LatestFrameMailbox(capacity=1)
  → 既有 CPU VideoCanvasHost
  → 非空、非黑 framebuffer
```

接收门控只缓存当前 endpoint generation 的 SPS/PPS。畸形、超限、下游容量丢弃或 generation
变化都会清空恢复状态；必须等到新的 SPS、PPS 和 IDR 才重新向 decoder 提交。RTP 90 kHz 的
32 位时间戳会展开回绕、首帧归零并转换为单调微秒。

## 类和组件职责

| 类或目标 | 单一职责 | 所有权与线程 |
| --- | --- | --- |
| `WebRtcEndpointSession` | 拥有 PeerConnection/Track，协商方向和 H.264 codec/fmtp，安装官方 depacketizer，统计 RTP/AU 和关闭 endpoint | transport 所有；libdatachannel 回调与 sender worker 均受 generation/closing 约束 |
| `H264ReceivePipeline` | 私有 Annex-B 解析、SPS/PPS/IDR 恢复、4 MiB 上限和 RTP 时间戳展开 | endpoint generation 内部状态；不接触 decoder、Qt 或 QWidget |
| `WebRtcClientOptions` | 解析并冻结媒体角色、信令角色、sample 和 timeout 的 CLI 契约 | main 线程值对象 |
| `WebRtcClientRuntime` | 拥有文件信令、endpoint、可取消 control worker 和可选 publisher source | worker 线程执行；停止请求不在锁内 join |
| `WebRtcViewerController` | 在 Qt UI 线程组合 manager、外部输入 handle、单画面窗口和 CPU canvas，并生成 decoded/presented 证据 | UI 线程拥有窗口和媒体句柄；receive sink 只捕获弱 handle |
| `WebRtcClientMain` | 创建 `QApplication`，按角色装配组件，维持唯一 Cleanup 边界 | 进程组合根；不实现媒体策略 |
| `EncodedVideoInputHandle` | 为现有 media 解码入口重新盖上 media generation，并提供有界 H.264 提交 | viewer controller 共享持有；transport 只见弱引用 |
| `EncodedVideoDecodeSession` | 复用既有 FFmpeg H.264 解码和最新帧发布 | media 所有 worker；公共接口未改变 |
| `LatestFrameMailbox` | 保存最新一帧，旧帧可被覆盖，避免 UI 反压 transport | media/render 既有 capacity-1 边界 |
| `rtmp_monitor_video_canvas` | 将 `VideoCanvasHost`、CPU/OpenGL canvas 从完整 UI 抽成窄复用目标 | 只依赖 Qt Widgets/OpenGLWidgets 与 render；产品 UI 和测试客户端共同链接 |

## 如何嵌入现有框架

transport、media、render/UI 仍是兄弟模块。transport 不包含 media/render/ui 头文件，也不知道
FFmpeg 或 QWidget；media 的公共契约没有修改。唯一跨层连接位于测试客户端组合根：viewer
controller 创建现有 `MultiStreamPlaybackManager` 的外部 H.264 输入 handle，endpoint 的 receive
sink 捕获其弱引用并调用 `submit(H264AccessUnit)`。因此 endpoint generation 与 media generation
各自在所属模块失效，不存在跨层共享 owner。

产品 `rtmp_monitor_ui` 改为链接窄画布目标；Week 5 客户端不再为了三块画布源码依赖完整产品 UI。
`RTMP_MONITOR_ENABLE_WEBRTC=OFF` 时客户端、libdatachannel、viewer 入口和 WebRTC DLL 仍不会进入
产品路径。

## 生命周期

关闭顺序固定为：取消信令等待；使 endpoint generation/回调失效；停止并汇合 source/control
worker；关闭 Track/PeerConnection；关闭媒体 handle 并移除流；注销画布、销毁窗口；最后全进程
执行一次 `rtc::Cleanup()`。所有入口允许重复调用，锁内不等待线程。

## 架构影响

- 风险等级：R2。
- 职责变化：transport 增加接收协议与恢复门控；客户端 main 拆成 options/runtime/viewer/controller
  组合；画布源码形成窄复用目标。
- 依赖变化：没有新增 transport → media/render/ui 依赖；跨层连接只在客户端组合根。
- 契约变化：`WebRtcEndpointSession` 增加协商前 ReceiveOnly sink、媒体不兼容错误和接收计数；media
  公共接口、RTMP façade、schema v1 和产品配置均不变。
- 验证：fresh OFF/ON CTest 39/39、44/44；Week 5 两种双客户端拓扑和 Week 4 完整回归通过。

## 明确未完成

- 正式产品 UI 的一次性 WebRTC 接收入口，保留到 Week 8。
- 两台电脑的 LAN、真实公网 Direct/Needs Relay、TURN 和 ARM 真机。
- 桌面动态 1280×720 观感、窗口交互和人工先关任一端，待用户按测试指南执行。
- 测试样本仍只在忽略目录本地生成，不提交、不安装、不分发。
