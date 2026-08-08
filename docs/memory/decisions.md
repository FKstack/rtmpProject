# RtmpMonitor 重要设计决策

本文件只记录会影响长期架构、平台、存储、安全或维护方式的决定。普通代码修改不写 ADR。

## ADR-001 仓库文档是项目记忆的权威来源

- 日期：2026-07-29
- 状态：已确认
- 背景：跨会话工作需要稳定、可审查且不依赖外部服务的项目上下文。
- 决策：使用 `AGENTS.md`、`docs/memory/`、handoff、roadmap、guides 和 weeks 形成分层记忆；OpenViking 只负责语义召回。
- 原因：代码和 Git 文档可以审查、比较和纠错；外部召回可能过期或不可用。
- 替代方案：只使用聊天记录或只使用向量记忆。
- 影响：新会话必须先读仓库事实；召回内容发生冲突时必须纠正。
- 验证证据：根目录 `AGENTS.md` 与 `docs/memory/README.md`。
- 相关文件：`AGENTS.md`、`docs/memory/README.md`

## ADR-002 Windows Codex 与 WSL2 OpenViking 分层部署

- 日期：2026-07-29
- 状态：已确认
- 背景：主要开发发生在 Windows 原生 Codex，而 WSL2 VHDX 已位于 G 盘，适合保存 Server 和数据库。
- 决策：Windows 只安装 Codex 插件、Hook 和 MCP 代理；WSL2 只运行 OpenViking Server。保留本机既有 `CODEX_HOME`，不迁移或复制现有 Codex Home；仓库不记录其个人绝对路径。
- 原因：WSL 插件无法捕获 Windows 会话；分层部署可避免重复 Hook/MCP，并满足主要持久数据不写 C 盘的要求。
- 替代方案：Windows 内嵌 Server；Windows 和 WSL 同时安装插件；将 Codex Home 迁移到 E 盘。
- 影响：Windows 通过本机 HTTP 访问 WSL 1933；真实配置和数据库不进入 Git。
- 验证证据：用户目录下 `.codex` 已验证为指向本机 `CODEX_HOME` 的 Junction；WSL VHDX 位于非系统盘；OpenViking 0.4.11、OAuth、systemd、Windows health/readiness、Marketplace 插件和内置 MCP 握手均已按分层方案验证。WSL 空闲生命周期与 Hook 持久审批作为独立已知问题跟踪。
- 相关文件：`docs/memory/README.md`

## ADR-003 使用工作区派生身份隔离项目记忆

- 日期：2026-07-29
- 状态：已采用；跨工作区运行时隔离仍需在 Desktop 自然重启后复验
- 背景：同一仓库的 Windows 路径与 WSL `/mnt/<drive>/...` 路径可能被插件识别为不同工作区。
- 决策：Windows 使用插件的工作区派生 peer，并将召回范围设置为 `actor`。未来如启用 WSL Codex，显式映射到已验证的 Windows peer，不创建第二份项目身份。
- 原因：固定全局 peer 会让其他仓库误写入本项目；工作区派生身份提供默认隔离。
- 替代方案：全局固定 `rtmp-project-personal`；为本项目维护专用 Codex 启动器。
- 影响：其他仓库拥有不同 peer；全局偏好与项目事实分层；安装后必须记录实际 peer。
- 验证证据：2026-08-03 已从 Windows `ovcli.conf` 精确删除固定 `actor_peer_id`，保留工作区派生开关和 `actor` 召回范围。插件同一派生函数验证本机仓库根目录得到 `E--rtmpProject`，另一工作区得到独立 peer；六个历史 Session 显式写入项目 peer并完成。长期运行的 Desktop 子进程仍需自然重启后做跨工作区运行时复验。
- 相关文件：`docs/memory/README.md`

## ADR-004 RTMP Server 产品化优先成熟伴随服务

