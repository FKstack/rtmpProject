# RtmpMonitor 当前项目快照

> 最近审查日期：2026-08-10
>
> 发布分支：`v0.1.0-alpha.1`
>
> 发布前基线：`fb54a6076f3cf05616617d77b17f97e4f28d2eb3`
>
> 说明：本快照依据源代码、CMake、Git 状态和本轮实际构建/CTest 结果整理。

> Week 6 状态：四宫格首帧黑屏已修复；主网格已改为居中的标准 16:9 监控网格，默认与全屏均使用 `Contain`，标准 16:9 流因此可以完整、铺满且不变形；异常比例完整留边，每格仍可手动选择 `Cover`。四组 600 秒 CPU/OpenGL 与 Quality 的历史正式门禁全部通过，CLI 默认 Renderer 为 `auto`，显式 CPU 回滚保留。

## 项目目标

RtmpMonitor 是一个使用同一套 C++17、Qt 6 Widgets 和 FFmpeg 代码，在 Windows x86_64 与 Linux ARM64 上接收、解码并多宫格显示 H.264/RTMP 视频的桌面/嵌入式客户端。

RTMP Server 采用独立 SRS 服务，不在 Qt 进程内实现 RTMP 协议。当前仓库已完成
Windows+WSL2 侧的最小配置、生命周期脚本、URL/配置生成和只读健康监控；ARM
真机、真实摄像头和最终分发仍按 ISSUE-008 保持 `[需要验证]`。

## 当前技术栈

- C++17、CMake 3.21+。
- Qt 6 Widgets；生产级单画布 OpenGL YUV renderer，并保留 CPU/QPainter 回退。
- FFmpeg 8.1.2 LGPL：avformat、avcodec、avutil、swscale。
- Windows x86_64：MSVC、Qt 6.6.1、Ninja。
- Linux ARM64：WSL2 Ubuntu 22.04、AArch64 GCC 11、Qt 6.2.4 目标库和 ARM64 sysroot。
- 外部 RTMP 基础设施：SRS 或 nginx-rtmp，默认 RTMP 端口 1935。

## 目标平台与本机环境

| 项目 | 已验证事实 |
| --- | --- |
| Windows | Windows 11；仓库个人绝对路径不写入版本控制。 |
| WSL2 | `Ubuntu-22.04-New`，版本 2，发行版 VHDX 位于 G 盘。 |
| WSL 网络 | mirrored networking、Windows 防火墙开启。 |
| WSL swap | 位于 F 盘。 |
| Codex Home | 使用本机 `CODEX_HOME`；用户目录下 `.codex` 是指向该目录的 Junction，个人绝对路径不入库。 |
| ARM64 sysroot | 位于 WSL VHDX 内；本任务不移动或修改其现有工具链边界。 |
| OpenViking Server | 0.4.11 已从预编译 wheel 安装到 `/opt/openviking/venv-0.4.11`；MCP 固定为 1.29.0；VLM 为 `openai-codex/gpt-5.6-luna`（OAuth，Responses，`reasoning.effort=none`）；配置和数据分别位于 `/etc/openviking`、`/var/lib/openviking`。`openviking.service.d/proxy.conf` 只为该服务设置本机 `127.0.0.1:7890` 代理和 localhost 例外，不修改 Clash 配置。 |
| OpenViking Windows 客户端 | `ovcli.conf`、插件状态和日志位于本机专用配置目录，该个人绝对路径不入库；Marketplace 插件 0.7.4 已安装并启用一次。`ovcli.conf` 不再固定 `actor_peer_id`，由插件按工作目录派生 peer；当前 CLI 的通用 `hooks` 功能为稳定且默认启用，已移除失效的旧配置项 `plugin_hooks`。 |
| Kimi Code MCP 客户端 | 项目级 `.kimi-code/mcp.json`（规则 `/.kimi-code/mcp.json` 已在 `.gitignore`）连接 `http://127.0.0.1:1933/mcp`，peer 固定 `E--rtmpProject`。2026-08-08 起 `OPENVIKING_API_KEY` 持久化为 Windows 用户级环境变量（HKCU，值与受限 `ovcli.conf` 一致），根治了此前“绕开安全启动器直接启动即 401”的问题；任意方式启动的新会话均可连接。仅主动工具调用，无自动 Hook 链路。 |
| WSL 登录保活 | `OpenViking-rtmpProject-WSL-KeepAlive` 登录后延迟 30 秒、禁止重复实例并隐藏维持 WSL 生命周期；无交互式 WSL 终端的五分钟空闲、四个 Windows HTTP 入口及任务停止/重启恢复已通过。 |

