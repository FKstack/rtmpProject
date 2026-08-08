# Week 6：产品级 OpenGL 视频渲染与验证总览

## 1. 当前结论

Week 6 已不再是“把 RGB 图片上传到单路 `QOpenGLWidget`”的实验。当前生产代码已经形成一条可切换后端的视频渲染链：FFmpeg 解码得到不可变 YUV 帧，容量为 1 的邮箱保存每路最新帧，主窗口用一个共享画布合成 1～16 路，全屏窗口按需创建临时画布；OpenGL 初始化失败时自动使用 CPU/QPainter 后端。

截至 2026-08-04，证据必须分层理解：

| 层级 | 已验证事实 | 不能据此宣称 |
|---|---|---|
| Windows x86_64 构建/单测 | Debug 完整 CTest 12/12；生产 YUV framebuffer、渲染核心、动态网格和生命周期测试通过 | 不能代替 10 分钟性能资格测试 |
| Windows 实机 OpenGL | NVIDIA GeForce RTX 3060 Laptop GPU，OpenGL 4.6.0 NVIDIA 591.86；实际创建 Desktop GL 上下文 | 不能推断其他 GPU/驱动同样通过 |
| Windows 四路 RTMP | 修复后的客户端在不进入全屏的情况下连接即显示 4 路；指标为 4 个可见 RenderItem、4 个绑定邮箱和非零 upload/render | 不是 16 路长稳性能结论 |
| framebuffer 画质 | YUV420P/NV12、BT.601/709/2020 NCL、Limited/Full 共 8 例通过 | OpenGL 并非“天然更清晰” |
| Linux ARM64 | Debug 全量交叉构建 94/94；主程序、ES3 EGL smoke 和渲染核心均为 ELF64/AArch64 | 未在真实盒子证明 QPA、GPU、解码或长稳可用 |

2026-08-05 四组 600 秒 A/B 与 Quality 门禁全部通过，因此 CLI 默认值已从 `cpu` 切换为 `auto`。`auto` 先尝试 OpenGL，能力、Context 或 Shader 初始化失败时仍自动回退 CPU；`--renderer=cpu` 保留为诊断和发布回滚路径。

## 2. 从历史原型到当前产品路径

| 项目 | 历史 Week 6 原型 | 当前产品实现 |
|---|---|---|
| 输入 | RGB/RGBA `QImage` | YUV420P 或 NV12 `VideoFrame` |
| 画布 | 单路实验 Widget | 主网格单画布 + 临时全屏画布 |
| 纹理 | 可能按帧重建 RGBA 纹理 | 尺寸/格式不变时复用 YUV 纹理并 `glTexSubImage2D` |
| Shader | RGB 复制/Blitter | Desktop 330 与 ES 300 YUV→RGB Shader |
| 调度 | UI 信号驱动图片更新 | Dirty 位 + 15/30 FPS 有界调度 |
| 队列 | 图片进入 UI 事件队列 | 每路容量 1 最新帧邮箱 |
| 回退 | 原型失败即不可用 | `VideoCanvasHost` 自动选择 CPU 后端 |
| 指标 | 测试是否启动 | schema v3 记录请求/实际后端、GL 身份、上传、绘制和纹理 |

旧 `VideoRenderWidget` 只保留为历史 RGB 上下文/析构烟测，不是生产数据路径。ARM64 基线是 OpenGL ES 3.0，不再用 GLES2 描述生产承诺。

## 3. 本轮首帧黑屏故障及修复

用户提供的两段录像显示：四路 RTMP 已连接，主网格仍为黑屏；进入一次全屏后，四路一起出现。录像排除了“nginx 未发布”“四路都未解码”和“某一路纹理坏掉”，把范围缩小到主画布的低频 Snapshot 更新。

根因是下面这种连接方式：信号连接到 lambda，同时要求 `Qt::UniqueConnection`。Qt 6 不能为该 lambda 建立唯一连接，运行时拒绝连接，所以首帧 `showFrame()` 没有驱动 `refreshRenderSnapshot()`。全屏生命周期走了另一条 Snapshot 刷新路径，看起来像“全屏修好了画面”。

修复包含两部分：

1. 将连接目标改为 `VideoGridWidget::handleRenderStateChanged` 成员函数，使 `Qt::UniqueConnection` 有合法、可比较的接收端。
2. `RenderItem::frameVisible` 只表达业务帧是否可显示，不再混入 `QWidget::isVisibleTo()` 的瞬时状态；隐藏/最小化由画布调度器停止 `update()`。

新增回归不手工刷新 Snapshot，而是让隐藏锚点收到首帧并验证信号链自动更新。结果为动态网格测试 29/29、完整 CTest 12/12；四路实机验证没有进行任何双击，连接后立即显示。

## 4. 画质证据

生产 framebuffer 测试先由 CPU 公式生成参考 RGB，再让实际 OpenGL Shader 渲染到 framebuffer 并读回像素。门槛为每例 PSNR ≥35 dB、平均绝对误差（MAE）≤3、P99 通道误差≤8。

| 格式 | 矩阵/范围 | PSNR dB | MAE | P99 | 结果 |
|---|---|---:|---:|---:|---|
| YUV420P | BT.601 Limited | 55.6320 | 0.1778 | 1 | 通过 |
| YUV420P | BT.709 Limited | 52.6217 | 0.3556 | 1 | 通过 |
| YUV420P | BT.2020 NCL Limited | 50.8608 | 0.5333 | 1 | 通过 |
| YUV420P | BT.709 Full | ∞ | 0 | 0 | 通过 |
| NV12 | BT.601 Limited | 49.6114 | 0.3556 | 2 | 通过 |
| NV12 | BT.709 Limited | 48.6423 | 0.5333 | 2 | 通过 |
| NV12 | BT.2020 NCL Limited | 46.0896 | 0.8889 | 2 | 通过 |
| NV12 | BT.709 Full | ∞ | 0 | 0 | 通过 |

