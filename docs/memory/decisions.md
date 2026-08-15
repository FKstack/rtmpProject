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
- 状态：已确认；后续由 ADR-015 细化并落地 Windows+WSL2 最小 SRS 链路
- 背景：产品最终需要可管理的 RTMP Server 能力，但从零实现 RTMP 会显著增加协议、安全和运维风险。
- 决策：推荐顺序为成熟伴随服务、成熟嵌入式组件、自行实现协议。首选评估随产品分发的 SRS；其他成熟 Server 子进程作为备选。
- 原因：伴随服务保留成熟协议实现和清晰进程边界，最容易建立健康检查、重启和回退。
- 替代方案：直接嵌入 Server 库；自行实现握手、chunk、流控制和会话管理。
- 影响：该决策在 2026-07-29 只建立规划边界；后续实施状态以 ADR-015 为准。
  当前没有在 Qt 进程内实现 RTMP Server 协议，但已存在外部 SRS 配置、脚本、客户端
  URL/配置生成和只读健康监控。
- 验证证据：`docs/roadmap/project_plan.md` 的专项任务、比较和验收标准；ADR-015 的
  Windows+WSL2 独立复验记录。
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

## ADR-015 SRS 采用外部服务、稳定版本固定和只读客户端监控

- 日期：2026-08-08
- 状态：架构已确认；Windows+WSL2 最小链路已独立复验，ARM/摄像头待验证
- 背景：项目播放、解码、并发和 OpenGL 已完成，缺失的是成熟 RTMP Server 的部署与接入层。目标覆盖 Windows x64 开发和 ARM Linux 产品环境，同时禁止自研 RTMP Server、大改播放器或让平台部署逻辑侵入业务代码。
- 决策：固定 SRS 6.0.184（`v6.0-r0`）。Windows 开发首选 WSL2 Ubuntu 源码构建，Docker 固定镜像作为烟测/CI 备选，不采用 MSVC 原生假设或 Cygwin 正式基线；ARM Linux 首选目标机本机构建并由 systemd 管理。Qt 第一版不拥有 SRS 进程，只通过 `MediaServerEndpoint`、`RtmpUrlBuilder` 和异步 `MediaServerMonitor` 读取配置、生成 URL 和观察 1935/回环 HTTP API 健康。`FFmpegPlayer` 继续只接收 RTMP URL。第一版不启用 HTTP Callback。
- 原因：SRS 官方稳定构建和主要运行环境是 Linux/Docker；现有 WSL2 与 ARM Linux 路线可复用同一配置和运维模型。外部服务所有权避免 Qt 误杀未知进程，现有播放器自动重连已经覆盖 Server 短暂中断。同步 `on_publish` 若依赖桌面 GUI 会让摄像头推流反向依赖客户端可用性。
- 替代方案：Cygwin `srs.exe` 正式部署；Qt 直接启动/停止 WSL、Docker 或 systemd；新增统一管理摄像头/SRS/播放器的 `SrsManager`；第一版启用 `on_publish/on_unpublish` 自动建流。
- 影响：平台差异放在 WSL/Docker 脚本和 systemd unit；公共 C++ 只需 Qt Network 健康观察和 URL/config 纯逻辑。1935 冲突时只能识别复用或失败，不得杀未知 listener。动态发现或鉴权以后由常驻 control service 承接 Callback，不直接指向 GUI。
- 验证要求：独立验收必须使用全新/清空的 CMake 缓存和直接黑盒命令，不以实现者历史报告替代。2026-08-09 已重跑 Windows Debug CTest 17/17、单路真实 SRS 双侧推拉流、同 URL 恢复、SIGQUIT 和冲突拒绝；Kimi 的 4/16 路与 600 秒报告只作为历史证据。WSL LAN 入站、ARM 目标 ABI、官方镜像 arm64 manifest、真实摄像头编码仍 `[需要验证]`，见 ISSUE-008。
- 相关文件：`docs/architecture/srs_server_integration_plan.md`、`docs/weeks/week7/week7_srs_server_integration.md`、`deploy/srs/`、`scripts/srs/`、`include/common/server/`

## ADR-016 Windows 开发以 Visual Studio CMake Preset 和 F5 为标准入口

- 日期：2026-08-09
- 状态：已确认
- 背景：`Qt-Debug` 曾遗漏 vcpkg toolchain，Visual Studio 先报 FFmpeg 缺失，随后报 `build.ninja` 不存在；命令行或双击 EXE 还可能绕开 MSVC Qt 运行环境并误加载 MinGW Qt。
- 决策：公共 `CMakePresets.json` 提供隐藏 `Windows-MSVC-vcpkg`，只引用 `VCPKG_ROOT`；个人 Qt/vcpkg 绝对路径留在被忽略的 `CMakeUserPresets.json`。开发者选择 `Qt-Debug`、删除缓存并重新配置、将 `rtmp_monitor.exe` 设为启动项后按 F5。未执行 `windeployqt` 前不把双击 EXE 作为支持的启动方式。
- 原因：Visual Studio、MSVC、Qt Kit、vcpkg 和调试环境保持一致，且公共仓库不保存个人路径；CMake 缺少 toolchain 时在依赖探测前给出明确错误。
- 影响：命令行只作为 Developer PowerShell 构建/CTest 后备；GUI 与中文显示的最终视觉结论由用户的 Visual Studio 会话确认。
- 验证证据：`Qt-Debug --fresh` 指向正确 vcpkg，生成 `build.ninja`，136/136 构建和 CTest 17/17（85.29 秒）通过；无 toolchain 负向配置命中项目自定义诊断。
- 相关文件：`CMakePresets.json`、`CMakeLists.txt`、`README.md`、`docs/guides/build-and-testing/cross_platform_build.md`