- 日期：2026-07-29
- 状态：已确认
- 背景：产品最终需要可管理的 RTMP Server 能力，但从零实现 RTMP 会显著增加协议、安全和运维风险。
- 决策：推荐顺序为成熟伴随服务、成熟嵌入式组件、自行实现协议。首选评估随产品分发的 SRS；其他成熟 Server 子进程作为备选。
- 原因：伴随服务保留成熟协议实现和清晰进程边界，最容易建立健康检查、重启和回退。
- 替代方案：直接嵌入 Server 库；自行实现握手、chunk、流控制和会话管理。
- 影响：新增独立产品化专项；本次只修订规划，不实现 Server 业务代码。
- 验证证据：`docs/roadmap/project_plan.md` 的专项任务、比较和验收标准。
- 相关文件：`docs/roadmap/project_plan.md`

## ADR-005 OpenViking Hook 行为开关使用非敏感用户环境

- 日期：2026-07-30
- 状态：已确认
- 背景：Codex 的 `shell_environment_policy.set` 对受控命令有效，但实测不会进入已运行的生命周期 Hook 宿主；官方 Hook schema 也没有命令级 `env` 字段。
- 决策：仅将 `OPENVIKING_RECALL_COMPRESS=0`、`OPENVIKING_WORKSPACE_PEER=1` 和 `OPENVIKING_RECALL_PEER_SCOPE=actor` 写入 ASUS 用户环境。API Key、连接地址和配置路径仍只保存在权限受限的客户端配置中。
- 原因：三个开关不含凭据，且工作区 peer 仍由当前目录派生；用户环境能让新启动的 Desktop/CLI Hook 基础进程一致继承。
- 替代方案：修改 Marketplace 插件缓存中的 `hooks.json`；创建重复的用户级 Hook；把 API Key 写入环境；依赖只对 Shell 生效的环境策略。
- 影响：所有新启动的 Codex 进程都会关闭插件内置召回压缩并启用工作区 peer/actor 范围；其他仓库仍按各自工作目录派生 peer。插件升级不覆盖这些值。
- 验证证据：ASUS 用户注册表环境已从目标 SID 配置单元复验；使用同样环境直接运行官方 Hook 时，压缩器状态为 `configured_off`，捕获日志为 `hasPeer=true`。
- 相关文件：`docs/memory/known_issues.md`、`docs/project_handoff.md`

## ADR-006 使用 Windows 登录任务维持 WSL OpenViking 生命周期

- 日期：2026-07-30
- 状态：已确认
- 背景：本机 WSL 发行版在没有前台 WSL 客户端时可能退出；systemd unit 会在发行版运行时管理 Server，但不能单独阻止 WSL 被回收。
- 决策：使用当前用户登录任务 `OpenViking-rtmpProject-WSL-KeepAlive` 隐藏运行一个 `sleep infinity` WSL 客户端。任务只维持发行版生命周期，OpenViking 的启停、重启和日志仍由 systemd 管理。
- 原因：该方案不需要把 1933 绑定到局域网，不修改 WSL VHDX 位置，并可通过停止或删除任务独立回滚。
- 替代方案：每次手工启动 WSL；把 Server 作为 Windows 进程运行；修改全局 WSL idle 行为；将服务绑定到局域网并使用端口代理。
- 影响：用户登录后 WSL 会常驻并占用对应内存；任务等待本机 7890 后仍会启动 WSL，但不负责修改或修复代理应用。
- 验证证据：任务已注册为登录后延迟 30 秒、禁止重复实例、无执行时限和失败重启。在没有交互式 WSL 终端的条件下空闲超过 5 分钟后，任务仍为 Running，Windows `/health`、`/ready`、`/studio` 持续为 200，`/docs` 最终为 200；停止并重新启动任务后保活链和 health 恢复。
- 相关文件：`docs/guides/development/openviking_usage_and_testing.md`、`docs/memory/known_issues.md`

## ADR-007 OpenViking MCP 子进程显式继承客户端配置

