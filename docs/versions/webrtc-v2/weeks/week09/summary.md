# WebRTC V2 Week 9：摄像头发布与四路产品会话

> 完成日期：2026-08-30
>
> 当前门禁：`blocked(camera_environment)`。真实摄像头 CAM-09 未获授权；Week 10 代表负载 runner 已补齐
> W9-RES-01 的 1,800 秒资源与恢复资格。

## 1. 本周实际完成范围

Week 9 在既有 publisher 目标内增加了窄职责 `CameraH264PublisherSource`，并把正式产品的
`WebRtcProductSessionController` 从单会话扩展为最多四条互相隔离的运行期会话。没有新增通用
`MediaSource` 插件层，没有修改 schema v1、保存档案、设备控制、RTMP fallback、TURN/WSS 或默认
网络策略。

摄像头发布固定请求 1280×720@30。Windows worker 使用 Media Foundation 枚举和打开设备，公开信息
只有 `camera-1`、`camera-2` 等本次运行别名。它先查找原生 H.264 类型，并用实际 AU 预检 SPS、PPS、
baseline profile、level≤3.1、B slice 和两次 IDR 间隔；合规时 Annex-B 直通。不合规时关闭原 capture
对象、重新打开同一设备并选择 NV12，且只有合成 NV12 实际完成 `h264_mf` 编码和 FFmpeg 解码预检后
才启用固定 GOP 30、零 B 帧、低延迟回退。编码器不可用时稳定返回兼容路径不可用，不扩展 NVENC、
AMF、QSV 或 x264 矩阵。非 Windows 稳定返回 `platform_unsupported`。

两条输出路径共同执行 Annex-B、4 MiB AU、恢复 IDR 携带 SPS/PPS和时间戳归零策略。每个 source 只有
一个采集/编码 worker，无用户态帧队列；停止通过 closing 标志与 SourceReader Flush 打断读取，独立
join 门禁保证 waiter/stop 并发安全，并只在 mutex 内按同一指针清理 reader 后释放 MF/FFmpeg。

产品控制器现在持有以 `StreamId` 为键的私有 `SessionContext` map。每项独占 slot、token、widget、
input handle、receive runtime、mailbox、连接事实、状态和 freshness。slot 使用最低空闲项，对应
`session-01` 至 `session-04`；第五路稳定返回 `capacity_reached`。单 tile 删除只取消对应流，菜单取消
动作仍取消全部。endpoint generation、product token 和 media generation 继续是三个独立概念。

## 2. 公共契约与兼容性

- `ClientPublisherSource` 增加 `Sample`、`Camera`；publisher 支持 `--list-cameras` 和
  `--source=camera --camera-index=N`，viewer 继续拒绝 source/camera 参数。
- controller 增加带输出 `StreamId` 的 `start()`、逐路 cancel/state/diagnostics/exchangeRoot 和逐路状态
  signal；既有无参方法保留。
- 无参 `state()` 按 Error > NeedsRelay > Connecting > Direct > Idle 聚合；无参 diagnostics 在单路时
  返回该路，多路时只返回无效 StreamId 的聚合状态，不相加或伪造逐路计数。
- `WebRtcProductEvent`/`WebRtcProductDiagnostics` 增加 `streamId`，诊断增加 `mediaGeneration`。
- `H264AccessUnit`、`SessionMediaSample`、`H264SubmitPort`、profiles/schema 和控制授权契约未改。

## 3. 生命周期和故障隔离

单路关闭顺序为：token 失效，requestStop，join runtime，关闭 input handle，移除 media stream，移除
widget，擦除 context。取消全部时先对所有 runtime 发停止，再统一 join 和释放。共享诊断 timer 只在
最后一路结束后停止；单路陈旧、远端关闭、容量拒绝或重建不会停止其他三路的 decode worker pool 或
呈现。

集成测试创建四组真实 SendOnly/ReceiveOnly PeerConnection，验证四路 RTP→Annex-B→FFmpeg decode→
mailbox→presented→Direct，第五路零副作用拒绝、远端先关闭一路时另外三路继续呈现、单路停止小于
1 秒、最低 slot 重建和新 generation。同步 state/diagnostics/Cancelled signal 的取消重入与网格动画
期间立即取消也有回归。后续 Week 10 代表性 720p30 runner 已完成预热 60 秒和 1,800 秒四路测量，
第 600 秒停止一路、第 720 秒重建，约 2 秒恢复 Direct，其余三路持续呈现。

## 4. 架构影响

- 风险等级：R2。
- 职责：publisher 内新增摄像头具体 source 和私有策略；product controller 改为拥有最多四个独立
  context，不把会话状态推给 UI、media 或 transport。
- 依赖：publisher 仍只依赖 H.264/FFmpeg，Windows 条件链接 MF 库；新增层门禁禁止 publisher 包含
  `webrtc_runtime` 或 `webrtc_product`。`media → render → ui` 与 product 组合方向未变。
- 数据和隐私：设备真实名称、symbolic link、序列号、SDP、candidate、ICE 凭据、地址和内容帧不进入
  报告；不保存截图、裸 H.264 或内容哈希。
- OFF 行为：WebRTC=OFF 继续不生成相关目标、菜单、runtime 或自动联网行为。

## 5. 当前不能声称的结果

真实摄像头未获显式授权，因此没有枚举或打开物理设备。CAM-01～09 的物理设备事实均未完成；
CAM-02～08 只有实现与组件/合成证据，不能写成现场通过。同机四组真实 PeerConnection 只证明软件
隔离，`physicalFourEndpointClaimed=false`。实现阶段 fresh Debug OFF/Debug ON/Release ON 基线为
39/39、48/48、48/48；P1 生命周期与 Windows Qt 本地运行时加固后，当前代码又完整重跑 CTest：
Debug OFF 39/39（122.66 秒）、Debug ON 48/48（201.71 秒）、Release ON 48/48（173.61 秒），并在
移除 Qt/MinGW PATH 与插件路径变量后直接验证受影响程序无弹窗退出。Week 9 原 smoke 的小型 fixture
只提供生命周期证据；Week 10 已用代表性 720p30 负载补齐四路 1,800 秒、全程逐路队列/丢弃/延迟、
进程工作集和故障恢复：斜率 0.134 MiB/min、首末 60 秒增长 2.802 MiB，队列与 cleanup 通过。
因此 W9-RES-01 通过、`W9-GATE=blocked(camera_environment)`；全局
`performanceQualified=false` 仍表示物理 LAN 未资格，不回写为 Week 9 资源失败。