## ADR-017 Windows OpenGL 全屏保留 DWM 合成边界并串行切换顶层窗口

- 日期：2026-08-09
- 状态：已采用；真实 Overlay 现场录像待确认
- 背景：Windows 无推流单路全屏在 Camera 切换时出现旋转的历史控制栏，随后控制栏自动隐藏形成完全黑屏；NVIDIA Alt+Z 外部覆盖层可能同时出现。Qt 官方记录了 Windows DWM 对全屏 OpenGL 窗口和其他顶层窗口的合成限制。
- 决策：Windows 桌面的单路全屏顶层窗口与 F11 主窗口为原生 HWND 保留 `WS_BORDER`；单路全屏进入前先隐藏主窗口，退出时先清空当前 Snapshot、解绑并隐藏全屏窗口，再恢复主窗口。无帧期间禁止控制栏自动隐藏。Linux/ARM EGLFS 继续使用既有单画布策略，不引入 Win32 依赖。
- 原因：该方案对应 Qt 官方 workaround，并从生命周期上消除两个 OpenGL 顶层窗口重叠和历史 Snapshot 暴露；无需改变 FFmpeg、renderer、Shader 或业务管理层。
- 替代方案：`WA_AlwaysStackOnTop`；强制 CPU renderer；重写全屏 renderer；关闭 NVIDIA Overlay。前者会破坏正常叠放，CPU 只能诊断，后两者扩大范围或把外部环境当作根治。
- 影响：Windows 全屏窗口存在不可见或接近不可见的系统合成边界；进入/退出的短暂桌面暴露风险由黑色全屏窗口改为串行切换控制。现场发布前必须在 Visual Studio F5 + 默认 OpenGL + Overlay 开启环境复验。
- 验证证据：Windows `Qt-Debug --fresh`、136/136 构建、CTest 17/17 通过；自动化验证两个 HWND 的 `WS_BORDER`、主窗口与单路全屏不同时可见、无帧控制栏和 Camera 历史清理。真实 F5 录像仍由 ISSUE-009 跟踪。
- 相关文件：`src/common/ui/FullscreenVideoWindow.cpp`、`src/common/ui/MainWindow.cpp`、`tests/VideoGridSmokeTest.cpp`、`tests/VideoGridDynamicTest.cpp`、`docs/memory/known_issues.md`

## ADR-018 单路全屏使用帧状态控制栏、画布截图与 raster 揭幕过渡

- 日期：2026-08-09
- 状态：已采用；真实流视觉验收待确认
- 背景：无帧控制栏必须常驻；有帧后需要在不遮挡画面的前提下可靠唤出。截图必须反映点击瞬间实际呈现的画布且不能阻塞 UI。原全屏退出顺序会在主画布首绘之前暴露桌面或白色 backing store。
- 决策：控制栏状态只由当前全屏 `VideoWidget::isFrameVisible()` 驱动：无帧固定可见，有帧经 1200ms 自动收起，由底部 96px 独立热区滑入，动画为 180ms `OutCubic`，离开防抖 250ms。桌面单路全屏截图由按钮或 `Ctrl+Shift+S` 进入同一入口，从 `VideoCanvasHost::grabFramebufferImage()` 捕获，先展示缩略图，再在线程池用 `QSaveFile` 原子保存 PNG。退出时全屏窗口用最后 framebuffer 构造 raster 冻结层，在其后方恢复主窗口，等主网格转发 `surfacePresented()` 后再隐藏过渡层；750ms 超时负责兜底。
- 原因：热区独立于 OpenGL 子控件的事件分发；framebuffer 截图天然不包含 QWidget 控制栏和 Toast；异步编码避免 UI 卡顿；raster 层避免两个 OpenGL 顶层窗口同时合成，也不暴露桌面和未首绘窗口。
- 替代方案：始终固定工具栏（遮挡有帧内容）；只监听顶层 mouse move（OpenGL 子控件会截获）；截图整个顶层窗口（会包含控件）；同步保存或弹对话框（阻塞交互）；用第二个 OpenGL 顶层窗口或 `WA_AlwaysStackOnTop` 过渡（增加 DWM/叠放风险）。
- 影响：截图范围仅为桌面单路全屏，默认保存到系统 Pictures 下的应用截图目录，失败不退出全屏；EGLFS 仍使用单画布路径，不创建第二顶层窗口。未修改 FFmpegPlayer、OpenGLGridRenderer、Shader、解码和 SRS。
- 验证证据：Windows `Qt-Debug --fresh` 配置、136/136 构建成功，完整 CTest 17/17（92.74 秒）通过；自动化已覆盖控制栏状态机、异步 PNG、像素、唯一命名、失败路径、过渡首绘门禁/超时及重复往返。真实 SRS 流下的方向、颜色、Overlay 与白闪视觉结论仍由 ISSUE-009 跟踪。
- 2026-08-09 补充约束：滑动控件必须位于底部热区的裁剪层级内，不得通过把 QWidget 移出全屏顶层窗口来隐藏；同方向动画请求必须幂等。全屏临时画布不承担鼠标交互并启用事件穿透，控制栏计时和光标空闲计时彼此独立；任意全屏子控件路径的鼠标活动都要立即恢复光标。独立 Windows Debug 构建 136/136、CTest 17/17（98.01 秒）通过，现场结果仍由 ISSUE-009 跟踪。
- 相关文件：`include/common/ui/FullscreenVideoWindow.h`、`src/common/ui/FullscreenVideoWindow.cpp`、`include/common/ui/VideoCanvasHost.h`、`src/common/ui/VideoCanvasHost.cpp`、`include/common/ui/VideoGridWidget.h`、`src/common/ui/VideoGridWidget.cpp`、`src/common/ui/MainWindow.cpp`、`tests/VideoGridSmokeTest.cpp`、`tests/VideoGridDynamicTest.cpp`