- 日期：2026-07-30
- 状态：待 Desktop 重启验证
- 背景：Hook 子进程能够读取 Windows 客户端配置，但 Desktop 启动的插件 MCP 子进程未继承 `OPENVIKING_CLI_CONFIG_FILE`，日志显示认证 401；MCP 手工握手成功不代表 Desktop app-server 使用了同一环境。
- 决策：只在唯一的 `openviking-memory` 插件 `.mcp.json` 中为 MCP 子进程声明非敏感客户端配置路径、状态目录和召回行为变量；不复制 API Key，不创建第二份 MCP，不把真实配置提交仓库。
- 原因：MCP 和 Hook 是独立子进程，需要显式且一致地定位权限受限的 `ovcli.conf`；保持单插件、单 MCP 可避免重复注入和状态分叉。
- 替代方案：把 API Key 写入 `config.toml` 或用户环境；手工注册第二个 MCP；复制客户端配置到 Codex Home。
- 影响：Desktop 自然重启后应能从同一配置完成 MCP 鉴权并暴露 18 个工具，包括精确 `search_experience` 和 `read_experience`。插件升级可能覆盖缓存内 `.mcp.json`，升级后必须复查。
- 验证证据：CLI `codex mcp get openviking-memory --json` 已显示新增环境且仍只有一个 MCP；CLI 已发现并发起 `search_experience`。当前 Desktop app-server 仍缓存旧配置，精确工具尚未出现，不能提前标记为已确认。
- 相关文件：`docs/memory/known_issues.md`、`docs/guides/development/openviking_usage_and_testing.md`

## ADR-008 OpenViking Server 使用独立的本机代理环境

- 日期：2026-07-31
- 状态：已确认
- 背景：OpenViking 的 `openai-codex/gpt-5.4` 调用出现 `ConnectTimeout`；systemd 服务未继承 Windows 代理，而 WSL 经 `127.0.0.1:7890` 显式访问上游正常。
- 决策：在 `/etc/systemd/system/openviking.service.d/proxy.conf` 中只为 OpenViking 服务设置大小写两套 `HTTP_PROXY`、`HTTPS_PROXY`，并设置 `NO_PROXY=127.0.0.1,localhost,::1`。不修改 Clash for Windows 的节点、订阅、规则、端口或 LAN 设置。
- 原因：服务级环境能让 VLM 请求稳定走现有本机代理，同时保持 1933、health、Studio 和本地 MCP 走回环，不影响其他 WSL 进程。
- 替代方案：修改全局 WSL 代理；修改 Clash 规则；继续直连；把代理写入仓库或 `ov.conf`。
- 影响：Clash for Windows 的 7890 必须先可用；代理停止时 OpenViking 基础 health 仍可能正常，但 VLM 摘要和记忆抽取会失败。回滚时删除该 drop-in、执行 daemon-reload 并重启服务。
- 验证证据：systemd 新 PID 的 Environment 已包含六个代理变量；精确 Codex endpoint 20/20 得到预期 405，最大 0.83 秒；真实 9,373 字节 Resource 在 46 秒内完成摘要、overview、向量与关联 Session，journal 无新增 timeout。
- 相关文件：`docs/memory/known_issues.md`、`docs/project_handoff.md`、`docs/guides/development/openviking_usage_and_testing.md`

## ADR-009 OpenViking 后台 VLM 使用 GPT-5.6 Luna 并固定无推理模式

- 日期：2026-08-03
- 状态：已确认
- 背景：OpenViking 的摘要、Session commit 和记忆抽取属于高频结构化工作；`gpt-5.6-luna` 面向成本敏感和高吞吐负载。当前部署使用 Codex OAuth 后端而不是标准 API 计费端点，不能直接用公开 API 单价推导实际账单。
- 决策：将 OpenViking 0.4.11 的 VLM 从 `openai-codex/gpt-5.4` 切换为 `openai-codex/gpt-5.6-luna`，保留 OAuth、Responses API、服务专用代理、本地 embedding 和存储边界。由于 0.4.11 配置 schema 拒绝 `vlm.reasoning_effort`，在 Codex Responses 适配器中只对 `gpt-5.6-*` 明确发送 `reasoning.effort=none`。
- 原因：避免 GPT-5.6 未指定时采用更高推理级别而抵消低价/低延迟目标，同时不改变其他模型行为。
- 替代方案：继续使用 GPT-5.4；切换标准 OpenAI API Key；使用 GPT-5.6 Terra；接受 GPT-5.6 默认推理级别。
- 影响：本机 venv 内存在一处可被 OpenViking 重装或升级覆盖的兼容补丁；升级后必须复查适配器并重新运行 doctor、最小模型请求和可清理 Session commit。回滚备份位于 WSL 原配置/适配器旁，恢复后仍必须保留已经轮换的新 root key。
- 验证证据：doctor VLM PASS；真实 Luna `store=false` 请求成功；专用 Session commit `completed`，reasoning Token 为 0；测试 Session 与唯一标记已清理。
- 相关文件：`docs/memory/project_snapshot.md`、`docs/memory/known_issues.md`、`docs/guides/development/openviking_usage_and_testing.md`