这组数据证明颜色矩阵、range、plane、stride 和 Shader 路径相对 CPU 参考没有可见退化。它不证明 OpenGL 比 CPU 更清晰；两条路径的目标本来就是得到同样的画面。

## 5. 性能证据怎样才成立

OpenGL 的预期收益是把 YUV→RGB、缩放和 16 路合成交给 GPU，并避免解码线程生成/缩放 RGB `QImage`。是否真的更好，必须由同机 A/B 决定：

```text
同一程序 + 同一批预编码输入 + 同一窗口 + 8 个解码 worker
CPU 600 s -> 冷却 30 s -> OpenGL 600 s
```

正式硬门槛包括：

- 采样中的实际后端必须为 `opengl`，GL vendor/renderer/version 非空，纹理字节大于零。
- 16 路 OpenGL 平均应用 CPU 相对 CPU 至少降低 15%，平均显示 FPS 不低于 14。
- 源到显示最差流 P95 相对 CPU 不恶化超过 10%，且 P95≤750 ms、最大≤1500 ms。
- 最新帧年龄和内部延迟 P95 不恶化超过 10%。
- UI 最大调度间隔小于 500 ms，压缩包队列不超过 45。
- OpenGL 纹理字节预热后稳定；工作集斜率≤2 MiB/分钟，首末窗口均值增长≤64 MiB。
- 8 个画质用例全部通过。

只有这些门槛全部通过，才把默认后端改为 `auto` 并重新回归。任一项失败都保留 `cpu` 默认，并记录失败数据，而不是挑选有利指标。

## 6. 2026-08-05 正式 600 秒结果

四组均使用 16 路、8 个解码 worker、20 秒预热，按 CPU→冷却 30 秒→OpenGL 顺序执行。脱敏机器可审查摘要见 [week6_renderer_comparison_results.json](week6_renderer_comparison_results.json)，完整原始每秒样本保存在被 Git 忽略的 `out/renderer-comparison-formal/`。

| 指标 | CPU | OpenGL | 结论 |
|---|---:|---:|---|
| 16 路平均应用 CPU | 4.85% | 1.50% | 降低 69.08%，通过≥15%门槛 |
| 16 路平均显示 FPS | 12.74 | 14.91 | OpenGL 通过≥14 FPS |
| 16 路 frame age P95 | 46 ms | 43 ms | 改善 |
| 16 路内部延迟 P95 | 42 ms | 40 ms | 改善 |
| 16 路 UI 最大间隔 | 145 ms | 271 ms | OpenGL <500 ms |
| 16 路最大压缩包队列 | 1 | 1 | 通过≤45 |
| 双屏最差流源到显示 P95 | 214 ms | 196 ms | 改善，远低于 750 ms |
| 双屏最差流最大延迟 | 2060 ms | 317 ms | OpenGL 通过≤1500 ms |
| 双屏 frame age P95 | 45 ms | 43 ms | 改善 |
| 双屏内部延迟 P95 | 51 ms | 42 ms | 改善 |
| 双屏 UI 最大间隔 | 1203 ms | 441 ms | OpenGL 通过<500 ms |
| OpenGL 纹理字节 | 0 | 22,118,400 | 预热基线稳定 |

纹理稳定门禁有一个重要细节：Camera 03 计划内断流时有 1 个样本从 22,118,400 降为 20,736,000 字节，说明该路资源被主动释放；其余 599 个样本都处于基线，恢复后的末 60 秒完全回到基线，从未高于基线。门禁因此检查“不得向上增长且末 60 秒完全恢复”，而不是错误要求故障注入期间也不能释放资源。这个规则有固定 SelfTest 覆盖。

正式门禁全部通过后，CLI 默认值切换为 `auto`。切换后重新构建，OpenGL 标签测试 3/3、完整 CTest 12/12（77.13 秒）通过；不传 `--renderer` 的空客户端指标记录 `requestedBackend=auto`、`activeBackend=opengl`、无 fallback。CPU 显式回滚路径继续保留。

## 7. 如何复现本页证据

先运行不启动 GUI 的脚本自测，然后运行构建/图形测试：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\compare_renderers.ps1 -Action SelfTest
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\test_week6_opengl.ps1 -SkipConfigure
```

常规输出位于：

- `out/week6-opengl/windows-opengl-validation.json`
- `out/week6-opengl/framebuffer-quality-results.txt`
- `out/week6-opengl/quality-artifacts/`
- `out/renderer-comparison/comparison.json`
- `out/renderer-comparison/comparison.md`

完整 A/B 操作见 [自动化脚本实战篇](week6_renderer_performance_test_guide.md)，框架和 OpenGL 知识见 [产品渲染框架教学篇](week6_product_rendering_framework_tutorial.md)。

## 8. 平台边界

Windows 使用 OpenGL 3.3 Core Shader，当前实机驱动提供 4.6；Linux ARM64 使用 ES 3.0 Shader。交叉构建只证明编译器、ABI、Qt OpenGL 和 FFmpeg 依赖正确，目标板仍需分别验证 QPA 插件、EGL surface、真实 GPU 驱动、软件/硬件解码与长期运行。PBO、共享 Context 上传线程、D3D11VA、DMA-BUF/EGLImage、HDR tone mapping 和 ES2 不在本轮承诺范围。