## ADR-019 MSVC Ninja 依赖前缀使用原始字节探测

- 日期：2026-08-09
- 状态：已采用
- 背景：Windows Debug 退出时的 CRT Heap Corruption 被 Application Verifier 定位为 `FullscreenVideoWindow` 类布局不一致。CMake 3.29 将 `cl.exe` 的 UTF-8 中文 `/showIncludes` 输出按 GBK 解码成乱码，Ninja 因前缀不匹配记录 `#deps 0`，头文件变化后调用方未重编译。
- 决策：仅在 MSVC + Ninja 下于配置阶段运行最小 `/showIncludes` 探针，以 `ENCODING NONE` 保留编译器原始输出字节，提取并覆盖 CMake/Ninja 依赖前缀；探针失败时配置直接失败。新增可选真实流桌面 UI 退出测试，同时覆盖 CPU 与 OpenGL。
- 原因：语言环境变量不能保证已安装编译器资源与 CMake 解码路径一致；原始字节与 Ninja 实际接收的编译器输出严格一致，且适用于英文、中文及其他本地化输出。失败关闭比静默生成无头文件依赖的构建更安全。
- 替代方案：只执行一次 clean build；固定中文或英文前缀；仅设置 `VSLANG`；禁用 Debug Heap；改变自动连接或强制 CPU renderer。前两者不能跨语言环境，`VSLANG` 在本机实测未改变探测结果，后几项只会掩盖 ABI 不一致。
- 影响：MSVC Ninja 配置多一次极小编译探针；Visual Studio Preset 与命令行增量构建都能正确跟踪头文件。未修改 FFmpegPlayer、renderer、解码、OpenGL 资源或运行时退出顺序。
- 验证证据：全新配置后 `rules.ninja` 前缀正确，`MainWindow.cpp.obj` 为 347 条有效依赖并包含 `FullscreenVideoWindow.h`；143/143 构建、CTest 18/18、Application Verifier Full Heaps 复测、真实流 UI CPU/OpenGL 30/30、单路播放器 30/30、16 路管理器 10/10 通过。独立 MSVC ASan RelWithDebInfo 目录完成 54/54 构建，CPU/OpenGL 真实流 UI 退出通过且无 ASan 报告。
- 相关文件：`CMakeLists.txt`、`tests/LiveUiShutdownTest.cpp`、`docs/memory/known_issues.md`

## ADR-020 Windows 测试包采用显式依赖来源与有版本门禁的 VC++ 前置

- 日期：2026-08-11
- 状态：已采用；项目授权和干净机验收待人工完成
- 背景：原安装规则只产生 `bin/rtmp_monitor.exe` 与 QSS，无法作为可分发包。
  `windeployqt --compiler-runtime` 又从 Qt 安装树复制了 VC++ 14.40 安装器，早于本轮
  MSVC 14.41；仅凭文件名无法证明 Qt、FFmpeg 或运行时来源正确。
- 决策：Windows 安装根直接放置 EXE 和运行资料；打包脚本按固定顺序执行 CMake
  install、MSVC `windeployqt`、vcpkg Release FFmpeg、授权/版本/来源清单、静态审计、
  ZIP 与 SHA-256。输出非空即拒绝覆盖。脚本从 `avutil_configuration()` 检查
  GPL/nonfree，从 CMake cache 锁定 Qt/vcpkg/MSVC 来源，并以哈希、Authenticode 和
  最低版本验证 `qwindows.dll`、FFmpeg 与微软 VC++ x64 前置安装器。运行时前置不由
  脚本静默安装，由干净机维护者确认签名后执行。
- 原因：候选包必须可追溯、可审计且不能因开发机 PATH 或旧 Qt 附带运行时产生假
  通过；中央 VC++ Redistributable 可由微软维护安全更新，也符合当前 Alpha 测试包
  的安装边界。
- 替代方案：复制整个开发 PATH；只运行 `windeployqt` 并接受过旧运行时；从
  System32 复制来源不明 DLL；静态链接全部依赖；在打包时静默安装系统组件。
- 影响：测试包包含微软签名的 x64 VC++ 前置安装器；干净机首次测试可能需要先安装
  它，不能再把包描述为绝对零前置的纯绿色包。项目自身采用保守的授权声明，最终
  授权选择仍由项目负责人复核。Linux 安装目录和运行时策略不受影响。
- 验证证据：独立目录重复打包成功；包内 31 个文件，PE 未解析依赖、禁止文件和
  敏感信息命中均为 0；ZIP 完整解压与 SHA-256 匹配；包内版本 CLI 通过；Release
  CTest 19/19（78.96 秒）通过。干净 Windows/VM 验收仍 `[需要验证]`。
- 相关文件：`CMakeLists.txt`、`scripts/package_windows.ps1`、`LICENSE`、
  `THIRD_PARTY_NOTICES`、`README_WINDOWS_TEST_PACKAGE.txt`、
  `docs/roadmap/v0.1.0_alpha1_release_handoff_checklist.md`

## ADR-021 源码交接使用 Git 与版本锁定脚本，不提交第三方库