## 当前主要模块

- 应用组合：`StreamConnectionController`、`StyleLoader`。
- 播放与并发：`FFmpegPlayer`、`MultiStreamPlaybackManager`、`DecodeWorkerPool`。
- UI：`MainWindow`、`VideoGridWidget`、`VideoWidget`、`FullscreenVideoWindow`、`VideoCanvasHost`、`CpuVideoCanvas`、`VideoOpenGLCanvas`（GL 可选编译）、`LogPanel`。
- 视频帧与渲染：`VideoFrame`、`LatestFrameMailbox`、`VideoRenderController`、`OpenGLGridRenderer`（仅 `RTMP_MONITOR_HAS_OPENGL=1` 时编译）、`EmbeddedGlCapabilities`；旧 `VideoRenderWidget` 仅保留为历史 RGB 原型冒烟。
- Linux 平台：`LinuxApplicationBootstrap`、`LinuxRenderingPolicy`、`LinuxRendererFactory`（`src/platform/linux/`，仅 Linux 编译）；EGLFS 下全屏复用主画布。
- 日志与用户消息：`LogManager`、`SensitiveDataSanitizer`、`UserMessageService`。
- 构建与验证：Windows/ARM64 Preset、AArch64 toolchain、Linux `RASTER/GLES3/AUTO` 渲染模式、RTMP 和多路验收脚本、`scripts/qualify_embedded_device.sh` 板级资格脚本。

## 当前已验证进度

- 2026-08-10 Phase 0 审计已完成：169 个受跟踪文件、0 个未跟踪文件；发布排除项
  均保持忽略，凭据扫描命中只包含合成脱敏测试夹具/安全说明，跨平台指南中的个人
  盘符已改为占位符和环境变量。历史合并提交不重写，后续按 Phase 独立提交；
  `v0.1.0-alpha.1-rc1` 仅冻结 Phase 0 基线，不代表后续发布门禁完成。
- 2026-08-10 发布清单 Phase 0 的版本号与功能冻结项已完成：CMake 数字核心保持
  `0.1.0`，预发布版本集中定义为 `0.1.0-alpha.1`，程序 `--version` 使用同一生成
  配置；Windows Debug 主程序重建成功，完整 CTest 18/18 通过（91.77 秒）。本轮未
  设置 `RTMP_MONITOR_TEST_URL`，不构成新的真实流复验。后续仅接受发布清单内的
  必需改动；其余 Phase 0～5 项未因此自动完成。
- 2026-08-10 已创建发布候选分支 `v0.1.0-alpha.1`；当前 Windows Debug 工作树
  增量构建无待编译目标，完整 CTest 18/18 通过（92.76 秒）。本轮未设置
  `RTMP_MONITOR_TEST_URL`，因此真实流 UI 用例为跳过路径，真实流结论仍使用下方
  已记录的独立 Verifier/压力测试证据，不把本轮 18/18 冒充新的真实流验收。
