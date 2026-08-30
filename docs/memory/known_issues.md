# RtmpMonitor 已知问题

只有可复现问题或明确未完成的验收项进入本文件。已验证事实与可能原因必须分开。

## ISSUE-001 Linux ARM64 真实设备尚未验收

- 状态：未解决
- 影响范围：Linux ARM64 发布、QPA、GPU、FFmpeg 播放和长期稳定性声明。
- 已验证现象：2026-08-03 主程序、测试、生产渲染库和 EGL/ES3 冒烟已通过 AArch64 交叉构建及依赖门禁；现有文档明确未在目标盒子完成真实运行。
- 复现步骤：当前没有已确定且可访问的目标盒子测试环境，无法执行发布级复现。
- 已排除内容：不是“是否能生成 AArch64 ELF”的问题，该门禁已有记录。
- 可能原因：待确认；厂商镜像、QPA 插件、GPU/VPU 驱动和 ABI 均可能形成设备侧差异。
- 下一步验证：确定硬件型号和系统镜像，按跨平台构建指南执行部署、QPA、一路/多路播放、重连和长稳测试。
- 临时规避：只声明“ARM64 交叉构建通过”，不声明“ARM64 实机支持已完成”。
- 相关文档和代码：`docs/versions/rtmp-v1/guides/build-and-testing/cross_platform_build.md`、`docs/versions/rtmp-v1/weeks/week6/week6_opengl_environment_and_validation.md`

## ISSUE-002 Windows 16 路长时性能资格测试

- 状态：已解决（2026-08-05）
- 影响范围：`--renderer=auto` 是否可成为发布默认值、性能发布结论。
- 已验证现象：16 路预录和双屏延迟 CPU/OpenGL 四组各 600 秒、20 秒预热与 Quality 门禁全部完成。OpenGL 16 路平均 CPU 1.50%（CPU 4.85%，降低 69.08%）、显示 14.91 FPS；双屏最差流 P95 196 ms、最大 317 ms；frame age/内部延迟、UI、队列、工作集、纹理和 8 例画质门槛全部通过。
- 历史复现方法：`docs/versions/rtmp-v1/weeks/week6/week6_renderer_performance_test_guide.md` 记录了当时的 A/B 流程；对应本机脚本已退役，若要在新硬件复测，应基于当前 CTest、指标和目标机器重新建立资格编排。
- 已排除内容：OpenGL 实际后端/fallback 误判、短窗口替代长测、纹理持续增长、颜色/stride 可见退化和源延迟缺样本均已排除。
- 解决方式：修正 15 FPS timer 的双重节流量化；对照流使用独立前缀；状态文件原子替换；纹理门禁允许计划内断流短暂释放，但要求不高于预热基线且末 60 秒完全恢复。CLI 默认切换为 `auto`，保留显式 CPU 回滚。
- 后续观察：在驱动、硬件、分辨率或部署环境变化后重新执行正式门禁；真实 ARM64 仍由 ISSUE-001 跟踪。2026-08-08 居中 16:9 网格的第一轮 120 秒预录 Video 快速对照通过，但它早于标题覆盖和 F11 监控墙。最终监控墙版本的同口径短测中，CPU、FPS、内部延迟、UI gap、队列、工作集和纹理门禁通过，latest frame age P95 为 CPU 47 ms、OpenGL 52 ms，超过相对限值 51.7 ms 0.3 ms，因此总控判定失败。该毫秒级差异需要正式长测判断，不能事后放宽门槛。2026-08-04 的四组 600 秒正式结果早于当前布局；当前布局需要新的完整发布认证时必须重新运行正式套件，不能用短测或旧数据替代。此前默认 Cover 只是已被替代的中间方案。
- 2026-08-04 恢复补充：Snapshot 的业务可见性修复已回归；同时根据用户录像定位并修复 `renderStateChanged + lambda + Qt::UniqueConnection` 被 Qt 6 拒绝、首帧不刷新主画布的问题。动态网格测试 29/29、完整 CTest 12/12 和四路无全屏实机验证均通过。
- 相关文档和代码：`README.md`、`docs/versions/rtmp-v1/architecture/video_rendering_framework.md`、`docs/versions/rtmp-v1/weeks/week4/week4_sixteen_stream_validation.md`

## ISSUE-003 跨会话 OpenViking 召回尚未验收