- 日期：2026-08-11；2026-08-12 修订
- 状态：已采用；独立接收方复现待验证
- 背景：远端源码可以在维护者已配置的 MSVC/Qt/vcpkg 环境中构建，但全新 clone
  缺少受跟踪的 Windows 用户 Preset，既有环境脚本又含维护者盘符和代理假设。把
  Windows `.lib/.dll` 或 Linux `.so` 直接提交会放大平台、ABI、体积、许可证和安全
  更新风险。
- 决策：GitHub 分支与精确 commit 是唯一源码交接基准。源码仓库只提交参数化的
  Windows/Linux ARM64 开发入口、无个人路径的 Preset 示例及固定 vcpkg commit/
  FFmpeg 版本；不再生成或维护源码 ZIP。Windows 自动准备 vcpkg/FFmpeg，但只检查
  并指导安装 VS/Qt；Linux 仅支持 Ubuntu 22.04 宿主到 ARM64 的 RASTER/GLES3
  交叉开发。第三方运行库只进入构建目录或 Phase 3 Windows 分发包。
- 原因：脚本和版本锁既能让二次开发者重建依赖，又避免 Git 历史成为不可维护的二进制
  仓库；个人路径保留在被忽略的 `CMakeUserPresets.json`。
- 替代方案：提交第三方库；额外维护不含 Git 历史的源码 ZIP；自动安装 VS/Qt；支持
  任意 Linux 发行版/目标 ABI；把自动 clean-room 验收当作独立接收方验收。
- 影响：二次开发者仍需自行取得 VS 2022 和 Qt 6.6.1 MSVC；ARM 厂商 SDK、QPA、GPU
  和真机资格继续属于 Phase 5。Phase 4 自动化通过也不能勾选接收方人工复现。
- 验证证据：GitHub clean-room clone 的 Windows Release 143/143 构建与 CTest
  19/19（79.49 秒）通过；WSL2 ARM64 RASTER 124/124、GLES3 138/138 构建，
  FFmpeg 8.1.2/ELF/依赖及 QEMU 33 项纯逻辑断言通过。2026-08-12 又清理了维护者
  本机测试入口，并将 README 改为 clone 后运行统一脚本的公共入口；精简后的
  F 盘 clean-room Windows Debug 143/143、CTest 19/19（103.10 秒），G 盘 WSL
  RASTER 124/124、GLES3 138/138 和 QEMU 33 项断言再次通过。
  独立接收方复现仍 `[需要验证]`。
- 相关文件：`scripts/setup_windows_dev.ps1`、`scripts/setup_linux_arm64_dev.sh`、
  `CMakeUserPresets.example.json`、`README.md`

## ADR-022 Windows 摄像头资格测试采用机器标记权威链与视觉复核链

- 日期：2026-08-12
- 状态：已采用；真实 1/4/8 路 600 秒矩阵待执行
- 背景：历史 15 FPS 渲染对照和手机拍屏无法区分采集、编码、SRS、解码、邮箱覆盖与显示调度瓶颈，也不能提供稳定的端到端延迟真值。
- 决策：保留 32 位毫秒时间戳加 CRC8 的旧第一行，增加 32 位源帧序号加 CRC8 的第二行；摄像头源在预览和编码前写入同一帧。schema 升级为 v4，累计保存延迟/呈现间隔分位数、标记识别、序号缺口、调度超期和 requested/effective/achieved FPS。Windows `auto` 统一为 30 FPS，ARM64 `auto` 暂时保持 15 FPS。机器指标是权威结论，60 FPS 受控录像只作独立复核。
- 原因：序号可以识别重复、丢失和左右同源帧，机器时间可以测量端到端延迟；两条链职责分离后，录屏负载不会污染正式性能门禁，视觉证据也不能覆盖程序指标。
- 影响：新增不安装的 `rtmp_monitor_camera_source`、`rtmp_monitor_video_analyzer` 和 `scripts/camera_validation.ps1`；1/4/8 路 720p30 成为 Windows 最低资格线，16 路和 60 FPS 单列为能力项。
- 实测修订：`--validation-layout` 只隐藏 chrome、保持可移动窗口；控制器固定左侧拉流和右侧同帧参考。SRS 资格配置开启 `tcp_nodelay/min_latency` 并将 `mw_latency` 设为 0，防止服务端批量发送把 30 FPS 变成突发到达。
- 相关文件：`include/common/media/LatencyMarkerCodec.h`、`include/common/render/DisplayFrameRatePolicy.h`、`scripts/camera_validation.ps1`、`docs/guides/testing/windows_camera_validation.md`

## ADR-023 诊断层单向组合媒体与渲染，业务 façade 保持兼容

- 日期：2026-08-13
- 状态：已采用
- 背景：媒体管理器曾反向拉取渲染指标并同时负责 UI watchdog、schema v4 和文件写入；
  FFmpeg、连接、全屏、网格与入口类也分别混合多个层级或生命周期职责。
- 决策：依赖保持 `media -> render -> ui`，独立 diagnostics 层只读组合 media/render，
  且不得被二者反向依赖；以 façade 保持现有公共 API，内部按输入/解码、Registry/
  Reporter、Chrome/截图、几何/动画/Scene、Options/Bootstrap 拆分。CTest 静态扫描
  `media !-> render/ui` 和 `render !-> ui`。
- 原因：先切断跨层反向依赖，再缩小线程和 UI 生命周期责任，可以在不同时重写媒体与
  UI 主链的前提下提高单元可测性，并保持兼容行为。
