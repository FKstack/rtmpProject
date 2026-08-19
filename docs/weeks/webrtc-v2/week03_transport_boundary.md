# V2 Week 03：协议无关 Transport 边界

## 本周目标

建立不泄漏 UI/FFmpeg/libdatachannel 类型的输入契约，并让 RTMP 先通过新入口保持原行为。

## 知识

- `std::variant` 用类型系统阻止 mode 与字段错配。
- façade 兼容层允许旧 API 保持稳定，同时逐步迁移内部调用。
- session generation 是异步回调和重连之间的数据所有权边界。

## 实验

- 用纯逻辑测试构造 RTMP/WHEP/P2P 三类来源，验证非法字段组合无法通过类型契约。
- 读取一个 schema v1 临时文件，确认只读迁移为 RTMP，原文件在保存前不改变。

## 开发任务

- 新增 `rtmp_monitor_transport`、`MediaTransportMode`、三类 source 和 `EncodedVideoAccessUnit`。
- 为 `MultiStreamPlaybackManager` 增加 typed overload；字符串 RTMP overload 委托到兼容入口。
- 保存流写入 schema v2，并兼容读取 v1。
- 扩展依赖门禁，禁止 transport include app/media/profiles/server/render/ui。
- WebRTC 构建选项默认 OFF；OFF 时不得发现或链接 libdatachannel。

## 验收

- RTMP StreamId 分配、URL 校验时点、重连、音频、指标和停止行为不变。
- schema v1→v2、三类 source 校验和敏感字段排除测试通过。
- Windows 全目标和完整 CTest 通过；ARM64 RASTER/GLES3 重新配置通过。

## 风险与停止条件

- 不把 PeerConnection 所有权放入 FFmpegPlayer。
- 不复制 DecodeWorkerPool、邮箱、音频或渲染策略。
- typed overload 改变旧错误隔离语义时立即回滚局部设计。

## 下周入口

在 transport 内实现纯测试的 RTP 解析和 H.264 访问单元重组。
