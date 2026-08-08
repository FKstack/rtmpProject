# 当前视频渲染架构审查

> 审查日期：2026-08-03

## 基线问题

改造前，`FFmpegPlayer` 在共享解码 worker 中依据 UI 视口限帧、缩放，并通过
swscale 生成 RGB888 `QImage`。`MultiStreamPlaybackManager` 再以统一定时器轮询图片，
逐帧通知 `VideoWidget`，最终由每路 `VideoSurface` 使用 QPainter 绘制。

该路径能够工作，但存在四个产品化障碍：

- 解码输出格式、显示尺寸和 UI 生命周期相互耦合。
- YUV plane、stride、PTS、time base 和颜色描述在 UI 前丢失。
- 每路 QWidget 分别绘制，无法以一次合成稳定控制 16 路资源和刷新节奏。
- 指标无法区分解码、容量 1 覆盖、上传和最终绘制。

## 当前实现

当前生产链路为：

```text
网络线程 -> 有界压缩包队列 -> DecodeWorkerPool
         -> immutable VideoFrame -> LatestFrameMailbox(capacity=1)
         -> VideoRenderController / Dirty
         -> OpenGLGridRenderer 或 CPU fallback
```

- `FFmpegPlayer` 只产生 YUV420P8/NV12_8；其他软件解码格式先规范化为 YUV420P。
- `VideoFrame` 保留所有权、plane、有效行字节、带符号 stride、PTS、duration、time base、
  sequence、session generation 和颜色描述。
- 主网格只有一个 `VideoCanvasHost`；正常模式下只有一个 QOpenGLWidget Context/FBO。
- 全屏按需创建第二个画布并消费同一邮箱，不移动网格 QWidget，不共享 GLuint。
- OpenGL 不可用或初始化失败时，画布自动切换到 CPU/QPainter 诊断后端。

## 线程与所有权

| 对象 | 所在线程 | 所有权/同步 |
| --- | --- | --- |
| 网络读取 | 每流独立 QThread | stop flag + FFmpeg interrupt callback |
| AVCodecContext | 固定 DecodeWorkerPool worker | 同流固定 worker，不并发调用 |
| VideoFrame | worker 创建，跨线程只读 | AVFrame clone/ref 或自有 plane copy |
| LatestFrameMailbox | worker 写、GUI 读 | mutex，容量固定为 1 |
| Snapshot/Dirty | GUI 构建，worker 只置 FrameDirty | Snapshot 仅 GUI；Dirty 为 atomic |
| GL 对象 | QOpenGLWidget Context 线程 | Context 销毁回调和析构入口释放 |

## 仍需实机验证

- Windows 16 路 10 分钟 CPU/OpenGL 对照门禁。
- Linux ARM64 目标板 QPA、OpenGL ES 3.0、软件解码和长期稳定性。
- HDR tone mapping、硬件帧导入、PBO 和异步上传不在当前交付范围。