- 替代方案：按文件行数机械拆类；一次性重写播放与 UI；让 media 直接依赖统一指标接口。
- 影响：新增若干小型实现类和 `rtmp_monitor_diagnostics` 库；组合根仍拥有完整应用生命周期，
  但 `main.cpp` 不再承载配置和事件桥接。新增边界必须遵守依赖门禁。
- 验证证据：Windows Debug CTest 21/21、Windows Release 全构建、ARM64 RASTER/GLES3
  交叉构建通过；RASTER ELF 无 Qt OpenGL/EGL/GLES 依赖。未执行正式性能或 ARM 真机资格。
- 相关文件：`docs/architecture/progressive_decoupling_architecture.md`、
  `cmake/CheckLayerDependencies.cmake`、`include/common/diagnostics/RuntimeMetricsReporter.h`

## ADR-024 使用全局架构 Skill 和项目级风险门禁约束代码变更

- 日期：2026-08-13
- 状态：已采用
- 背景：项目曾出现媒体、渲染、UI、诊断和应用编排职责集中在少数大类中的情况。仅靠任务提示和事后代码评审，不能稳定要求 AI 在实现局部需求前检查依赖方向、数据所有权、线程生命周期与公共契约。
- 决策：在当前 `CODEX_HOME` 安装通用 `$architect-code-changes` Skill，并在仓库根 `AGENTS.md` 对生产代码、公共头文件、CMake、线程、持久化、schema、CLI 和测试架构变更强制使用。Skill 使用 R0～R3 风险分级：R1 简化检查，R2 完整检查，R3 在依赖方向、公共契约或授权范围冲突时暂停确认。
- 原因：全局 Skill 复用架构推理流程，项目 `AGENTS.md` 只维护 RtmpMonitor 的具体依赖边界，避免规则复制和漂移；分级门禁能阻止新增耦合，同时避免小型局部修复被过度设计拖慢。
- 替代方案：把完整 Skill 复制进仓库；所有代码任务一律阻断确认；仅依靠类行数或静态扫描；只在重构专项中考虑架构。
- 影响：后续代码任务必须在编辑前给出对应级别的架构检查，并在交付中报告架构影响。此决定只改变 AI 开发流程，不改变产品代码、运行时依赖、CLI、schema 或 UI 行为。
- 验证证据：Skill 官方结构校验通过；安装文件与已校验暂存文件 SHA-256 一致；`AGENTS.md` 规则包含 R1/R2/R3、项目依赖方向、拆分要求和最终报告要求。
- 相关文件：`AGENTS.md`、`docs/architecture/progressive_decoupling_architecture.md`、`cmake/CheckLayerDependencies.cmake`

## ADR-025 保存推流与单目标 MQTT 控制采用独立边界

- 日期：2026-08-13
- 状态：已采用；ARM64 交叉构建已验证，实车联调待验证
- 背景：产品需要保存 RTMP 推流列表，并控制一台小车启停推流、四向移动和停车。若直接加入
  `MainWindow`、连接控制器或媒体管理器，会重新混合 UI、持久化、网络 session 与媒体生命周期。
- 决策：新增 `rtmp_monitor_profiles` 和 `rtmp_monitor_device_control`；前者拥有 schema v1 原子
  JSON，后者拥有 Paho MQTT C 1.3.16 异步 session、全局配置和命令 codec。应用 Controller
  编排用例，组合根创建具体对象。v1 固定单 Broker/Topic/设备，不发送 `deviceId`，使用 QoS 0、
  retain false 和大小写敏感的 `moveCar`。
- 原因：保存列表和 MQTT 具有独立变化原因、I/O、错误语义与生命周期；拆开后可用临时目录和
  Fake Broker 独立测试，不使媒体线程接触 QWidget 或设备网络。
- 替代方案：把 MQTT 塞进 `StreamConnectionController`；每个推流档案保存 MQTT Topic；使用
  Qt MQTT（当前 Qt 未安装）；发送两种大小写动作兼容。
- 影响：Windows/ARM64 增加 Paho 运行依赖；UI 增加保存列表入口和设备控制 Dock。断网自动停车
  仍依赖固件看门狗，本轮只提供桌面端松开、失焦、隐藏、退出和重连补停。
- 验证证据：Windows Debug CTest 25/25、Release 构建、ARM64 RASTER/GLES3 交叉构建和
  ELF 依赖检查通过；Fake Broker 已验证实际 CONNECT/CONNACK、`device/control` 发布、断开与
  重连。用户自行配置的现场 Broker 只验证过 CONNACK 0，未执行远端命令或实车动作；现场端点不得写入仓库。
  Windows 测试包按交付要求不计算/比对哈希、不生成 SHA-256 文件，仍保留依赖与签名审计。
- 相关文件：`docs/architecture/progressive_decoupling_architecture.md`、`CMakeLists.txt`、
  `cmake/CheckLayerDependencies.cmake`

## ADR-026 设备控制输入采用桌面双模式并隔离输入状态

- 日期：2026-08-14
- 状态：已采用；实车手感和固件失联看门狗仍待现场验证
- 背景：原四个方向按钮虽然能完成协议控制，但鼠标连续操作手感弱，也没有 Windows 用户熟悉的
  WASD/方向键路径。若把应用级键盘过滤、摇杆几何或按键集合直接加入 `MainWindow`、应用 Controller
  或 MQTT client，会再次混合 UI 输入、车辆状态和网络 session 生命周期。
