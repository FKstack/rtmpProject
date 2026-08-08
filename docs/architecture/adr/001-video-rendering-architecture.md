# ADR-001：单主 OpenGL 画布与临时全屏画布

- 日期：2026-08-03
- 状态：已采用；2026-08-05 正式门禁通过，默认后端为 `auto`

## 背景

产品需要在 Windows x86_64 和 Linux ARM64 上显示 0～16 路实时视频，同时保留现有
StreamId、动态布局、拖拽、全屏、重连和安全退出行为。原 QImage/QPainter 路径丢失 YUV
元数据且 CPU 成本较高；旧 QOpenGLWidget 仅为单路 RGB 原型。

## 决策

- 网格使用一个 OpenGL 画布合成全部流。
- Windows 全屏按需创建第二个临时画布，不共享 GL 对象；主窗口隐藏后停止主画布调度。
- CPU 后端保留为诊断和能力失败回退。
- Windows 请求 OpenGL 3.3 Core；嵌入式请求 OpenGL ES 3.0。
- 所有 GL 调用均在 Qt GUI/Context 线程，第一版不使用 PBO、共享上传线程或共享 Context。
- 运行参数为 `--renderer=auto|opengl|cpu`；`auto` 初始化失败时自动回退 CPU。

## 放弃的方案

| 方案 | 放弃原因 |
| --- | --- |
| 每路一个 QOpenGLWidget | 最多 16 个 Context/FBO，切换和嵌入成本高，硬件帧未来会重复导入 |
| 独立上传线程 + 共享 Context | 首版生命周期和驱动兼容风险大，收益尚无实测依据 |
| 继续解码到 RGB QImage | 保留 CPU 转换/缩放耦合，阻断 YUV Shader 和零拷贝演进 |
| 搬运网格渲染 QWidget 到全屏 | reparent 生命周期复杂，无法表达独立 30 FPS 全屏调度 |

## 结果

获得稳定的 `VideoFrame + LatestFrameMailbox` 边界、单画布批量合成、纹理复用、明确
Context 清理和 CPU 回滚路径。代价是 Windows 全屏短时拥有第二个 Context。四组 600 秒
正式对照通过后默认已切换为 `auto`；显式 `--renderer=cpu` 仍是诊断和发布回滚路径。
