# 《GoObject 渲染流程解析（案例 720）》映射

> 来源 PDF 共 18 页，已于 2026-08-03 完整阅读并逐页检查。

## 四层映射

| PDF 角色 | RtmpMonitor 对应实现 |
| --- | --- |
| 用户交互层 | `MainWindow`、`VideoGridWidget`、拖拽、选择和全屏入口 |
| 应用逻辑层 | `StreamConnectionController`、设备状态、StreamId 和流生命周期 |
| 渲染控制层 | `VideoRenderController`、`RenderSnapshot`、Dirty 合并和绘制调度 |
| 底层渲染层 | `OpenGLGridRenderer`、YUV uploader、纹理、Shader、Qt FBO |
| `GoDrawGroup` 总控 | 依 Snapshot 顺序合成全部 `RenderItem` 的单画布 renderer |
| 具体图元绘制类 | YUV plane 上传、视频 quad、黑边/裁剪和后续 overlay renderer |

## 采用的思想

- 交互、业务状态、渲染控制和 GPU 资源分层。
- 由一个总控对象按稳定顺序组织底层绘制。
- Dirty 只表达“需要重新生成结果”，不把业务对象直接交给 GPU 层。
- 底层职责拆成资源准备、参数设置和 draw，便于按层定位故障。

## 明确不照搬

PDF 中的 CAE 图元低频且可长期驻留；视频帧是高频、允许丢弃的时间序列。因此：

- 不为每帧创建业务绘制对象，不建立随时间增长的场景图。
- `setDirty` 映射为 atomic bit accumulator；相同位重复设置只计为合并。
- 连续帧只覆盖容量 1 邮箱，不为每帧投递 Qt queued event。
- 不跨 Context/线程调用 OpenGL；所有 GL 操作留在对应 QOpenGLWidget GUI/Context 线程。
- 全屏可短时存在第二个 Context，但不共享 GLuint，避免 Context 共享生命周期污染。

## 故障定位链

```text
无画面
  -> StreamId/状态是否正确
  -> mailbox 是否有递增 sequence
  -> Dirty 是否置位且 scheduler 是否请求 update
  -> frame 是否上传、纹理是否复用
  -> viewport/scissor/UV 是否有效
  -> Shader 编译、颜色 uniform 和 Context generation 是否正确
```