- 决策：保持七类 MQTT JSON、四向/停车离散协议和 Controller 运动真值不变；在 UI 边界新增
  `VirtualJoystickWidget`，独立拥有鼠标捕获、20% 死区、四向量化、约 10° 迟滞和 120 ms 视觉
  回中；新增 `DeviceControlInputRouter`，独立拥有键盘模式、显式解锁、按键集合、最后按下优先和
  文本/模态快捷键作用域。两者只发出方向、释放或停车意图，由组合根接入现有 Controller。
- 原因：输入设备策略、车辆运动语义和 MQTT 网络生命周期是三个独立变化原因。分离后可在不连接
  Broker 的条件下测试鼠标边界与键盘状态机，并避免 Paho 回调或媒体层接触 QWidget 输入事件。
- 替代方案：继续使用方向按钮；在 `MainWindow::keyPressEvent` 中硬编码 WASD；把摇杆连续坐标加入
  payload；引入游戏输入或移动端摇杆库；第一版提供可配置改键。
- 影响：默认鼠标模式，键盘每次会话必须显式启用；Space 停车、Esc 解除，多键最后按下优先。
  MQTT 断开、失焦、隐藏、全屏、设置和退出会收敛输入状态。模式和解锁状态不持久化，不增加
  斜向、速度、设备回执、第三方依赖或协议字段。
- 验证证据：Windows Debug 独立目录 217/217 构建步骤（全目标）和 CTest 27/27、常用目录 CTest 27/27、
  Windows Release 全目标构建、ARM64 RASTER/GLES3 全目标交叉构建、AArch64 ELF/动态依赖门禁和
  QEMU 纯逻辑测试通过。实车手感和 Windows 多缩放人工视觉仍待现场验证。
- 相关文件：`include/common/ui/VirtualJoystickWidget.h`、
  `include/common/ui/DeviceControlInputRouter.h`、`docs/architecture/saved_stream_and_mqtt_device_control.md`

## ADR-027 应用 UI 采用深石墨 Qt Widgets 主题与原生平台外观

- 日期：2026-08-14
- 状态：已采用；Android 仍为未来设计约束，不属于本次实现
- 背景：主监控区使用浅色默认控件，设备控制区使用独立深色卡片，左右视觉层级割裂；同时产品未来
  可能移植到 Android，需要触控尺寸、可缩放图标和低分辨率可达性，但当前媒体和共享画布架构仍以
  Qt Widgets 为稳定实现。
- 决策：保留 Qt Widgets、原生窗口和 CPU/OpenGL 共享画布；应用统一使用 Fusion、深色 Palette 与
  限定作用域 QSS，产品标题为“RtmpMonitor 监控台”。设备控制 Dock 内部采用
  `QDockWidget → QScrollArea → DeviceControlPanel`；Windows 组合层通过 DWM 使用原生深色标题栏。
  应用图标提交确定性 SVG 源、QRC PNG 和 Windows 多尺寸 ICO，SVG 不作为运行时解码依赖。
- 原因：单一语义色板和稳定选择器能统一控件状态而不侵入媒体/渲染；原生标题栏保留系统行为；
  滚动容器和约 40 px 控件兼顾 1280×720 与未来触控；代码原生矢量资产易审查和跨平台派生。
- 替代方案：引入 QML 壳；自绘无边框窗口；仅给浅色主区换背景；运行时直接加载 SVG；本轮实现
  深浅主题切换或 Android 工程。
- 影响：`StyleLoader::applyApplicationStyle()`、`MainWindow::installDeviceControlPanel()`、现有 UI
  信号和配置/协议契约保持兼容。Windows 最终可执行目标新增系统 `dwmapi` 私有依赖；不新增第三方
  依赖，不改变 media/render/ui 编译方向、线程模型、数据所有权或视频画布所有权。
- 验证证据：Windows Debug 全目标构建与 CTest 27/27、Windows Release、ARM64 RASTER/GLES3
  全目标交叉构建及 ELF 动态依赖检查通过；Windows EXE 图标可提取，未导入 Qt6Svg；1280×720
  实际截图通过。多系统 DPI 人工矩阵仍保留为现场复验项。
- 相关文件：`resources/styles/app.qss`、`src/common/app/StyleLoader.cpp`、
  `src/common/ui/MainWindow.cpp`、`src/platform/windows/WindowsWindowAppearance.cpp`、
  `docs/guides/development/style_loading.md`

## ADR-028 RTMP 视频保留并以单例音频引擎提供 AAC 单向下行