## ADR-010 用脱敏 peer-only Session 回填历史任务

- 日期：2026-08-03
- 状态：已采用
- 背景：原始 Codex rollout 包含系统提示、工具输出、子代理记录、重复/续接任务和潜在敏感内容；OpenViking 原生全量 backfill 无法满足本项目的筛选、证据复核和脱敏要求。
- 决策：只处理 `thread_source=user` 且工作目录属于 rtmpProject 的顶层任务，按来源 ID 合并重复/续接任务；为每个逻辑任务创建一个稳定的 `rtmp-history-<date>-<source-id>` Session，只写入经代码、Git、测试或仓库文档复核的两条摘要消息。提交策略固定为 `self.enabled=false`、`peer.enabled=true`、`memory_types=[events,entities,preferences]`、`keep_recent_count=0`。
- 原因：保留可在 Studio 审计的来源与结论，同时避免重放原始 transcript、子代理内容或生成全局 identity/soul、experience、trajectory、case。
- 替代方案：OpenViking 原生全量 Codex backfill；直接把 rollout JSONL 当作 Resource；只维护仓库文档、不生成语义记忆。
- 影响：历史摘要是召回索引而非权威事实；仓库文档仍按 ADR-001 优先。任一 commit 失败必须依据该 Session 的 `memory_diff.json` 回滚，不能保留半完成批次。
- 验证证据：六个历史 Session 的任务均为 `completed`，均存在 `archive_001/messages.jsonl`、`memory_diff.json` 和 `.done`；召回已命中 Week 1～6、平台、模块、未完成验收和 RTMP Server 路线。27 个子代理会话未导入。
- 相关文件：`docs/memory/project_snapshot.md`、`docs/project_handoff.md`、`docs/guides/development/openviking_usage_and_testing.md`

## ADR 模板

## ADR-011 视频渲染采用单主 OpenGL 画布和临时全屏画布

- 日期：2026-08-03
- 状态：已采用；2026-08-05 默认切换为 `auto`
- 背景：逐路 QImage/QPainter 丢失 YUV 元数据、耦合解码与视口，并阻碍硬件帧演进。
- 决策：解码输出不可变 `VideoFrame` 到容量 1 邮箱；网格由一个 OpenGL 画布合成；全屏创建不共享 GLuint 的临时画布；GL 失败自动回退 CPU。
- 原因：将 Context/FBO 数量控制为正常 1 个、全屏短时 2 个，同时保持现有 StreamId、动态布局和故障隔离。
- 替代方案：每路一个 QOpenGLWidget；解码到 RGB QImage；共享 Context 上传线程。
- 影响：Windows 请求 GL 3.3 Core，嵌入式请求 ES 3.0；第一版不承诺 PBO、硬件帧、HDR 或 ES 2.0。
- 验证证据：Windows 四组 600 秒 CPU/OpenGL 正式门禁全部通过；16 路 OpenGL 平均 CPU 相对降低 69.08%、显示 14.91 FPS，双屏最差流 P95 196 ms/最大 317 ms，framebuffer 8 例质量通过。默认切换后完整 CTest 12/12 通过。ARM64 实机仍未完成。
- 相关文件：`docs/architecture/adr/001-video-rendering-architecture.md`、`docs/architecture/video_rendering_framework.md`

## ADR-012 主网格采用居中的标准 16:9 监控几何