- 2026-08-08 Linux 双路径渲染架构（`embedded_device_rendering_strategy.md` §16～§22）已由 Kimi 落地：`CpuVideoCanvas`/`VideoOpenGLCanvas` 拆为独立编译单元，`RtmpMonitorBuildConfig.h` 暴露 `RTMP_MONITOR_HAS_OPENGL`；新增 `src/platform/linux/`（bootstrap/policy/factory）与 `EmbeddedGlCapabilities` 纯判定；EGLFS 自动改为画布内单路 Snapshot 全屏；GLES3 P1 小优化（静态 sampler、批次 unpack、每 RenderItem 一次错误检查、纹理保留策略）完成。Windows Debug CTest 14/14 通过（新增 capabilities/policy 两个纯逻辑目标与 7 个动态网格用例）。
- 2026-08-08 WSL2 ARM64 双构建完成：RASTER 产物为 ELF64/AArch64 且 NEEDED 无 Qt6OpenGL/Qt6OpenGLWidgets/libEGL/libGLES；GLES3 产物含 Qt6 OpenGL/OpenGLWidgets 依赖且全部来自 sysroot。QEMU 用户态下纯逻辑测试：capabilities 10/10、policy 9/9、render core 9/9、user message 5/5 通过；logging 2 个计时敏感用例在 QEMU 下失败（QEMU 时序问题，Windows 端通过，与本次改动无关）。板级资格脚本 `scripts/qualify_embedded_device.sh` 已交付但未经真实板运行。
- Week 1～3：外部 RTMP 链路、动态 1～16 路 UI、单路 FFmpeg 拉流与安全退出已完成。
- Week 4：源码已支持 0～16 路动态连接、阻塞网络与共享解码池解耦、指标和自动化验收脚本。
- Week 5：设备状态、日志分层、脱敏和可配置重连相关模块已存在。
- Week 6 后续产品化：解码输出已切换为不可变 YUV `VideoFrame` 和容量 1 邮箱；主网格使用单 OpenGL 画布，全屏使用不共享 GLuint 的临时画布，OpenGL 失败自动回退 CPU。
- Week 6 指标已升级为 schema v3；生产 framebuffer 8 个质量用例、Renderer A/B 总控、独立测试流前缀、每秒采样、脱敏汇总和三篇教学/实战文档已完成。四组 600 秒正式门禁全部通过：OpenGL 16 路平均 CPU 相对降低 69.08%、显示 14.91 FPS，双屏最差流 P95 196 ms、最大 317 ms。
- 2026-08-04 已修复四路 RTMP 连接后主网格黑屏、进入一次全屏才显示的问题：Qt 6 拒绝 lambda 形式的 `Qt::UniqueConnection`，现改为成员槽；动态网格 29/29、完整 CTest 12/12 与四路无双击实机验证通过。
- 2026-08-08 此前将主网格默认设为 `Cover` 只是对超宽格子的临时补救，现已被 16:9 网格几何修复取代：`MonitoringGridGeometry` 为当前行列数选择整数像素中面积最大的近似 16:9 视频区、居中的动态 layout margins 和一致单格尺寸。普通窗口使用 4px 外边距/4px 格间距，F11 监控墙使用 0px/0px。新格默认 `Contain`，标准 16:9 流完整铺满；4:3、竖屏等异常比例完整留边；每格仍可手动选择 `Cover`，单路全屏固定 `Contain`，不提供 Stretch。
- `VideoWidget` 的设备名已移入 `videoSurface` 左上角半透明覆盖标签，不再占用独立标题行；`deviceName_`、tooltip 和 Snapshot 保留完整名称，省略标签不拦截鼠标。视频区下限为 144×81；窗口缩放、字体/样式变化、日志 Dock 显隐和动态布局均会重新计算。底部事件日志默认隐藏，但仍持续收集并可由“视图”菜单显示。
- `MainWindow` 提供“视图 → 监控墙模式”、F11 和 Esc 往返：进入时保存并隐藏窗口栏、菜单、工具栏、状态栏和日志 Dock，退出时原样恢复。监控墙临时强制各格有效模式为 `Contain`，退出后恢复原先逐格选择；从监控墙进入单路全屏再退出不会丢失监控墙状态。
- 2026-08-08 撤回一组基于非 VS 启动窗口误判而产生的临时 Canvas/overlay 层级修改后，使用 VS 的 MSVC 工具链完成 Windows Debug 构建，完整 CTest 12/12 全部通过（74.57 秒）。最终回归包含 64 组 1～16 路/四种窗口尺寸的纯几何用例、真实 16 格 viewport/覆盖标题、4:3/竖屏完整显示、日志 Dock、F11/Esc，以及 CPU/OpenGL 对 16:9/4:3/竖屏四边标记的 framebuffer 验证；原有 8 个 YUV/颜色/stride 质量用例保留。删除临时方向诊断日志后又在 `vcvars64.bat` 环境重新编译并执行生产 OpenGL framebuffer 目标，1/1 通过。
- 2026-08-08 第一轮居中 16:9 网格的 120 秒快速对照全部门禁通过：CPU 3.425%→0.203%，display FPS 11.047→14.914，frame age P95 46→34 ms；但它早于最终标题覆盖和监控墙实现。
- 最终监控墙版本的 120 秒快速对照实际命中 RTX 3060 Desktop GL 3.3、无 fallback：CPU 2.362%→0.197%（降低 91.66%），display FPS 8.496→14.914，internal latency P95 43→40 ms，最大 UI gap 149→174 ms，纹理稳定为 22,118,400 bytes；latest frame age P95 47→52 ms，超过 51.7 ms 门槛 0.3 ms，因此总控如实判定失败。两次短测均不替代四组 600 秒正式门禁。
- 视觉证据边界：用户确认 Visual Studio 生成并启动的程序显示正常。Codex 自动化账户启动的窗口与用户 VS 运行环境不同，不作为可比产品证据，也不再据此修改生产渲染代码。当前可重复的定量证据来自纯几何、CPU/OpenGL framebuffer 回读和用户对 VS 产物的人工确认；尚未固化 VS 窗口外围留白的像素测量截图。按状态文件精确停止后，1935 监听器及匹配客户端/FFmpeg/nginx 进程均为 0。
- 2026-08-09 Windows 单路全屏后续修复已完成代码与自动化门禁：无帧控制栏固定可见；有帧后经 1200ms 以 180ms `OutCubic` 收起，由底部 96px 热区滑入，离开防抖 250ms；清帧立即恢复。截图按钮与 `Ctrl+Shift+S` 抓取全屏画布 framebuffer、立即显示缩略图，并在线程池用 `QSaveFile` 异步原子保存 PNG。退出以最后 framebuffer 的 raster 冻结层遮挡切换，主网格 `surfacePresented()` 后才揭开，750ms 超时兜底。Visual Studio MSVC 环境 `Qt-Debug --fresh` 配置成功、136/136 构建成功、完整 CTest 17/17（92.74 秒）通过。真实 SRS 流下的工具栏、截图方向/颜色和 NVIDIA Overlay 录屏仍 `[需要验证]`，见 ISSUE-009。
- 2026-08-09 用户第二轮录像发现无帧顶部旋转控制栏、有帧截图后底栏不收起以及光标难以恢复。控制栏现改为底部 96px 热区内的受裁剪子控件，不再把表面动画到顶层窗口边界外；重复鼠标移动不再重启动画。全屏临时画布的鼠标事件穿透到统一处理器，底栏 250ms 收起与光标 2000ms 空闲隐藏使用独立计时，任何全屏鼠标活动立即恢复光标。新建独立 Windows Debug 目录完成 136/136 构建和 CTest 17/17（98.01 秒）；用户退出旧 F5 实例后，Visual Studio 当前构建目录的 `rtmp_monitor.exe` 已重新链接 1/1 成功。重新 F5 后的录像复验仍 `[需要验证]`。
- 2026-08-09 Windows Debug 退出堆损坏已定位到构建依赖追踪，而非 FFmpeg/OpenGL 运行时：Application Verifier Full Heaps 捕获 `FullscreenVideoWindow` 分配块末尾首次越界，Ninja 同期记录旧 `MainWindow.cpp.obj` 为 `#deps 0`。根因是 CMake 3.29 按 GBK 错误解码 `cl.exe` 的 UTF-8 中文 `/showIncludes` 前缀，头文件变化后调用方未重编译，产生类布局 ABI 不一致。CMake 现以原始字节探测前缀且探测失败即拒绝配置；全新 `Qt-Debug` 后 `MainWindow.cpp.obj` 恢复 347 条依赖并包含 `FullscreenVideoWindow.h`。143/143 构建、CTest 18/18、Verifier 真实流 UI 复测、CPU/OpenGL UI 关闭 30/30、单路真实流 30/30、16 路真实流 10/10，以及独立 ASan RelWithDebInfo 54/54 构建与真实流 UI 退出均通过；Verifier 已关闭。用户 Visual Studio F5 两种 renderer 各 30 次人工退出仍 `[需要验证]`，见 ISSUE-010。
- 2026-08-03 Windows Debug 全工程构建通过，完整 CTest 12/12 通过（65.87 秒），包含生产 YUV Shader framebuffer 像素验证和渲染核心并发/所有权/最终渲染延迟测试。
- 2026-08-03 Linux ARM64 Debug 全量交叉构建 94/94 目标步骤通过，最终改动又通过 45 步增量构建；主程序、ES3 EGL 冒烟和渲染核心测试均为 ELF64/AArch64，主程序依赖目标 Qt6 OpenGL/OpenGLWidgets 与 FFmpeg `.so`。
- 历史 GLES2 冒烟基线已提升为 GLES3；ARM64 主程序、测试、EGL/ES3 与 Qt OpenGL 目标均生成 AArch64 ELF。
- 上述 ARM64 结论仅证明交叉编译和链接，不证明真实盒子上的 QPA、GPU 或视频播放。