- 日期：2026-08-14
- 状态：实现及 Windows 受控软件链路验证完成，真实声学延迟与 ARM 真机仍待硬件验证
- 背景：产品需要播放摄像头随 RTMP 视频发布的声音，同时保持现有多路视频、共享解码池、SRS、配置 schema 和 CLI 兼容。RTMP/FLV 的现有能力适合 AAC 下行，但不适合后续低延迟双向 Opus 对讲。
- 决策：输入会话识别必需 H.264 和可选 AAC；`MultiStreamPlaybackManager` 唯一持有 `AudioPlaybackEngine`，后者在专用 Qt 事件循环线程中执行 AAC 解码、`libswresample` 转 48 kHz mono S16 和 `QAudioSink` 输出。全应用一次只选择一个稳定 `StreamId`，启动默认无选择/静音，压缩包与 100 ms PCM 队列有界，Sink 欠载后以 40 ms 预缓冲恢复。同步优先使用 100 ms 内仍新鲜的实际呈现视频 PTS，画布交接时回退到同代次最新解码 PTS，并执行 +45/−125 ms 有限追赶。后续双向语音采用保留 RTMP 视频、独立 WebRTC/Opus 音频的阶段。
- 原因：单例输出避免 16 路混音和资源争用；独立音频线程避免视频峰值阻塞；可选音频的非致命状态维持视频故障隔离；有界队列优先实时性；FFmpeg 类型不泄漏到 UI/render，保持依赖方向。
- 替代方案：每路独立 `QAudioSink`；默认播放第一路；在视频解码 worker 中同步解码音频；以 RTMP/AAC 实现返向对讲；本阶段直接切换全部媒体到 WebRTC。
- 影响：media 新增 Qt Multimedia 和 `swresample` 私有依赖，Windows 包增加 Qt Multimedia 运行库/插件和 `swresample` DLL，ARM sysroot 增加 Qt Multimedia、ALSA 和 AAC/swresample 能力。现有 RTMP URL、保存推流 schema、metrics schema v4、CLI、视频接口和 MQTT 协议不变。
- 验证证据：2026-08-15 使用阿里云境内正常 MP4、FFmpeg、WSL2 SRS 6.0.184 和正式音频引擎连续通过三轮 320 秒及一轮 600 秒；600 秒为 647 样本、P50 89.561 ms、P95 111.632 ms、最大 125.396 ms、Sink 欠载 0 次。真实 GUI 全屏切换修复后欠载为 0，最终 ZIP 解压后也通过播放/静音/退出冒烟。Windows Debug/Release 29/29、ARM64 RASTER/GLES3 构建和门禁通过。该软件口径止于 QAudioSink 写入；声卡回环和 ARM V4L2/ALSA 真机未执行前不得宣称声学端到端或 ARM 资格通过。
- 相关文件：`docs/architecture/low_latency_audio_stream.md`、`include/common/media/AudioPlaybackEngine.h`、`scripts/audio/`

## ADR-029 默认网络功能离线并以单根提交重建泄露端点的发布历史

- 日期：2026-08-15
- 状态：已采用
- 背景：用户或现场公网端点曾被写成 MQTT 产品默认值并进入发布分支历史，使首次启动自动访问远端，
  同时把第三方网络位置暴露在源码、文档和已打包二进制中。仅修改最新文件不能清除可达 Git 历史。
- 决策：MQTT 首次默认禁用且 Broker 为空；真实端点只由用户输入并保存到本机 AppConfig。禁用允许
  空 Broker，启用或测试连接必须提供合法地址。`master` 与 `v0.1.0-alpha.1` 从同一脱敏最终 Tree
  创建无父单根提交，并在远程默认分支和保护规则可安全切换时替换旧可达引用。远程和全新克隆验证
  完成前，本地安全分支与 Bundle 必须保留且不得推送。
- 原因：离线默认值遵循最小暴露原则；本机配置保留用户控制权和现有兼容性；单根重建能从发布分支
  的可达对象中移除历史端点，而本地临时备份避免重写过程误删未提交项目成果。
- 替代方案：只修复 HEAD；在旧历史上追加删除提交；仅删除远程分支后重新推送旧祖先；把真实地址
  换成另一个公共演示地址。前三种仍保留泄露对象可达性，最后一种仍违反离线默认原则。
- 影响：配置 schema v1、Topic、设备控制协议、Paho 回调和线程生命周期不变；首次无配置的外部行为
  从自动连接改为离线。Git 提交身份整体重建，旧提交 SHA 不再属于 master/alpha；外部 Fork、他人
  克隆和已下载制品无法由本仓库强制召回。
- 验证证据：默认配置零连接、启用空 Broker 拒绝、本机有效回环配置及 Fake Broker 流程通过；
  Windows Debug 全目标构建与 CTest 29/29、Windows Release 全构建、ARM64 RASTER/GLES3 增量构建
  和依赖审计通过，RASTER 未引入 GL/EGL/GLES。脱敏根 Tree、打包目录和全新解压目录的旧现场
  端点命中均为 0；无配置 ZIP 冒烟 5 秒无外部 TCP 连接，已有本机配置在测试后恢复。远程全新克隆
  扫描按本次历史重建流程继续执行。
- 相关文件：`AGENTS.md`、`include/common/device_control/DeviceControlTypes.h`、
  `src/common/device_control/MqttSettingsRepository.cpp`、
  `docs/architecture/saved_stream_and_mqtt_device_control.md`

## ADR-030 设备状态使用独立 Topic 与本地单调时钟，控制目标绑定稳定 StreamId

- 日期：2026-08-15
- 状态：已采用
- 背景：预研设备每 15 秒在状态 Topic 上报带 `client_id` 的心跳，而启动推流需要把用户当前 RTMP
  URL 放入 `data.url`。Broker 会话状态不能代表设备在线，且现有车辆控制 payload 没有目标字段。
- 决策：单个 Paho session 同时订阅 `device/control` 和 `device/status`，两项 SUBACK 后才 Connected；
  业务层以本地单调时钟跟踪每个设备 ID，30 秒无心跳离线。RTMP URL 末段映射设备 ID，稳定
  StreamId 保存当前控制目标；`startStream` 使用该绑定 URL，观察 UI 对端点脱敏。状态缓存有界为
  64，Broker/状态 Topic 切换清 session，短暂重连按 30 秒规则自然过期。
- 原因：Paho 只拥有网络资源和回调代次，心跳业务规则可纯测试；稳定 StreamId 不受拖拽布局影响；
  单调时钟避免设备时间戳回退或电脑时钟校准造成误判；安全停止不应被心跳过期阻断。