- 日期：2026-08-08
- 状态：已采用
- 背景：原动态网格把每格拉成接近 4:1。`Contain` 会产生大量逐格左右黑边，临时改用 `Cover` 虽能填满，却会裁掉约一半上下内容，无法满足监控场景“标准 16:9 流必须完整、铺满且不变形”的要求。
- 决策：在网格几何层为所有格计算统一的最大 16:9 视频区，并把剩余窗口空间集中为整个网格外围留白。标准流默认与全屏均使用 `Contain`；异常比例默认完整留边；操作员仍可逐格手动选择 `Cover`；不提供 Stretch。事件日志默认隐藏但继续收集，可从“视图”菜单显示。
- 原因：对 16:9 源，只有让 viewport 同为 16:9，才能同时满足完整、铺满和无变形。任意源比例不可能普遍同时满足三项，因此异常比例明确优先完整与不变形。
- 替代方案：默认 Cover（丢失边缘内容）；Stretch（画面变形）；保持超宽格子并接受逐路黑边；按源比例生成不规则格子（破坏监控墙对齐和拖拽模型）。
- 影响：`VideoGridWidget` 负责纯几何计算和居中 margins；`VideoWidget` 报告 chrome 并限制最小视频区为 144×81；CPU/OpenGL 继续共用 placement，不改 Shader、邮箱、上传和指标 schema。长标题不再影响列宽。
- 验证证据：该阶段的 48 组纯几何用例、真实 16 格 viewport、4:3/竖屏、长标题、Dock 显隐、CPU/OpenGL 四边 framebuffer 回读通过；Windows Debug CTest 12/12（72.57 秒）通过。该阶段的 120 秒 Video 快速对照全部门禁通过，但它早于 ADR-013 的标题覆盖和监控墙最终实现，也不替代四组 600 秒正式认证。
- 相关文件：`src/common/ui/VideoGridWidget.cpp`、`src/common/ui/VideoWidget.cpp`、`src/common/ui/MainWindow.cpp`、`tests/VideoGridDynamicTest.cpp`、`tests/OpenGLGridRendererSmoke.cpp`

## ADR-013 普通紧凑网格与 F11 沉浸式监控墙

- 日期：2026-08-08
- 状态：已采用；最终布局的长时资格测试待重新执行
- 背景：ADR-012 已保证每路 16:9 视频完整、不变形，但普通窗口的标题栏、菜单、工具栏和状态栏会改变中央区域比例，因此 4×4 等尺寸网格仍会在整体左右留下空间。数学上，当可用区域比例与“列数÷行数×16:9”不同，不可能同时实现完整、无变形、无裁剪和绝对零留白。
- 决策：普通窗口把布局压缩为 4px 外边距/4px 格间距；F11 监控墙隐藏窗口与应用 chrome，并使用 0px/0px。几何算法不再要求视频宽高是 16 和 9 的整数倍，而是在整数像素约束下选择面积最大的近似 16:9 候选。设备名改为视频左上角半透明覆盖标签。监控墙临时强制有效模式为 `Contain`；Esc/F11 退出时恢复进入前的窗口、面板和每格显示模式。
- 原因：普通模式保留可操作性并尽量减少外围空间，监控墙利用完整 16:9 显示器达到接近零外围留白；两种模式都不牺牲源画面边缘，也不引入几何变形。
- 替代方案：默认 Cover（丢失监控边缘）；Stretch（人物和文字变形）；非等尺寸马赛克（破坏一致监控布局和拖拽模型）；滚动（不能同时看到 16 路）；自动进入监控墙（改变启动行为和窗口控制权）。
- 影响：`VideoWidget` 标题不再占独立布局行；`VideoGridWidget` 保存监控墙密度并为 Snapshot 应用有效 Contain；`MainWindow` 保存和恢复窗口 chrome。Shader、纹理、邮箱、解码和指标 schema 不变。
- 验证证据：最终 64 组纯几何用例覆盖 1～16 路和四种窗口尺寸，1920×1080 的 16 路监控墙左右外围留白不超过 8px/侧；覆盖标题、F11/Esc、日志 Dock、单路全屏往返和 CPU/OpenGL 的 16:9/4:3/竖屏四边 framebuffer 回读通过。撤回一次基于非 VS 启动窗口误判的临时改动后，VS/MSVC Debug 构建成功，CTest 12/12（74.57 秒）通过。用户确认 VS 生成的程序显示正常；自动化账户启动窗口不作为可比视觉证据。
- 性能边界：最终版本 120 秒快速对照中，OpenGL CPU 降低 91.66%、显示 14.914 FPS、实际后端与纹理证据有效；但 latest frame age P95 47→52 ms，超过 51.7 ms 相对门槛 0.3 ms，因此总控判定失败。重新执行正式套件前，不把 ADR-012 的较早短测或 2026-08-04 的 600 秒结果包装为当前布局认证。
- 相关文件：`src/common/ui/VideoGridWidget.cpp`、`src/common/ui/VideoWidget.cpp`、`src/common/ui/MainWindow.cpp`、`tests/VideoGridDynamicTest.cpp`、`tests/VideoGridSmokeTest.cpp`、`tests/OpenGLGridRendererSmoke.cpp`