## 当前阻塞与未完成项

- Linux ARM64 真实硬件盒子的 QPA、GPU、FFmpeg 播放、输入和长期稳定性尚未验收。
- Windows 16 路正式性能资格测试已完成；更换 GPU/驱动、分辨率或部署环境后需重新执行。
- 2026-08-04 的四组 600 秒正式结果早于当前紧凑网格和 F11 监控墙；当前布局只完成两次 120 秒预录 Video 快速对照，最终版本还因 latest frame age P95 超出相对门槛 0.3 ms 而未全过。若要把当前布局作为新的完整发布负载认证，需要重新执行 Video、LiveLatency 与 Quality 的正式套件。
- RTMP Server 接入已于 2026-08-09 完成独立修复复验：Visual Studio `Qt-Debug` 现通过公共 `Windows-MSVC-vcpkg` 与本机 `VCPKG_ROOT` 使用正确 FFmpeg；无 toolchain 时 CMake 会给出可操作错误。全新配置、136/136 构建、CTest 17/17（85.29 秒）通过。SRS 6.0.184 的 1935/回环 1985、WSL/Windows 双侧 H.264 拉流、停推/同 URL 恢复、SIGQUIT 停止和未知 1935 占用拒绝已重跑通过；停止后端口与状态文件均清理。Kimi 的 4/16 路与 600 秒结果保留为历史证据，本次未重跑；Visual Studio F5 中文界面需用户在本机窗口最终确认。ARM 实机、真实摄像头、Docker 仍 `[需要验证]`，见 ISSUE-008。
- Windows 单路全屏的历史控制栏、完全黑屏、成功推流后工具栏不可唤出和退出白闪均已完成代码修复与自动化回归，但 Visual Studio F5、默认 OpenGL、NVIDIA Overlay 开启的真实 SRS 流录像尚未由用户确认；首帧、截图方向/颜色、停推与恢复推流保持 `[需要验证]`，见 ISSUE-009。
- Windows Debug 退出堆损坏的首次非法写入已由 Application Verifier 捕获并修复，自动化与真实流压力回归均通过；Visual Studio F5 下 `auto`/`cpu` 各 30 次人工关闭仍 `[需要验证]`，见 ISSUE-010。
- OpenViking 0.4.11 已启用为 systemd 服务并绑定 `127.0.0.1:1933`；MCP 已通过 wheel-only 固定为 1.29.0。doctor、`/health`、`/ready`、Windows 回环访问、重启恢复、user/root key 边界和临时 Session CRUD/清理均已验证。
- OpenViking Marketplace 与 `openviking-memory` 0.7.4 已各安装一次；插件声明四个官方 Hook 和一个内置 MCP。真实 MCP 握手及 18 个工具枚举已通过。2026-07-30 已在共享 `CODEX_HOME` 的 Codex CLI 中完成持久审批，四个 Hook 均显示 `Installed=1`、`Active=1`。真实 CLI `/compact` 已触发 `PreCompact(trigger=manual)`、真实 transcript 捕获、工作区 peer 和 Session commit accepted 均有日志证据；当时后台抽取任务因网络超时失败。
- 2026-08-03 当前 Desktop 已真实暴露并成功调用 `search_experience`、`read_experience`；常规 `search`、`recall`、`read` 也已从项目 peer 命中历史摘要。精确 MCP 工具与直接召回已通过，但仍需在自然新建的 Desktop 任务中证明 `SessionStart`/`UserPromptSubmit` 自动注入和跨任务唯一标记完全一致，故 ISSUE-003 仍未完全关闭。
- 2026-08-03 已从 9 个顶层 rtmpProject rollout 中排除当前任务并合并重复/续接记录，形成 6 个 `rtmp-history-<date>-<source-id>` 脱敏 Session；27 个子代理会话、原始 transcript 和工具输出未导入。六个 commit 均为 `completed`，每份都有 `archive_001`、`memory_diff.json` 和 `.done`，只抽取到 `E--rtmpProject` 的 `events/entities/preferences`。
- 固定 `actor_peer_id=E--rtmpProject` 曾导致其他工作区内容误写入项目 peer；完整 peer 已备份后，精确删除 7 个相关事件、2 个偏好和 1 个完全被其覆盖的软件实体。`ovcli.conf` 现改为工作区派生 peer：本机仓库仍派生 `E--rtmpProject`，另一工作区会派生独立 peer。全局其他项目记忆和现有模型调研 Resource 未删除。
- 2026-07-31 已确认 OpenViking 的 `openai-codex/gpt-5.4` 超时底层为 `httpcore.ConnectTimeout`，原因是 systemd 服务未加载代理环境而直接连接上游。现已增加仅作用于 `openviking.service` 的代理 drop-in 并重启；精确 endpoint 经 `127.0.0.1:7890` 连续 20 次均在 0.83 秒内得到预期 HTTP 405。真实 9,373 字节（5,973 字符）Resource 在 46 秒内完成摘要、overview、向量和关联 Session，journal 无新增 timeout；255 字符 abstract 和 1,920 字符 overview 已复核存在。未修改 Clash for Windows 的节点、规则、订阅或端口。
- 2026-08-03 最新只读复测显示现有代理链路再次间歇不稳：10 次目标 endpoint 探测仅 3 次得到预期 405，7 次为 TLS EOF。第一轮历史回填因此失败并完整回滚，第二轮在 Session 级精确回滚/重试保护下六份全部完成。本轮未修改 Clash、代理规则、systemd drop-in、端口或防火墙；残余风险见 ISSUE-007。
- 2026-08-03 已将 OpenViking VLM 从 `gpt-5.4` 切换为 `gpt-5.6-luna`。0.4.11 的 Codex Responses 适配器已做最小本机兼容修正：只对 `gpt-5.6-*` 传递 `reasoning.effort=none`；`ov.conf` 不写入其 schema 不接受的 `vlm.reasoning_effort`。`doctor` 显示 `openai-codex/gpt-5.6-luna` PASS；真实 `store=false` 探测返回 Luna，使用 33 输入、7 输出、共 40 Token。专用 Session commit 在约 21 秒内 `completed`，总计 14,480 Token、reasoning Token 为 0，`memory_diff` 无增删改；测试 Session、完成任务索引均已删除，唯一标记零命中且清理错误为 0。该验收只证明 Server VLM 与抽取链路，不关闭 ISSUE-003 的 Desktop 跨任务召回缺口。
- 2026-08-03 一次配置语法检查错误地输出完整 `ov.conf`，导致当时的 Server root key 被工具输出回显。该 key 已立即随机轮换；当前配置和回滚备份已同步新值并保持 `600`、`fklightdog:fklightdog`。Windows user/admin key 未改变，旧 root key 不得再次使用或记录。
- 一次验收工具输出意外回显了 Windows admin user key；该 key 已立即轮换并验证旧值返回 401、新值返回 200，`ovcli.conf` ACL 仍仅允许 ASUS 与 SYSTEM。Studio 和已运行的客户端进程必须改用新 key。
- Windows 登录保活已在没有交互式 WSL 终端的条件下连续运行超过五分钟；任务保持 Running，`/health`、`/ready`、`/studio` 持续为 200，`/docs` 最终为 200。停止并重启任务后保活链和 health 均恢复；ISSUE-004 已解决。
- 已验证 OpenViking 0.4.11 在 1933 同源提供 Web Studio `/studio` 和 OpenAPI `/docs`；mirrored networking 工作时不需要 `netsh portproxy` 或局域网绑定。
- 本轮失败验收产生的专用 OpenViking Session、Codex 测试任务、插件状态和 transcript 已删除；长期记忆精确 grep 未发现本轮唯一标记。