- 替代方案：用设备 timestamp 与电脑时间比较；MQTT Connected 即设备 Online；在 Paho 回调中直接
  改 QWidget；为每张卡创建 Paho client；在现有消息中擅自添加 `client_id`。这些方案分别存在时钟、
  语义、线程、资源或协议兼容问题。
- 影响：MQTT 本机配置 schema 从 v1 升到 v2并提供只读迁移；`MqttConnectionOptions` 增加
  `statusTopic`，`MqttDeviceClient` 增加定向 start 发布入口。媒体、渲染、音频、保存推流 schema、
  CLI 和线程模型不变。由于车辆控制消息仍无目标字段，同一控制 Topic 只能部署一台受控设备。
- 验证证据：Windows Debug CTest 29/29、Release 全构建、ARM64 RASTER/GLES3 全目标交叉构建通过；
  Fake Broker 覆盖双订阅、拒绝、重连和状态 Topic，纯逻辑测试覆盖解析、30 秒边界、恢复和缓存上限。
- 相关文件：`include/common/device_control/`、`src/common/device_control/`、
  `src/common/app/DeviceControlController.cpp`、`src/common/app/StreamConnectionController.cpp`、
  `docs/architecture/saved_stream_and_mqtt_device_control.md`

## ADR-031 不改设备契约时先建设单车本地控制、事件与证据闭环

- 日期：2026-08-15
- 状态：已接受；Phase 1、Phase 2A 与模块三已实施
- 背景：长期移动安防路线优先需要命令 ACK、鉴权、多车、事件和证据，但当前约束明确禁止修改 MQTT
  控制/心跳返回、设备固件和硬件。现有客户端只能观察本地 publish、MQTT 会话、心跳、RTMP 和 SRS
  状态，不能证明设备执行结果，也不能可靠提供多车、遥测、地图或巡逻输入。
- 决策：当前试点限定单车、单桌面操作者和本地离线优先，按“控制安全/诚实审计 → 事件领域与最小
  截图 → 完整证据/导出 → 默认关闭的 SRS DVR PoC”分阶段实施。控制状态归纯策略，StopCar 绕过
  普通门禁；断连无法提交和 publish 失败分别产生 Critical 本地安全事件。事件只使用现有可观察
  信号，系统恢复不得由操作者伪造。EvidenceCatalog 是证据唯一事实源，事件 evidenceIds 为可重建
  投影；事件留存通过 tombstone 保证证据可解释。
- 原因：该范围能形成诚实、可审计、可逐步验收的单车闭环，同时不修改设备协议或媒体底座。纯策略、
  事件领域、证据 I/O 和 SRS callback 具有独立变化原因、状态、生命周期和测试接缝，不能继续塞入
  `DeviceControlController`、`MainWindow` 或 media/render。
- 替代方案：先做多车/地图 UI；用 QoS 0 publish 或心跳推断设备执行；把事件状态写入普通日志；让
  Qt 主进程承载 SRS HTTP callback；为事件 v1 立即引入 SQLite。它们分别会制造假能力、错误语义、
  不可查询业务状态、额外进程内协议生命周期或未验证的跨平台依赖。
- 影响：未来新增合法的 `app/composition -> control_policy/event_center/evidence` 依赖，media、render、
  device_control 和 server 不反向依赖新模块。MQTT payload、心跳、Topic/QoS、RTMP/FFmpeg、CLI 和
  现有 schema 不变。事件 v1 使用 Qt Core + QSaveFile；证据 I/O 有界且可停止；SRS DVR 默认关闭并
  使用独立回环适配器。ACK、多车、TLS/RBAC、遥测、地图、巡逻、SOS、AI、对讲和动态码率继续延期。
- 验证证据：设计依据当前源码、CMake 和 29 项 CTest 清单核对；产品经理会话完成两轮只读评审并
  明确“最终接受，可记录 ADR-031”。Phase 1 已于 2026-08-15 按 R2 落地：新增 Qt Core-only
  `control_policy`、应用 Transport 端口、真实呈现帧观察、共享移动解锁、首次失效停车/待补发和诚实
  审计；Windows Debug CTest 31/31、Windows Release、ARM64 RASTER/GLES3 构建及交叉依赖门禁通过。
  MQTT/RTMP/硬件契约未变，真实车辆动作和 ARM 真机显示仍待人工验收。Phase 2A 于 2026-08-16
  按 R2 落地：新增 Qt Core-only `event_center`、schema v1 原子 JSON、八类事实事件、应用只读观察桥、
  活动事件去重/恢复/复发/关闭和默认隐藏的事件 Dock/状态徽标；Windows Debug CTest 34/34
  （131.13 秒）、Windows Release、ARM64 RASTER/GLES3 全目标、AArch64 依赖审计及 QEMU 逻辑测试
  通过。模块三于 2026-08-16 按 R2 落地：新增 Qt Core + Qt Gui 的 `evidence` 边界、schema v1
  EvidenceCatalog、有界截图 I/O、事件详情、无有效画面登记、目录导出、启动一致性恢复，以及
  event schema v1 到 v2 的兼容迁移和证据投影。按产品决定不计算或比对 SHA-256 等内容哈希；仅验证
  原子提交顺序、规范路径和文件存在性，并在 UI/manifest 明确不提供防篡改保证。Windows Debug
  CTest 36/36、Windows Release、ARM64 RASTER/GLES3 全目标、AArch64 依赖审计和 QEMU 逻辑测试
  通过。SRS DVR 仍未实施。
- 相关文件：`docs/architecture/mobile_security_single_vehicle_operator_loop_design.md`、
  `docs/roadmap/mobile_security_product_module_recommendations.md`

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