- 状态：未解决
- 影响范围：Codex 自动召回、自动捕获、Hook 和 MCP 的可信状态。
- 已验证现象：OpenViking 0.4.11、CPU embedding、OAuth、systemd 和 Windows 客户端已落地；MCP 已通过 wheel-only 从不兼容的 2.0.0 固定为 1.29.0。doctor、`/health`、`/ready`、Windows 回环访问、服务重启、user/root key 边界及临时 Session CRUD/清理均通过。官方 Marketplace 与 `openviking-memory` 0.7.4 已各安装一次；插件中恰有四个 Hook 和一个内置 MCP，真实 MCP 握手成功并枚举 18 个工具。CLI `/hooks` 显示四个 Hook 均为 `Installed=1`、`Active=1`。2026-07-30 已在真实 Codex CLI 输入 `/compact`，Hook 日志记录 `PreCompact(trigger=manual)`、真实 rollout 路径、`hasPeer=true` 和 commit accepted。2026-08-03 当前 Desktop 已实际暴露并成功调用 `search_experience`、`read_experience`；六个历史摘要 Session 均完成，常规 `search`/`recall`/`read` 能直接召回 Week 1～6、项目平台、模块、未完成事项和 Server 路线。
- 复现步骤：在共享 `CODEX_HOME` 的 CLI 执行最小 turn 和 `/compact`，等待 commit 任务终态；随后自然重启 Desktop，新建任务并检查精确经验工具及唯一标记自动召回。
- 已排除内容：Hook 信任、真实 PreCompact 触发、MCP 注册重复、精确经验工具缺失、直接项目记忆召回和 Server 基础健康已排除；仓库文档恢复不依赖 OpenViking，`AGENTS.md` 和本目录提供降级路径。
- 剩余缺口：尚未在移除固定 `actor_peer_id` 并自然重启 Desktop 后，新建一个不提供答案原文的任务，证明 `SessionStart`/`UserPromptSubmit` 自动注入、actor 隔离和跨任务唯一标记完全一致。直接 MCP 检索成功不能替代这项 Hook 验收。
- 下一步验证：自然重启 Desktop，在 rtmpProject 与另一工作区分别核对派生 peer；创建可删除的唯一标记测试，等待 commit `completed`，再在新的自然 Desktop 任务验证自动注入与完全一致召回，并依据 `memory_diff.json` 清理测试数据。
- 临时规避：新会话严格按 `AGENTS.md` 读取仓库文档。
- 相关文档和代码：`AGENTS.md`、`docs/memory/README.md`、[Codex Hooks](https://learn.chatgpt.com/docs/hooks)、[Desktop slash commands](https://learn.chatgpt.com/docs/reference/slash-commands)

## ISSUE-004 WSL 空闲退出会中断 Windows 到 1933 的可用性

- 状态：已解决（2026-07-30）
- 影响范围：Windows Codex 启动后首次自动召回，以及长时间没有 WSL 前台客户端时的 Server 连续可用性。
- 已验证现象：`OpenViking-rtmpProject-WSL-KeepAlive` 在没有交互式 WSL 终端的情况下保持唯一的 `sleep infinity` 保活链。连续空闲超过 5 分钟后，任务仍为 Running，发行版未停止，Windows `/health`、`/ready`、`/studio` 均持续为 HTTP 200，`/docs` 最终也为 200。随后停止并重新启动计划任务，保活链恢复，Windows health 为 200；`openviking.service` 为 active/running，`Restart=on-failure`。本轮结束前再次读取任务对象成功，状态仍为 Running。
- 复现步骤：确认 Windows health 成功，结束所有 WSL 客户端并等待发行版退出，再请求 `http://127.0.0.1:1933/health`。
- 已排除内容：不是 OpenViking systemd unit 未启用，也不是服务自身重启策略失效；显式 `systemctl restart` 已验证会产生新 PID 并恢复健康。
- 可能原因：原问题来自 WSL 发行版生命周期不由 systemd 服务单独保活；登录任务通过维持一个受控 WSL 客户端解决该生命周期缺口。
- 后续监测：电脑登录后检查任务 Running；若用户主动停止任务或脚本路径失效，则重新执行五分钟健康验收。
- 回滚/规避：删除登录任务并结束其保活进程可回滚；若任务失效可手工启动 `Ubuntu-22.04-New`。始终不把 1933 绑定到局域网地址。
- 相关文档和代码：`docs/project_handoff.md`、`docs/versions/rtmp-v1/guides/development/openviking_usage_and_testing.md`、`/etc/systemd/system/openviking.service`

## ISSUE-005 OpenViking MCP 代理在 Windows Node 24 关闭期触发断言

- 状态：未解决
- 影响范围：插件 MCP 子进程关闭时的退出码与诊断噪声；尚未观察到正常工具调用失败。
- 已验证现象：使用 Codex 内置 Node 24.14.0 启动官方 0.7.4 MCP 代理时，initialize 和 tools/list 成功，18 个工具均可枚举；stdin 关闭并尝试删除上游 MCP session 后，Node 在 Windows libuv `UV_HANDLE_CLOSING` 断言处异常退出。
- 复现步骤：启动插件 `servers/mcp-proxy.mjs`，完成 MCP initialize/tools-list，然后关闭 stdin。
- 已排除内容：不是 Server 不可达、API key 无效或 MCP 注册重复；握手、鉴权和工具枚举均已成功。
- 可能原因：待确认；现象位于官方代理调用 `process.exit` 与 Windows Node 24 异步句柄关闭阶段。
- 下一步验证：完成 Codex 桌面重启后观察真实 MCP 生命周期；检查后续 OpenViking 插件或 Node 运行时是否修复，再决定是否向上游报告。
- 临时规避：不手工注册第二份 MCP；正常运行期间保持代理长驻，退出期异常仅作为诊断风险记录。
- 相关文档和代码：官方插件 `servers/mcp-proxy.mjs`

## ISSUE-006 OpenViking 0.4.11 Codex 适配器补丁可能被升级覆盖

- 状态：已缓解，升级后需复验
- 影响范围：GPT-5.6 Luna 的推理 Token、延迟和成本控制，以及 OpenViking 重装/升级后的 VLM 行为。
- 已验证现象：0.4.11 的 VLM 运行类会生成 `reasoning_effort`，但 Server 配置 schema 拒绝 `vlm.reasoning_effort`；Codex Responses 适配器原实现又没有把该参数转换为 Responses 的 `reasoning.effort`。直接只换模型会使 GPT-5.6 使用后端默认推理级别。
- 复现步骤：在 `ov.conf` 增加 `vlm.reasoning_effort` 后重启，服务报 `Unknown config field 'vlm.reasoning_effort'`；查看原始 `codex_responses_adapter.py` 可见请求只包含 model、instructions、input、store 和 tools。
- 已排除内容：不是 Luna 模型不可用、OAuth 失效、代理故障或 embedding 故障；真实 Luna 请求和 Session commit 均已成功。
- 当前缓解：本机适配器只对 `gpt-5.6-*` 发送 `reasoning.effort=none`，不向 `ov.conf` 写入非法字段；原配置和适配器已在各自 WSL 目录旁备份。
- 下一步验证：每次 OpenViking wheel 重装或升级后检查补丁是否仍存在，重新运行 `py_compile`、doctor、最小 `store=false` 请求与可清理 Session commit；若上游正式支持该字段，删除本机补丁并改用官方配置。
- 回滚/规避：恢复备份的适配器与配置并重启服务；不得恢复已经泄露的旧 root key。
- 相关文档和代码：`docs/memory/decisions.md`、`docs/versions/rtmp-v1/guides/development/openviking_usage_and_testing.md`、WSL 安装内 `codex_responses_adapter.py`

## ISSUE-007 WSL 到 Codex backend 的代理 TLS 仍有间歇性 EOF

- 状态：未解决
- 影响范围：OpenViking 的文件摘要、Session commit 和记忆抽取；基础 `/health`、`/ready` 和本地存储不受影响。
- 已验证现象：2026-08-03 经既有 `127.0.0.1:7890` 代理对目标 endpoint 连续只读探测 10 次，3 次得到预期 HTTP 405，7 次为 `SSL unexpected eof while reading`。第一轮历史回填在第 4 个 Session 因同一 `httpx.ConnectError` 失败并完整回滚；第二轮在 Session 级精确回滚/重试保护下六份全部完成。
- 已排除内容：不是摘要脱敏规则、OpenViking Server 基础健康、embedding 或本地数据目录故障。按用户约束，本轮没有修改 Clash、代理规则、节点、订阅、端口、systemd 代理 drop-in 或防火墙。
- 下一步验证：由用户先确保当前代理核心稳定，再做连续 20～30 次只读 endpoint 探测；只有零 TLS 错误后，才将大批量 Resource/Session 任务视为稳定。OpenViking 侧继续要求任务终态和失败回滚。
- 临时规避：把批量写入拆成可审计的独立 Session，每份轮询到 `completed`；失败时根据对应 `memory_diff.json` 精确回滚后重试，不重复上传 Resource。
- 相关文档和代码：`docs/project_handoff.md`、`docs/versions/rtmp-v1/guides/development/openviking_usage_and_testing.md`、`/etc/systemd/system/openviking.service.d/proxy.conf`

## ISSUE-008 SRS ARM 实机、真实摄像头与部分恢复场景尚未验收

- 状态：未解决
- 影响范围：SRS Server 接入的最终验收（方案文档第 16 节）；不得据此宣称"Server 产品化完成"。
- 已验证现象：2026-08-09 独立复验重新通过 Windows Preset 全新配置、136/136 构建、CTest 17/17（85.29 秒），以及 SRS 6.0.184 的 1935/回环 1985、Windows/WSL 双侧推拉流、停推恢复、SIGQUIT 停止和未知 1935 占用拒绝。Kimi 的 FFmpeg 10 分钟、4/16 路 profile、SRS 崩溃恢复和 API Degraded 结果属于历史证据，本次没有重复执行，不扩大为新的独立门禁结论。SRS `/api/v1/streams/` 默认分页 count=10 已确认，多流查询必须显式 `?start=0&count=N`。
- 复现步骤：见 `docs/versions/rtmp-v1/guides/build-and-testing/srs_failure_recovery.md` §5 与 `docs/versions/rtmp-v1/weeks/week7/`。
- 已排除内容：当前 Visual Studio 配置失败、WSL2 SRS 基础端口/API、单路推拉流、配置解析和监控防抖已有本轮命令终态证据；ARM、真实摄像头与未重跑的长测仍不在排除范围。
- 剩余缺口（保持方案第 15 节 `[需要验证]`）：
  1. WSL2 mirrored networking 下真实摄像头从 LAN 访问 Windows 主机 1935 的入站路径，必须由外部设备验证。
  2. 目标 ARM 板本机构建、systemd 管理、LAN 推拉流和重启自恢复（`build_srs_arm64.sh`/`verify_srs_chain.sh`/systemd unit 已交付，交叉构建仅证明 AArch64 ELF）。
  3. 真实摄像头 H.264 profile/level、GOP、时间戳与现有播放器兼容性。
  4. 非零 `--max-reconnect-failures` 达上限后 Server 恢复是否自动 restartStream；默认无限重试已覆盖常规恢复，Healthy 上升沿逻辑未启用。
  5. `ossrs/srs:6.0.184` 镜像 arm64 manifest 与 Docker Desktop bind mount 转义（Docker 备选路径未在本轮启用）。
- 下一步验证：确定 ARM 目标板后按 `cross_platform_build.md` §9 执行实机验收；摄像头到货后按方案 Phase 3 复测。
- 临时规避：项目状态区分“本轮独立复验”“Kimi 历史证据”和“ARM/摄像头待验证”，不得合并表述为全面产品化通过。
- 相关文档和代码：`docs/versions/rtmp-v1/architecture/srs_server_integration_plan.md`、`docs/versions/rtmp-v1/guides/build-and-testing/srs_failure_recovery.md`、`deploy/srs/`、`scripts/srs/`、`docs/versions/rtmp-v1/weeks/week7/`

## ISSUE-009 Windows 单路全屏历史表面、控制栏与退出闪屏

- 状态：代码、自动化回归及 Debug 真实 SRS 流 `auto/cpu` 复验已修复；Visual Studio F5 + NVIDIA Overlay 现场录像仍待维护者确认。
- 影响范围：Windows 桌面 `TemporaryWindowCanvas` 单路全屏及 F11 监控墙；Linux/ARM EGLFS 单画布路径不执行 Win32 逻辑。
- 已验证现象：用户提供的 12.864 秒 ScreenSketch 录像与两张截图显示，Camera 01 退出后进入 Camera 02 时可能残留旋转 180° 的旧控制栏；无视频帧时控制栏在 2.5 秒后消失，屏幕只剩黑色背景，NVIDIA Alt+Z 通知可能同时出现。NVIDIA 通知属于外部覆盖层，不是本程序控件。
- 根因：Windows DWM 对全屏 OpenGL 顶层窗口与其他顶层窗口同时显示存在已知合成限制；原进入/退出顺序会短暂重叠主窗口和全屏窗口，且隐藏窗口保留上一路 Snapshot。无帧状态仍启动控制栏自动隐藏则进一步造成“完全黑屏”体验。有帧后整条控制栏被隐藏，但鼠标事件可能由 `VideoCanvasHost/QOpenGLWidget` 接收，窗口本身没有稳定的底部唤出区域；退出时先隐藏全屏顶层窗口则会短暂暴露桌面/Visual Studio 和尚未完成首绘的白色主窗口。Qt 官方建议全屏 OpenGL 窗口保留 `WS_BORDER`：[Qt for Windows - Fullscreen OpenGL Based Windows](https://doc.qt.io/qt-6/windows-issues.html#fullscreen-opengl-based-windows)。
- 解决方式：Windows 的单路全屏窗口和 F11 主窗口在 `WinIdChange`/全屏状态变化时为 HWND 保留 `WS_BORDER`；进入单路全屏前先隐藏主窗口。无帧时控制栏和光标固定可见；首帧后等待 1200ms，以 180ms `OutCubic` 向下收起；独立的底部 96px 透明热区负责唤出，离开热区和控制栏 250ms 后收起，`clearFrame()` 会立即取消动画并恢复固定显示。截图按钮与 `Ctrl+Shift+S` 直接抓取全屏画布 framebuffer，立即显示缩略图，使用线程池和 `QSaveFile` 异步原子保存 PNG。退出时以最后一帧（无帧则纯黑）形成 raster 冻结层，隐藏全屏 OpenGL 子画布后恢复主窗口；只有主网格 `surfacePresented()` 后才揭开，750ms 设安全超时。
- 自动化证据：2026-08-09 使用 `Qt-Debug --fresh` 完成配置，136/136 构建成功，完整 CTest 17/17（92.74 秒）通过。回归覆盖无帧常驻、首帧滑出、96px 热区滑入/离开防抖、动画中清帧、按钮与快捷键单次触发、PNG 异步唯一命名/像素/失败报告、无帧不落盘、raster 过渡与 `surfacePresented()` 门禁、750ms 超时、普通/最大化/F11 恢复、30 次重复往返、旧 Camera 清理及 Windows `WS_BORDER`。
- 2026-08-09 录像复验补充：第二轮两段 ScreenSketch 录像确认，无帧时底栏之外会在顶部出现一条旋转的控制栏历史表面；有帧截图后底栏可能不再收起，且光标长时间隐藏后无法通过画布上的普通移动可靠恢复。代码核对表明，控制栏原先直接以全屏窗口为父对象并动画到窗口边界外，DWM/QOpenGLWidget 组合会暴露越界 backing-store；底部每个 `MouseMove` 又会重启动画；光标恢复只覆盖少数父控件事件，实际事件常被 GL/CPU 子画布或 Toast 接收。
- 补充修复：控制栏改为底部 96px 热区的子控件，滑出过程始终受父容器裁剪，不再把 QWidget 表面移出全屏顶层窗口；同方向动画幂等，不因重复 `MouseMove` 从 0 重启。全屏临时画布启用 `WA_TransparentForMouseEvents`，并由全屏窗口统一处理其范围内的鼠标活动；底栏离开后独立执行 250ms 收起，光标使用独立 2000ms 空闲计时，任何画布/控制栏/Toast 路径的鼠标移动都会立即恢复。新建独立构建目录完成 136/136，CTest 17/17（98.01 秒）通过；原目录曾因用户正在运行的 F5 进程占用 EXE 返回 `LNK1168`，用户退出后已重新链接 `rtmp_monitor.exe` 1/1 成功，Visual Studio 当前构建目录已是新版本。
- 2026-08-15 首进几何/光标回归：用户录像暴露新的首会话问题。根因是先隐藏临时 `VideoCanvasHost`，又在隐藏画布尚未取得全屏布局尺寸时生成 Snapshot；第二次进入会沿用上一次布局后的尺寸，因此看似恢复。旧全屏会话的异步光标计时器也可能在下一会话重新设置 `BlankCursor`。修复后统一执行“显示画布 → 强制布局 → 按最终尺寸生成 Snapshot”，并在 Windows 原生全屏/DPI 布局后的下一事件循环用会话代次保护再次同步；进入前停止旧状态并显式设置 `ArrowCursor`，退出开始时先关闭 presentation、失效旧回调并复位指针状态。
- 2026-08-15 验证证据：Windows Debug 全量 CTest 29/29（124.13 秒），其中 UI smoke 15.87 秒、动态网格 18.74 秒；Windows Release 全构建、ARM64 RASTER/GLES3 增量交叉构建及依赖门禁通过，RASTER 主程序 NEEDED 无 Qt OpenGL/EGL/GLES。阿里云正常 MP4 经 FFmpeg H.264/AAC 推送到既有 WSL2 SRS，Windows Debug `--renderer auto` 首次进入和连续 10 次往返均为 1920×1080、画面按 Contain 正常铺开且每次进入光标立即可见；`--renderer cpu` 首次进入 A/B 同样通过。截图保存在忽略目录 `out/fullscreen-regression-20260815/`。
- 剩余验证：维护者在 Visual Studio F5、NVIDIA Overlay 开启的真实桌面会话中，继续复核无帧顶部零历史表面、截图后底栏收起、截图方向/颜色、停推/恢复以及 Esc/按钮入口。已完成的 Debug 真实流双击验证不替代上述环境组合；`--renderer=cpu` 仍只作 A/B。
- 临时规避：若现场仍复现，保留 `--renderer=cpu` 仅作 A/B 定位，不得作为最终方案；提交新录像并记录进入方式、Renderer、Overlay 状态及 Camera 顺序。
- 相关文档和代码：`src/common/ui/FullscreenVideoWindow.cpp`、`src/common/ui/MainWindow.cpp`、`src/common/ui/VideoCanvasHost.cpp`、`src/common/ui/VideoGridWidget.cpp`、`tests/VideoGridSmokeTest.cpp`、`tests/VideoGridDynamicTest.cpp`

## ISSUE-010 Windows Debug 退出时 CRT Heap Corruption

- 状态：根因已修复，Application Verifier 与自动化压力回归通过；Visual Studio F5 人工关闭复验待用户执行。
- 影响范围：使用 MSVC + Ninja 的 Windows 增量构建；不是 FFmpeg、OpenGL 或 CPU renderer 的运行时专属问题。
- 已验证现象：自动连接真实流后正常退出，MSVC Debug CRT 在释放 `FullscreenVideoWindow` 时报告 heap suffix 损坏；`--no-camera-autostart` 不出现。单路 FFmpeg 生命周期、16 路 `MultiStreamPlaybackManager` 生命周期和 FFmpeg Debug DLL 哈希检查均正常，说明退出检查只是发现更早发生的写坏。
- 首次非法写入证据：新增真实流 UI 关闭测试后，对其启用 Application Verifier Full Heaps。Verifier 在 CPU 数据行首次中断，损坏位置恰为 `FullscreenVideoWindow` 已分配块末尾；释放栈为 `FullscreenVideoWindow` deleting destructor → `MainWindow::~MainWindow` → 测试。同期 Ninja 依赖数据库显示旧 `MainWindow.cpp.obj` 为 `#deps 0`，对象文件早于已变化的 `FullscreenVideoWindow.h`。旧调用方按较小类尺寸分配对象，新构造函数按新布局写成员，形成确定的一字节后堆尾越界。
- 根因：该 Windows 环境的 `cl.exe /showIncludes` 输出为 UTF-8 中文，CMake 3.29 按活动 GBK 控制台代码页自动解码后把乱码前缀写入 `rules.ninja`。Ninja 因前缀不匹配静默丢失头文件依赖，头文件变化后未重编译调用方，造成 C++ 类布局 ABI 不一致。
- 修复方式：MSVC + Ninja 配置阶段用独立探针以 `ENCODING NONE` 保留 `/showIncludes` 原始字节，并把原始前缀设置为 CMake/Ninja 依赖前缀；探针失败时配置直接失败，禁止生成依赖不安全的构建。新增 `rtmp_monitor_live_ui_shutdown_test`，用 `RTMP_MONITOR_TEST_URL` 参数化 CPU/OpenGL，等待 Playing、首帧呈现和渲染指标后按生产顺序正常关闭。
- 修复后证据：`cmake --fresh --preset Qt-Debug` 成功；`rules.ninja` 前缀与编译器输出一致；`MainWindow.cpp.obj` 恢复为 347 条依赖并包含 `FullscreenVideoWindow.h`。全量 143/143 构建、CTest 18/18、Application Verifier Full Heaps 真实流 UI 复测、CPU/OpenGL UI 关闭 30/30、单路真实 FFmpeg 生命周期 30/30、16 路真实流管理器生命周期 10/10 全部通过。另建 MSVC AddressSanitizer `RelWithDebInfo` 目录，54/54 构建并在明确使用 MSVC Qt DLL 的环境中通过 CPU/OpenGL 真实流 UI 退出测试，无 ASan 报告。Verifier 设置已在复测后关闭。
- 剩余验证：用户在 Visual Studio `Qt-Debug` 删除缓存并重新配置后，以 `--renderer=auto` 和 `--renderer=cpu` 各正常关闭 30 次，确认不再出现 CRT 对话框、卡死或后台残留；该人工 F5 项为 `[需要验证]`。不以禁用 Debug Heap、禁用自动连接、CPU 模式或强制结束进程规避。
- 相关文档和代码：`CMakeLists.txt`、`tests/LiveUiShutdownTest.cpp`、`docs/memory/decisions.md`

## ISSUE-011 Windows GUI 的 `--version` 会留下模态窗口

- 状态：已解决（2026-08-11）。
- 影响范围：Windows 命令行版本检查、无人值守发布门禁以及依赖该命令的 Phase 2/3 操作手册。
- 已验证现象：Phase 2 进程收尾发现 `rtmp_monitor.exe --version` 仍在运行，窗口标题为 `RtmpMonitor`；命令行和父进程确认它来自 Agent 的版本检查，而不是用户手工启动。发送正常窗口关闭消息后进程退出。
- 根因：主程序属于 Windows GUI 子系统；`QCommandLineParser::process()` 在该环境下通过模态消息框处理内置 version 选项，需要人工点击，不能作为脚本门禁。
- 修复方式：在保持其余 `process()` 帮助/错误行为不变的前提下，先用无副作用 `parse()` 识别 version 选项，向 stdout 输出应用名和集中式版本后立即返回成功。CTest 新增 `rtmp_monitor_version_cli_test`，精确匹配 `RtmpMonitor 0.1.0-alpha.1` 并设置 5 秒 TIMEOUT。
- 修复后证据：修复后从全新 Release/Debug 配置完成 143/143 构建与 19/19 CTest；version 定向测试分别在 0.12/0.14 秒内通过，测试后无 `rtmp_monitor.exe` 残留。
- 相关文档和代码：`src/main.cpp`、`CMakeLists.txt`、`docs/roadmap/v0.1.0_alpha1_release_handoff_checklist.md`

## ISSUE-013 Windows Release CTest 子进程出现入口点加载失败

- 状态：已解决（2026-08-15）。
- 首次记录：2026-08-13。
- 现象：当前工作树的 Release 全目标构建成功，但从 `ctest` 启动时，多组互不相关的测试会在
  进入测试代码前以 `0xc0000139` 退出；抽查的 MQTT contract/client 与 render core Release
  测试 EXE 在收敛 PATH 后直接运行均为退出码 0。Debug CTest 25/25 稳定通过。
- 根因：Codex 桌面父进程同时带有大小写不同的 `PATH` 与 `Path`，其中一项仍指向 Conda/MinGW Qt。只修改 `$env:PATH` 或在 `cmd.exe` 中执行 `set PATH=...` 不能移除另一项，Windows Loader 因而混载不兼容 DLL。
- 修复与证据：测试进程启动前通过 `Environment.SetEnvironmentVariable` 同时清除 `PATH`/`Path`，再只设置 Release、MSVC Qt、vcpkg 与系统目录。2026-08-15 最终 Release CTest 29/29（98.14 秒）通过。打包脚本的 FFmpeg DLL 探测也采用同一进程级隔离，避免探测错误版本。
- 2026-08-30 Week 9 回归：`rtmp_monitor_webrtc_client_ice_config_test.exe` 的普通 shell 直启曾因 target 目录缺 `Qt6Test.dll` 而从环境误载 MinGW DLL并弹出 qExec/qTerminate 入口点错误；Debug GUI 测试也曾因输出目录缺平台插件而无法初始化 QPA。client contract target 现复制同配置 MSVC `Qt6Test`，全部 `QApplication` 测试复制同配置 Qt runtime 与 `qwindows`/`qoffscreen` 插件。清除 Qt 路径、PATH 只保留系统目录并设 offscreen 后，Debug/Release client contract 与受影响 GUI 测试均退出码 0；WebRTC OFF 的 GUI 测试也通过同一部署路径运行。
- 注意：这是当前开发宿主的环境污染，不是应用运行时状态；新增本机工具目录时仍应保证同一进程只有一个规范化 Path。
- 相关文件：`CMakeLists.txt`、`docs/project_handoff.md`、`scripts/package_windows.ps1`

## ISSUE-012 Windows 真实摄像头正式资格矩阵尚未执行

- 状态：待验证
- 首次记录：2026-08-12
- 现象：自动化源、schema v4、控制脚本和离线分析器已经实现；当前会话已实际访问 WSL2 SRS，并完成单路真实摄像头并排短测。短测达到采集/发布/解码/显示约 30 FPS、标记识别 100%、源延迟 P95 102 ms，但尚未执行 1/4/8 路各 600 秒正式矩阵，也未执行 16 路和合成 60 FPS 能力项。
- 风险：不能把工具构建成功、单元测试或历史 15 FPS/手机拍屏结果表述为 Windows 720p30 产品资格通过；当前性能优化方向仍须由新报告确定。
- 复现/验证：先按 `docs/versions/rtmp-v1/guides/testing/windows_camera_validation.md` 执行 `Check` 和 120 秒快速运行，再执行 `RunMatrix`。只有 1/4/8 三组正式报告全部满足门禁才能关闭本问题。
- 临时措施：Windows 产品目标已经改为 30 FPS，但发布说明继续明确“资格待验证”；ARM64 仍保持独立的真实板卡门禁。
- 2026-08-12 证据：首轮单路 120 秒运行因一次发布背压出现 2 个序号缺口并正确判失败；改用仍然有界的 8 帧节拍队列后，第二轮 120 秒通过，平均采集/发布/解码/显示为 30.000/29.967/30.038/29.963 FPS，零序号缺口、零源端丢帧，源延迟 P95 104 ms。该结果仍不是 600 秒正式资格。

## ISSUE-017 WebRTC V2 Week 9 现场摄像头门禁未完成

- 状态：未解决（只剩摄像头验收环境，非已知生产崩溃）。
- 影响范围：W9-CAM-01、W9-CAM-09 与 W9-GATE；不得宣称真实摄像头资格通过。
- 已验证现象：Media Foundation 生产路径、原生 H.264 优先、NV12 + 合成 `h264_mf` 回退、停止并发、四组同机 PeerConnection 隔离和故障回归已通过组件/定向测试。Week 10 代表性 720p30 runner 已完成四路 1,800 秒、全程逐路可归属指标、工作集、停止/重建和 cleanup，W9-RES-01 通过。物理摄像头仍未获显式授权，未枚举或打开。
- 复现/验证：Camera 资格必须由设备所有者显式给出 index，预热 20 秒并连续呈现 120 秒；只允许记录 camera-N 别名和脱敏结果。
- 临时措施：固定 `cameraQualified=false`、`physicalFourEndpointClaimed=false`，W9-GATE 记录 `blocked(camera_environment)`；不得用 MP4、fixture 或短测替代真实摄像头。
- 相关文件：`docs/versions/webrtc-v2/weeks/week09/`、`docs/versions/webrtc-v2/weeks/week10/`、`scripts/webrtc/qualify_week10.ps1`

## ISSUE-014 单向音频声学延迟与 ARM 真机门禁待执行

- 状态：Windows 受控软件链路与测试包已验证；声学硬件和 ARM 真机仍待验证。
- 影响范围：声卡 DAC/扬声器实际出声延迟、输出设备热插拔，以及 ARM V4L2/ALSA 真机音频。
- 已验证现象：真实 `MP4 → FFmpeg H.264/AAC → WSL2 SRS 6.0.184 → RtmpMonitor → QAudioSink` 连续通过三轮 320 秒及一轮 600 秒；600 秒为 647 样本、P50 89.561 ms、P95 111.632 ms、最大 125.396 ms、欠载 0。真实 GUI 全屏与最终 ZIP 解压后播放/静音/退出均无欠载。Windows Debug/Release 29/29；ARM64 RASTER/GLES3 全目标交叉构建和 QEMU 门禁通过，RASTER 无 GL/EGL/GLES。
- 未完成条件：当前无声卡回环线或已校准声学回环，不能测量 QAudioSink 之后的硬件实际出声；ARM V4L2/ALSA 真机和未知输出设备热插拔未提供。
- 复现/验证：具备回环条件后，在当前软件报告旁新增声学采集报告并连续执行三轮，每轮至少五分钟；ARM 必须在真实 V4L2/ALSA 设备上运行发布、播放和长期稳定性矩阵。
- 临时措施：可以声明“发布端到 QAudioSink 写入 P95 ≤150 ms 已通过”，不得简称为“声学端到端延迟已认证”或“ARM 真机已认证”。
- 相关文档和代码：`docs/versions/rtmp-v1/architecture/low_latency_audio_stream.md`、`scripts/audio/`、`scripts/package_windows.ps1`、`include/common/media/AudioPlaybackEngine.h`

## Issue 模板

## ISSUE-015 GitHub Beta 推送受本机代理与直连网络阻塞

- 状态：已解决（2026-08-23）
- 原现象：旧回环代理端口拒绝连接，临时直连 443 也重置或超时，导致 VS2026 基线只能保留在本地。
- 解决与证据：用户确认当前代理为 `127.0.0.1:7890`；仅对相关 Git 命令设置该代理后，
  `ls-remote`、`fetch origin Beta` 与非强制 `Beta:Beta` push 成功，远端从 `7b5185c` 快进到
  `c04ff50`。全局 Git 代理未修改，没有 force push。
- 保留边界：今后仍先核实远端、只做非强制快进；认证、冲突或远端改写时必须停止。

## ISSUE-016 OpenViking Session commit 被接受但记忆提取因过期 Codex OAuth 失败

- 状态：已解决（2026-08-23）
- 影响范围：Codex Hook 的 `Stop`/`PreCompact` 能追加消息并收到 commit accepted，但后台 Phase 2 无法生成可召回的 archive overview 和长期记忆。
- 根因：OpenViking 的 OAuth store 仍标记为外部管理，却指向已经删除的旧 WSL Codex auth 文件；其中 access token 已于 2026-08-07 过期，refresh token 随后持续返回 HTTP 401。Hook、OpenViking HTTP 服务、embedding 和本地消息写入均正常，因此只看 Hook 状态或 commit 计数会误判为写入成功。
- 修复：保留权限为 `600` 的旧 OAuth store 备份；通过 systemd drop-in 将服务持久绑定到 Windows 当前 `CODEX_HOME` 的 `auth.json`，再用 OpenViking 官方 bootstrap 重建 external-owner store 并重启服务。未记录或输出任何 token。
- 验证：修复后 auth 状态为 `external`、`expiring=false`，服务 `/health`、`/ready` 均通过；当前任务 transcript 已重放，新的 session commit 后台任务达到 `completed`，archive overview 非空。当前会话归档内对 `Visual Studio 2026`、`week4`、`600K`、`main.cpp` 的文件级命中分别为 9、19、3、37，证明近期任务原文已进入归档；服务重启后未再出现 OAuth 401。
- 恢复说明：整份长 transcript 重放时 WM 摘要曾提示超过模型上下文，但该提交的长期提取任务仍正常 `completed`，原始 archive 与近期标记均存在。常规 Hook 继续按阈值分段提交，不应把一次性恢复重放方式作为日常路径。
- 回滚/监测：如需回滚，移除 `codex-auth.conf` drop-in、恢复受保护的旧 store 并重启；但旧凭据已过期，回滚后提取会再次失败。Windows Codex 退出登录或认证文件被移动后，应先检查 bootstrap 路径、auth 有效期和 `/api/v1/tasks` 终态，不能只看 commit accepted。
- 相关文档和代码：`docs/project_handoff.md`、`docs/versions/rtmp-v1/guides/development/openviking_usage_and_testing.md`、`/etc/systemd/system/openviking.service.d/codex-auth.conf`

## ISSUE-XXX 标题

- 状态：
- 影响范围：
- 已验证现象：
- 复现步骤：
- 已排除内容：
- 可能原因：
- 下一步验证：
- 临时规避：
- 相关文档和代码：