## 下一步优先任务

1. 自然重启 Desktop，让长期运行的插件/MCP 子进程读取移除固定 `actor_peer_id` 后的配置；分别在 rtmpProject 和另一工作区核对派生 peer。
2. 在新的自然 Desktop 任务中完成唯一标记自动注入验收；只有后台任务 `completed`、`SessionStart`/`UserPromptSubmit` 注入日志和完全一致召回同时成立才关闭 ISSUE-003，随后清理专用标记。
3. 在真实 Linux ARM64 盒子完成发布级验收：使用 `scripts/qualify_embedded_device.sh` 按用户指定的路数阶梯/码流/门槛执行，生成设备档案与 `recommendedMaxStreams`；EGLFS 全屏、linuxfb、温度和长稳只能在真机验证。
4. 按 `cross_platform_build.md` §9 在真实 ARM 目标板完成 SRS 本机构建、systemd 与 LAN 推拉流验收；真实摄像头到货后按方案 Phase 3 复测编码兼容性（ISSUE-008）。

## 关键文档

- [跨会话记忆说明](README.md)
- [重要设计决策](decisions.md)
- [已知问题](known_issues.md)
- [项目路线](../roadmap/project_plan.md)
- [当前会话交接](../project_handoff.md)
- [跨平台构建](../guides/build-and-testing/cross_platform_build.md)
- [RTMP 链路验证](../guides/build-and-testing/rtmp_chain_verification.md)
- [SRS Server 接入实施方案](../srs_server_integration_plan.md)
- [OpenViking 使用与测试](../guides/development/openviking_usage_and_testing.md)
- [Week 6 OpenGL 验证](../weeks/week6/week6_opengl_environment_and_validation.md)