## ADR-014 嵌入式渲染采用设备分级和保守资格策略

- 日期：2026-08-08
- 状态：双路径已于 2026-08-08 由 Kimi 实施完成（Windows CTest 14/14；ARM64 RASTER 与 GLES3 双构建、ELF/readelf 依赖验证通过；QEMU 纯逻辑测试除 2 个计时敏感日志用例外全过），真实板性能待验证
- 背景：Windows RTX 3060 已证明当前单画布 YUV OpenGL 路径在该设备上有显著 CPU 收益，但 ARM64 只有交叉构建证据。团队对低端盒子场景是否值得继续投入 OpenGL 存在争议；仅凭“Linux 6.1”无法判断 QPA、EGL/GLES、VPU、内存带宽、温度和驱动稳定性。
- 决策：保留 `VideoFrame + LatestFrameMailbox + RenderSnapshot + CPU/GL Canvas` 公共架构，并为 Linux ARM64 提供两种真实可裁剪构建：无 GPU/无可靠 EGL 的设备使用 `QImage + QPainter + Qt Raster Paint Engine + linuxfb`，应用不直接写 framebuffer；有 GPU 且实际 ES 3.0 Context、Shader、FBO smoke 通过的设备使用单画布 OpenGL ES 3.0。新增 `RASTER/GLES3/AUTO` CMake 模式，RASTER 构建不得查找或链接 Qt OpenGL、EGL、GLES。Linux bootstrap、能力策略和 backend factory 放入现有 `src/platform/linux/`。停止无目标设备的 PBO、共享 Context、上传线程和复杂 Shader 开发。
- 原因：低端设备的首要瓶颈可能是多路软件解码，GL 只能优化颜色转换、上传和合成。子码流和负载预算同时降低网络、解码、内存和显示成本，适用面更广；稳定 GLES3 设备仍能复用当前 OpenGL 的真实收益。
- 替代方案：所有平台默认 GL；删除 GL 退回逐路 QImage；把 Linux 6.1 视为统一图形平台；立即实现 GLES2、硬件解码或零拷贝。
- 影响：嵌入式 EGLFS 全屏应复用同一画布切换 Snapshot，不创建 Windows 式第二个 GL 顶层窗口。UI 技术上限仍为 16，但不再把 16 路作为任意 ARM 板的承诺或硬门槛；用户在目标板按自定义路数阶梯测试，结果写入 `recommendedMaxStreams`，超出时提示而不是静默硬拦截。当前“断开并移除”主链已存在，只补 Linux 两后端生命周期回归。硬件解码只在目标 SoC 冻结且实测证明必要后进入实施。
- 验证要求：Kimi K3 先完成 ARM64 RASTER 与 GLES3 两套交叉构建、AArch64 ELF 和依赖检查；RASTER 产物不得依赖 Qt OpenGL/OpenGLWidgets、EGL、GLES。WSL2 只证明构建和可运行的纯逻辑测试，不证明 linuxfb、EGLFS、GPU、VPU、温度或多路性能。真实板由用户选择路数、码流和门槛运行资格测试；GL 只有在该板的质量、延迟、温度和长稳门禁通过后才写入资格档案。
- 相关文件：`docs/architecture/embedded_device_rendering_strategy.md`、`docs/architecture/video_rendering_framework.md`、`src/common/ui/VideoCanvasHost.cpp`、`src/platform/linux/`、`CMakeLists.txt`

## ADR-XXX 标题

- 日期：
- 状态：已确认、待验证、已废弃
- 背景：
- 决策：
- 原因：
- 替代方案：
- 影响：
- 验证证据：
- 相关文件：
