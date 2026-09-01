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
- 相关文件：`docs/versions/rtmp-v1/guides/development/openviking_usage_and_testing.md`、`docs/memory/known_issues.md`

## ADR-007 OpenViking MCP 子进程显式继承客户端配置

- 日期：2026-07-30
- 状态：待 Desktop 重启验证
- 背景：Hook 子进程能够读取 Windows 客户端配置，但 Desktop 启动的插件 MCP 子进程未继承 `OPENVIKING_CLI_CONFIG_FILE`，日志显示认证 401；MCP 手工握手成功不代表 Desktop app-server 使用了同一环境。
- 决策：只在唯一的 `openviking-memory` 插件 `.mcp.json` 中为 MCP 子进程声明非敏感客户端配置路径、状态目录和召回行为变量；不复制 API Key，不创建第二份 MCP，不把真实配置提交仓库。
- 原因：MCP 和 Hook 是独立子进程，需要显式且一致地定位权限受限的 `ovcli.conf`；保持单插件、单 MCP 可避免重复注入和状态分叉。
- 替代方案：把 API Key 写入 `config.toml` 或用户环境；手工注册第二个 MCP；复制客户端配置到 Codex Home。
- 影响：Desktop 自然重启后应能从同一配置完成 MCP 鉴权并暴露 18 个工具，包括精确 `search_experience` 和 `read_experience`。插件升级可能覆盖缓存内 `.mcp.json`，升级后必须复查。
- 验证证据：CLI `codex mcp get openviking-memory --json` 已显示新增环境且仍只有一个 MCP；CLI 已发现并发起 `search_experience`。当前 Desktop app-server 仍缓存旧配置，精确工具尚未出现，不能提前标记为已确认。
- 相关文件：`docs/memory/known_issues.md`、`docs/versions/rtmp-v1/guides/development/openviking_usage_and_testing.md`

## ADR-008 OpenViking Server 使用独立的本机代理环境

- 日期：2026-07-31
- 状态：已确认
- 背景：OpenViking 的 `openai-codex/gpt-5.4` 调用出现 `ConnectTimeout`；systemd 服务未继承 Windows 代理，而 WSL 经 `127.0.0.1:7890` 显式访问上游正常。
- 决策：在 `/etc/systemd/system/openviking.service.d/proxy.conf` 中只为 OpenViking 服务设置大小写两套 `HTTP_PROXY`、`HTTPS_PROXY`，并设置 `NO_PROXY=127.0.0.1,localhost,::1`。不修改 Clash for Windows 的节点、订阅、规则、端口或 LAN 设置。
- 原因：服务级环境能让 VLM 请求稳定走现有本机代理，同时保持 1933、health、Studio 和本地 MCP 走回环，不影响其他 WSL 进程。
- 替代方案：修改全局 WSL 代理；修改 Clash 规则；继续直连；把代理写入仓库或 `ov.conf`。
- 影响：Clash for Windows 的 7890 必须先可用；代理停止时 OpenViking 基础 health 仍可能正常，但 VLM 摘要和记忆抽取会失败。回滚时删除该 drop-in、执行 daemon-reload 并重启服务。
- 验证证据：systemd 新 PID 的 Environment 已包含六个代理变量；精确 Codex endpoint 20/20 得到预期 405，最大 0.83 秒；真实 9,373 字节 Resource 在 46 秒内完成摘要、overview、向量与关联 Session，journal 无新增 timeout。
- 相关文件：`docs/memory/known_issues.md`、`docs/project_handoff.md`、`docs/versions/rtmp-v1/guides/development/openviking_usage_and_testing.md`

## ADR-009 OpenViking 后台 VLM 使用 GPT-5.6 Luna 并固定无推理模式

- 日期：2026-08-03
- 状态：已确认
- 背景：OpenViking 的摘要、Session commit 和记忆抽取属于高频结构化工作；`gpt-5.6-luna` 面向成本敏感和高吞吐负载。当前部署使用 Codex OAuth 后端而不是标准 API 计费端点，不能直接用公开 API 单价推导实际账单。
- 决策：将 OpenViking 0.4.11 的 VLM 从 `openai-codex/gpt-5.4` 切换为 `openai-codex/gpt-5.6-luna`，保留 OAuth、Responses API、服务专用代理、本地 embedding 和存储边界。由于 0.4.11 配置 schema 拒绝 `vlm.reasoning_effort`，在 Codex Responses 适配器中只对 `gpt-5.6-*` 明确发送 `reasoning.effort=none`。
- 原因：避免 GPT-5.6 未指定时采用更高推理级别而抵消低价/低延迟目标，同时不改变其他模型行为。
- 替代方案：继续使用 GPT-5.4；切换标准 OpenAI API Key；使用 GPT-5.6 Terra；接受 GPT-5.6 默认推理级别。
- 影响：本机 venv 内存在一处可被 OpenViking 重装或升级覆盖的兼容补丁；升级后必须复查适配器并重新运行 doctor、最小模型请求和可清理 Session commit。回滚备份位于 WSL 原配置/适配器旁，恢复后仍必须保留已经轮换的新 root key。
- 验证证据：doctor VLM PASS；真实 Luna `store=false` 请求成功；专用 Session commit `completed`，reasoning Token 为 0；测试 Session 与唯一标记已清理。
- 相关文件：`docs/memory/project_snapshot.md`、`docs/memory/known_issues.md`、`docs/versions/rtmp-v1/guides/development/openviking_usage_and_testing.md`

## ADR-010 用脱敏 peer-only Session 回填历史任务

- 日期：2026-08-03
- 状态：已采用
- 背景：原始 Codex rollout 包含系统提示、工具输出、子代理记录、重复/续接任务和潜在敏感内容；OpenViking 原生全量 backfill 无法满足本项目的筛选、证据复核和脱敏要求。
- 决策：只处理 `thread_source=user` 且工作目录属于 rtmpProject 的顶层任务，按来源 ID 合并重复/续接任务；为每个逻辑任务创建一个稳定的 `rtmp-history-<date>-<source-id>` Session，只写入经代码、Git、测试或仓库文档复核的两条摘要消息。提交策略固定为 `self.enabled=false`、`peer.enabled=true`、`memory_types=[events,entities,preferences]`、`keep_recent_count=0`。
- 原因：保留可在 Studio 审计的来源与结论，同时避免重放原始 transcript、子代理内容或生成全局 identity/soul、experience、trajectory、case。
- 替代方案：OpenViking 原生全量 Codex backfill；直接把 rollout JSONL 当作 Resource；只维护仓库文档、不生成语义记忆。
- 影响：历史摘要是召回索引而非权威事实；仓库文档仍按 ADR-001 优先。任一 commit 失败必须依据该 Session 的 `memory_diff.json` 回滚，不能保留半完成批次。
- 验证证据：六个历史 Session 的任务均为 `completed`，均存在 `archive_001/messages.jsonl`、`memory_diff.json` 和 `.done`；召回已命中 Week 1～6、平台、模块、未完成验收和 RTMP Server 路线。27 个子代理会话未导入。
- 相关文件：`docs/memory/project_snapshot.md`、`docs/project_handoff.md`、`docs/versions/rtmp-v1/guides/development/openviking_usage_and_testing.md`

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
- 相关文件：`docs/versions/rtmp-v1/architecture/adr/001-video-rendering-architecture.md`、`docs/versions/rtmp-v1/architecture/video_rendering_framework.md`

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
- 相关文件：`docs/versions/rtmp-v1/architecture/embedded_device_rendering_strategy.md`、`docs/versions/rtmp-v1/architecture/video_rendering_framework.md`、`src/common/ui/VideoCanvasHost.cpp`、`src/platform/linux/`、`CMakeLists.txt`

## ADR-015 SRS 采用外部服务、稳定版本固定和只读客户端监控

- 日期：2026-08-08
- 状态：架构已确认；Windows+WSL2 最小链路已独立复验，ARM/摄像头待验证
- 背景：项目播放、解码、并发和 OpenGL 已完成，缺失的是成熟 RTMP Server 的部署与接入层。目标覆盖 Windows x64 开发和 ARM Linux 产品环境，同时禁止自研 RTMP Server、大改播放器或让平台部署逻辑侵入业务代码。
- 决策：固定 SRS 6.0.184（`v6.0-r0`）。Windows 开发首选 WSL2 Ubuntu 源码构建，Docker 固定镜像作为烟测/CI 备选，不采用 MSVC 原生假设或 Cygwin 正式基线；ARM Linux 首选目标机本机构建并由 systemd 管理。Qt 第一版不拥有 SRS 进程，只通过 `MediaServerEndpoint`、`RtmpUrlBuilder` 和异步 `MediaServerMonitor` 读取配置、生成 URL 和观察 1935/回环 HTTP API 健康。`FFmpegPlayer` 继续只接收 RTMP URL。第一版不启用 HTTP Callback。
- 原因：SRS 官方稳定构建和主要运行环境是 Linux/Docker；现有 WSL2 与 ARM Linux 路线可复用同一配置和运维模型。外部服务所有权避免 Qt 误杀未知进程，现有播放器自动重连已经覆盖 Server 短暂中断。同步 `on_publish` 若依赖桌面 GUI 会让摄像头推流反向依赖客户端可用性。
- 替代方案：Cygwin `srs.exe` 正式部署；Qt 直接启动/停止 WSL、Docker 或 systemd；新增统一管理摄像头/SRS/播放器的 `SrsManager`；第一版启用 `on_publish/on_unpublish` 自动建流。
- 影响：平台差异放在 WSL/Docker 脚本和 systemd unit；公共 C++ 只需 Qt Network 健康观察和 URL/config 纯逻辑。1935 冲突时只能识别复用或失败，不得杀未知 listener。动态发现或鉴权以后由常驻 control service 承接 Callback，不直接指向 GUI。
- 验证要求：独立验收必须使用全新/清空的 CMake 缓存和直接黑盒命令，不以实现者历史报告替代。2026-08-09 已重跑 Windows Debug CTest 17/17、单路真实 SRS 双侧推拉流、同 URL 恢复、SIGQUIT 和冲突拒绝；Kimi 的 4/16 路与 600 秒报告只作为历史证据。WSL LAN 入站、ARM 目标 ABI、官方镜像 arm64 manifest、真实摄像头编码仍 `[需要验证]`，见 ISSUE-008。
- 相关文件：`docs/versions/rtmp-v1/architecture/srs_server_integration_plan.md`、`docs/versions/rtmp-v1/weeks/week7/week7_srs_server_integration.md`、`deploy/srs/`、`scripts/srs/`、`include/common/server/`

## ADR-016 Windows 开发以 Visual Studio CMake Preset 和 F5 为标准入口

- 日期：2026-08-09
- 状态：已确认
- 背景：`Qt-Debug` 曾遗漏 vcpkg toolchain，Visual Studio 先报 FFmpeg 缺失，随后报 `build.ninja` 不存在；命令行或双击 EXE 还可能绕开 MSVC Qt 运行环境并误加载 MinGW Qt。
- 决策：公共 `CMakePresets.json` 提供隐藏 `Windows-MSVC-vcpkg`，只引用 `VCPKG_ROOT`；个人 Qt/vcpkg 绝对路径留在被忽略的 `CMakeUserPresets.json`。开发者选择 `Qt-Debug`、删除缓存并重新配置、将 `rtmp_monitor.exe` 设为启动项后按 F5。未执行 `windeployqt` 前不把双击 EXE 作为支持的启动方式。
- 原因：Visual Studio、MSVC、Qt Kit、vcpkg 和调试环境保持一致，且公共仓库不保存个人路径；CMake 缺少 toolchain 时在依赖探测前给出明确错误。
- 影响：命令行只作为 Developer PowerShell 构建/CTest 后备；GUI 与中文显示的最终视觉结论由用户的 Visual Studio 会话确认。
- 验证证据：`Qt-Debug --fresh` 指向正确 vcpkg，生成 `build.ninja`，136/136 构建和 CTest 17/17（85.29 秒）通过；无 toolchain 负向配置命中项目自定义诊断。
- 相关文件：`CMakePresets.json`、`CMakeLists.txt`、`README.md`、`docs/versions/rtmp-v1/guides/build-and-testing/cross_platform_build.md`

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
- 相关文件：`include/common/media/LatencyMarkerCodec.h`、`include/common/render/DisplayFrameRatePolicy.h`、`scripts/camera_validation.ps1`、`docs/versions/rtmp-v1/guides/testing/windows_camera_validation.md`

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
- 相关文件：`docs/versions/rtmp-v1/architecture/progressive_decoupling_architecture.md`、
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
- 相关文件：`AGENTS.md`、`docs/versions/rtmp-v1/architecture/progressive_decoupling_architecture.md`、`cmake/CheckLayerDependencies.cmake`

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
- 相关文件：`docs/versions/rtmp-v1/architecture/progressive_decoupling_architecture.md`、`CMakeLists.txt`、
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
  `include/common/ui/DeviceControlInputRouter.h`、`docs/versions/rtmp-v1/architecture/saved_stream_and_mqtt_device_control.md`

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
  `docs/versions/rtmp-v1/guides/development/style_loading.md`

## ADR-028 RTMP 视频保留并以单例音频引擎提供 AAC 单向下行

- 日期：2026-08-14
- 状态：实现及 Windows 受控软件链路验证完成，真实声学延迟与 ARM 真机仍待硬件验证
- 背景：产品需要播放摄像头随 RTMP 视频发布的声音，同时保持现有多路视频、共享解码池、SRS、配置 schema 和 CLI 兼容。RTMP/FLV 的现有能力适合 AAC 下行，但不适合后续低延迟双向 Opus 对讲。
- 决策：输入会话识别必需 H.264 和可选 AAC；`MultiStreamPlaybackManager` 唯一持有 `AudioPlaybackEngine`，后者在专用 Qt 事件循环线程中执行 AAC 解码、`libswresample` 转 48 kHz mono S16 和 `QAudioSink` 输出。全应用一次只选择一个稳定 `StreamId`，启动默认无选择/静音，压缩包与 100 ms PCM 队列有界，Sink 欠载后以 40 ms 预缓冲恢复。同步优先使用 100 ms 内仍新鲜的实际呈现视频 PTS，画布交接时回退到同代次最新解码 PTS，并执行 +45/−125 ms 有限追赶。后续双向语音采用保留 RTMP 视频、独立 WebRTC/Opus 音频的阶段。
- 原因：单例输出避免 16 路混音和资源争用；独立音频线程避免视频峰值阻塞；可选音频的非致命状态维持视频故障隔离；有界队列优先实时性；FFmpeg 类型不泄漏到 UI/render，保持依赖方向。
- 替代方案：每路独立 `QAudioSink`；默认播放第一路；在视频解码 worker 中同步解码音频；以 RTMP/AAC 实现返向对讲；本阶段直接切换全部媒体到 WebRTC。
- 影响：media 新增 Qt Multimedia 和 `swresample` 私有依赖，Windows 包增加 Qt Multimedia 运行库/插件和 `swresample` DLL，ARM sysroot 增加 Qt Multimedia、ALSA 和 AAC/swresample 能力。现有 RTMP URL、保存推流 schema、metrics schema v4、CLI、视频接口和 MQTT 协议不变。
- 验证证据：2026-08-15 使用阿里云境内正常 MP4、FFmpeg、WSL2 SRS 6.0.184 和正式音频引擎连续通过三轮 320 秒及一轮 600 秒；600 秒为 647 样本、P50 89.561 ms、P95 111.632 ms、最大 125.396 ms、Sink 欠载 0 次。真实 GUI 全屏切换修复后欠载为 0，最终 ZIP 解压后也通过播放/静音/退出冒烟。Windows Debug/Release 29/29、ARM64 RASTER/GLES3 构建和门禁通过。该软件口径止于 QAudioSink 写入；声卡回环和 ARM V4L2/ALSA 真机未执行前不得宣称声学端到端或 ARM 资格通过。
- 相关文件：`docs/versions/rtmp-v1/architecture/low_latency_audio_stream.md`、`include/common/media/AudioPlaybackEngine.h`、`scripts/audio/`

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
  `docs/versions/rtmp-v1/architecture/saved_stream_and_mqtt_device_control.md`

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
  `docs/versions/rtmp-v1/architecture/saved_stream_and_mqtt_device_control.md`

## ADR-031 不改设备契约时先建设单车本地控制、事件与证据闭环

- 日期：2026-08-15
- 状态：已接受；Phase 1、Phase 2A、模块三与 Phase 4 PoC 已实施
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
  通过。Phase 4 于 2026-08-16 按 R2 落地为默认关闭的独立 PoC：新增未被正常配置加载的 SRS DVR
  模板、仅回环 HTTP 适配器、schema v1 原子收据和故障矩阵验证器；SRS 拥有录像，适配器拥有 spool，
  Qt/EvidenceService 均不接入。收据只以相对路径、大小和 mtime 纳秒去重，不读取内容、不执行内容
  哈希并明确不提供防篡改保证。Python 单元测试 14/14 与固定 SRS 6.0.184 端到端矩阵通过，包含分段、
  关键帧、尾段、重复/乱序回调、分别重启、低磁盘、适配器离线推拉流继续及默认配置零副作用；
  Windows Debug CTest 36/36、Release 全目标和 ARM64 RASTER/GLES3 构建/依赖门禁保持通过。
- 相关文件：`docs/versions/rtmp-v1/architecture/mobile_security_single_vehicle_operator_loop_design.md`、
  `docs/roadmap/mobile_security_product_module_recommendations.md`

## ADR-032 WebRTC Week 2 采用默认关闭的开发者隔离边界与一次性 schema v1

- 日期：2026-08-20
- 状态：已采用；产品门禁等待人工复核
- 背景：Week 2 需要验证 libdatachannel、non-trickle 手工信令、文件安全和 H.264 handler API，
  但不得提前改变稳定 RTMP 产品、Week 3 Transport 公共契约或产品网络默认行为。
- 决策：新增默认 OFF 的 `RTMP_MONITOR_ENABLE_WEBRTC`；只有 ON 才创建 Qt Core signaling、
  libdatachannel probe core、developer CLI 和测试，依赖固定为 `probe/test -> probe_core ->
  signaling/libdatachannel`。schema v1 严格七字段并只存于仓库忽略的固定交换目录，使用原子提交、
  当前用户专用 DACL、10 分钟过期和非递归受管清理。PeerConnection 只建 DataChannel，显式空 ICE
  server；日志使用字段允许列表。
- 原因：schema/文件安全、异步协议生命周期和产品媒体链具有不同变化原因。隔离 target 能让 OFF
  构建不发现、不链接、不部署 libdatachannel，同时为 Week 3 前提供真实 API 和生命周期证据。
- 替代方案：无条件把 libdatachannel 链入主程序；把 probe 放入 media/ui；使用任意 CLI 文件路径；
  接入 Track 或预建 Transport/MediaSource；使用独立 candidate 文件或长期信令服务。这些方案会污染
  稳定产品边界、扩大敏感数据面或提前冻结 Week 3 契约。
- 影响：新增的外部接口只有 CMake 开关、developer CLI 和一次性 schema v1。RTMP、MQTT、保存流、
  UI、媒体解码和安装包契约不变；WebRTC 静态库、probe/test 和运行 DLL 均在 ON 专用输出目录。
  `W2-GATE` 在用户完成周末人工 Offer/Answer 与隐私复核前保持阻塞。
- 验证证据：Windows Debug OFF/ON 全目标构建通过，CTest 分别 37/37 与 39/39；ON 缺依赖配置按
  预期失败；额外 200 个 host-candidate loopback 连接/重复关闭周期、Windows DACL 结构检查、H.264
  离线样本和 probe 输出敏感模式扫描通过。
- 相关文件：`CMakeLists.txt`、`include/common/webrtc_dev/`、`src/common/webrtc_dev/`、
  `src/tools/WebRtcProbeMain.cpp`、`docs/versions/webrtc-v2/weeks/week02/`

## ADR-033 WebRTC 媒体阶段采用同一双角色客户端和兄弟模块 H.264 契约

- 日期：2026-08-21
- 状态：R3 方向已确认；Week 3 契约/解码边界已实施并通过自动技术门禁
- 背景：原 P2P 路线把独立参考发布器固定为 Offerer/send-only、正式客户端固定为
  Answerer/recv-only，并计划在 Week 3 提前引入 `MediaSource`、`PeerSource` 和 profiles→transport
  依赖。实际代码中的 RTMP URL、StreamId、设备身份、事件、控制和保存档案已有稳定且不同的语义，
  直接统一会扩大迁移面；社团测试又要求两台电脑能使用同一客户端版本。
- 决策：未来建立同一 `rtmp_monitor_webrtc_client`，分别选择 publisher/viewer 和
  Offerer/Answerer，第一阶段只实现 SendOnly/ReceiveOnly。transport、publisher source 和 media
  是兄弟模块，只共享协议无关的 H.264 AU/source/sink 契约并由组合根装配；transport 与 media 互不
  依赖，profiles 不依赖 transport。Week 3 不创建 MediaSource/PeerSource/schema v2；正式客户端
  后续只接入一次性 WebRTC session，原 RTMP façade 和保存流 schema v1 保持不变。
- 原因：信令发起权与媒体方向是不同变化原因；媒体源、PeerConnection 和 FFmpeg decoder 也拥有
  独立线程、状态和停止终点。兄弟模块和同一测试组合根既能覆盖两种 Offer 角色，又避免把人工会话
  的 sessionId 虚构成长期设备或 peer 身份。
- 替代方案：继续维护两个 executable；让 media 依赖 transport；让 profiles 保存 peer ID；立即迁移
  所有 RTMP 调用到统一 variant；预建 SendReceive。它们分别降低测试对称性、制造反向耦合、混淆
  身份或为未出现的用例冻结空契约。
- 影响：Week 3 已按该决定新增纯 C++ H.264/session 契约，把 decoder/有界压缩队列/worker affinity/
  mailbox 迁到 media-owned `EncodedVideoDecodeSession`，并由 manager 创建 move-only generation
  handle。`FFmpegPlayer` 保留 RTMP 网络、重连、AAC 和 signal façade。停止顺序固定为停止输入、
  generation 失效、worker 汇合、队列/decoder/mailbox 清理；未来 Track/PC 仍遵循先失效再关闭。
  WebRTC 不授予机器人控制；控制媒体新鲜度继续采用实际代码的 1,000 ms。
- 验证证据：用户明确授权先行完成 Week 3；Windows Debug OFF/ON 全目标构建与完整 CTest 分别
  39/39、41/41，固定离线 Annex-B IDR 实际解码、容量/generation/十轮重复关闭、RTMP/AAC/MQTT/UI、
  Week 2 loopback 和依赖门禁均通过。`W3-GATE` 自动技术通过；`W2-GATE` 人工复核仍待补充，
  Track、双客户端和产品 WebRTC 路径未实施。
- 相关文件：`docs/roadmap/webrtc_v2_project_plan.md`、
  `docs/versions/webrtc-v2/guides/`、`docs/versions/webrtc-v2/weeks/week02/`、
  `docs/versions/webrtc-v2/weeks/week03/`

## ADR-034 WebRTC 入门教程采用独立单进程 DataChannel MiniLab

- 日期：2026-08-22
- 状态：已采用
- 背景：现有六篇指南偏理论，初学者缺少一条从环境配置到可观察结果的短路线。直接复用 Week 2
  probe 会同时引入 Qt、文件 schema、ACL 和双控制台操作，容易把信令文件安全与 WebRTC 基础协商
  混成一个学习问题；直接进入视频 Track 又会提前引入 RTP/H.264 和媒体线程生命周期。
- 决策：保留六个指南路径并改写为六章 Kilo 式微步骤教程；仓库只保存一份最终源码。新增独立
  `tutorials/webrtc-minilab/`，仅依赖精确版本的 `LibDataChannel::LibDataChannel`，在单进程中使用
  两个真实 PeerConnection、空 ICE server、内存 non-trickle Offer/Answer 和 DataChannel
  `ping -> pong`。每个代码步骤必须给出函数级小块、彩色删除/新增说明、无变更标记的可复制权威
  代码、构建/运行/实际证据和稳定通过条件；每个代码块后立即说明函数/API、参数、返回值、线程、
  所有权、失败方式、执行链和限制。每章必须给出非照抄实验、完整答案和验证命令。步骤快照只在
  Git 已忽略的 `out/` 重放，不接入根 CMake、产品 target 或既有公共头文件。
- 修订（2026-08-23）：最初采用 unified diff 作为增量载体，但初学者复制时容易把行首变更符号写入
  源码，长重构也不利于逐函数理解。因此改用浅红删除区块、浅黄新增区块和紧随其后的普通代码围栏；
  若预览器过滤颜色，文字标签与无标记复制块仍能工作。检查点仍按顺序重放，最终项目、隔离边界和
  仓库只保存一份最终源码的决定不变。
- 原因：MiniLab 把学习闭环限制在 PeerConnection、SDP/ICE、DataChannel、异步等待和安全关闭五个
  核心概念，同时保留真实库行为。彩色变更说明、可复制代码与即时检查点共同避免“解释概念后直接
  复制最终源码”的跳步，并降低把展示符号误复制进源码的风险；独立组合根让教程的构建、CLI 和失败
  模式不会扩散到 RTMP 产品，单份最终源码则避免六套步骤快照长期漂移。
- 替代方案：扩展 Week 2 probe 作为教程；保存六份逐章源码；第一天直接实现 Track/H.264 视频；使用
  mock PeerConnection。它们分别增加前置概念、维护重复、扩大生命周期风险或失去真实协商证据。
- 影响：新增外部表面只有独立教程项目及其 `webrtc_minilab` CLI；产品安装、运行时、网络默认值、
  Week 2 schema/probe 和 Week 3 媒体契约均不变。MiniLab 不创建信令文件、不访问 STUN/TURN，也不
  宣称已经实现视频、双机 LAN 或公网穿透。
- 验证证据：2026-08-23 使用 VS2026/MSVC 19.51 从全新目录重放 14 个正文检查点，全部配置、构建
  和运行成功；角色反转实验返回 0，协议不匹配实验立即给出固定分类并返回 1。最终 MiniLab CTest
  2/2 通过；帮助、非法参数、十轮运行、shutdown/Cleanup/summary 计数和敏感输出扫描通过；隐藏依赖
  时配置返回 1 且出现固定修复提示。六章结构校验为 0 错误、0 警告，彩色 HTML 配对、代码围栏、
  无变更标记、相对链接和最终检查点逐文件一致性检查通过。根项目既有 OFF/ON 39/39、41/41 证据
  未被本次纯文档修订改写。
- 相关文件：`tutorials/webrtc-minilab/`、`docs/versions/webrtc-v2/guides/`、
  `docs/versions/webrtc-v2/README.md`

## ADR-035 Week 4 publisher 采用 transport/source 兄弟目标与测试专用接收 peer

- 日期：2026-08-23
- 状态：已采用；R2 实施完成
- 背景：Week 4 需要在不扩展 Week 2 DataChannel probe、不让 media/UI 依赖 WebRTC 的前提下，
  同时证明 Offerer/Answerer 两种 publisher 角色、H.264 Track、MP4 pacing 和安全关闭。产品
  ReceiveOnly depacketize/viewer 属于 Week 5，不能为测试方便提前进入产品路径。
- 决策：新增纯契约 `rtmp_monitor_webrtc_contracts`、只依赖契约和 libdatachannel 的
  `rtmp_monitor_webrtc_transport`、只依赖 H.264 契约和 FFmpeg 的
  `rtmp_monitor_h264_publisher_source`；仅 `rtmp_monitor_webrtc_client` 组合 source、transport 与
  schema-v1 文件信令。`WebRtcEndpointSession` 唯一拥有 PC、Track、generation、容量 2 队列和
  sender；source 只持有注入的 H.264 提交端口。BUILD_TESTING 下另建 ReceiveOnly peer，通过
  libdatachannel H.264 depacketizer 验证 SPS/PPS/IDR，不把它作为产品 viewer。
- 原因：PeerConnection、文件/FFmpeg source 和产品 decoder 分别有独立依赖与生命周期。兄弟目标
  保持变化隔离；测试 peer 既能提供 AU 级证据，也不冻结 Week 5 的产品接收契约。
- 替代方案：扩展 Week 2 probe；让 transport 创建 source；让 source 直接持有 Track；在 Week 4
  产品 endpoint 中加入 depacketize/viewer。它们分别混合学习探针、反转依赖、泄露协议对象或提前
  扩大 Week 5 契约。
- 影响：WebRTC 默认仍 OFF。ON 时新增 publisher client；CLI 只接受 publisher、sample、
  offer/answer 和有界超时。关闭固定为 generation/回调失效、source join、sender join、Track/PC
  关闭与一次有界 Cleanup。样本仅在忽略目录合成，不提交、不安装、不分发。
- 验证证据：VS2026 Debug fresh OFF/ON 全目标构建与 CTest 39/39、43/43；两种 publisher 信令
  角色经真实 Track 自然退出；测试 peer 验证首个可恢复 AU 含 SPS/PPS/IDR；容量溢出/等待 IDR、
  generation、十轮关闭、MP4/BSF/pacing/停止/错误、CLI、层依赖、OFF 产物和输出脱敏门禁通过。
- 相关文件：`CMakeLists.txt`、`include/common/webrtc_transport/`、`include/common/publisher/`、
  `src/common/webrtc_transport/`、`src/common/publisher/`、`src/tools/WebRtcClientMain.cpp`、
  `scripts/webrtc/qualify_week4.ps1`、`docs/versions/webrtc-v2/weeks/week04/`

## ADR-036 Week 5 接收链只在组合根接入媒体并复用窄画布目标

- 日期：2026-08-23
- 状态：已采用；R2 实施完成
- 背景：Week 5 需要让同一客户端支持 ReceiveOnly 与双信令角色，并把 libdatachannel 重组的
  H.264 AU 接入既有 FFmpeg/mailbox/CPU 画布。若 transport 直接依赖 media/UI，或客户端链接完整
  `rtmp_monitor_ui`，会破坏兄弟模块边界并扩大高扇入依赖。
- 决策：`WebRtcEndpointSession` 只暴露协商前设置的协议无关 H.264 receive sink，并在 transport
  私有 `H264ReceivePipeline` 中完成当前 generation 的 Annex-B、SPS/PPS/IDR、4 MiB 上限和 RTP
  时间戳门控。组合根以 `shared_ptr<EncodedVideoInputHandle>` 持有媒体入口，sink 只捕获弱引用；
  media handle 自己恢复 media generation。将 `VideoCanvasHost`、CPU/OpenGL canvas 抽为
  `rtmp_monitor_video_canvas`，产品 UI 与测试客户端共同链接该窄目标。
- 原因：RTP/depacketize 与 decoder/render 有不同变化原因、依赖和线程模型。弱 handle 使 endpoint
  和 media generation 分别在所属 owner 内失效；窄画布目标复用已有渲染而不把完整产品 UI 带入
  测试客户端。
- 替代方案：transport 直接创建 decoder；media 依赖 libdatachannel；客户端链接完整 UI；自行实现
  RFC 6184；新增第二套 viewer renderer。它们会反转依赖、重复状态机、增加交叉所有权或形成并行
  渲染框架。
- 影响：`WebRtcEndpointSession` 增加 `setReceiveSink`、接收错误与计数；media 公共接口、RTMP
  façade、schema v1 和产品配置不变。关闭顺序为信令取消、endpoint generation/回调失效、worker
  汇合、Track/PC 关闭、媒体 handle 关闭、画布销毁和一次 Cleanup。WebRTC OFF 产物边界不变。
- 验证证据：VS2026 Debug fresh OFF/ON 全目标构建与 CTest 39/39、44/44；viewer pipeline 真实
  解码并取得非黑 framebuffer；两种双客户端信令拓扑各收到 180 AU、5045 RTP packet 并完成
  decoded/presented；容量恢复、错误 codec/fmtp、generation、晚回调、重复关闭、层依赖、CLI、Qt
  插件部署、清理和 Week 4 完整回归通过。
- 相关文件：`CMakeLists.txt`、`cmake/CheckLayerDependencies.cmake`、
  `include/common/webrtc_transport/`、`src/common/webrtc_transport/`、`src/tools/webrtc_client/`、
  `tests/WebRtcEndpointSessionTest.cpp`、`tests/WebRtcViewerPipelineTest.cpp`、
  `scripts/webrtc/QualificationCommon.psm1`、`scripts/webrtc/qualify_week5.ps1`

## ADR-037 Week 6 便携信令根与脱敏 selected-pair 证据

- 日期：2026-08-23
- 状态：已采用；R2 技术实施完成；门禁推进策略后由 ADR-038 更新
- 背景：Week 5 客户端只能从 Git 仓库定位信令目录，Release ZIP 无法运行；既有
  `candidateTypes` 只说明 description 中出现的候选，不能证明实际选中路径。直接增加任意路径 CLI
  会扩大清理边界，输出 candidate/SDP 再脱敏会把地址带到上层。
- 决策：新增客户端私有纯值 `WebRtcClientRuntimePaths`。exe 同级存在 `package-manifest.json` 时
  固定使用包内 `session-exchange`，否则保持仓库模式；不增加任意目录或 source path 参数。
  `WebRtcEndpointSession` 使用 libdatachannel `getSelectedCandidatePair()`，只返回 host/srflx/relay
  与 udp/tcp 的 `EndpointCandidatePair`，地址、端口、candidate、SDP 和 fingerprint 不进入 DTO。
  已 Connected 后的 Disconnected/Failed/Closed 收敛为 Failed，客户端发 `connection_lost`。
- 原因：运行布局是客户端部署政策，selected pair 是 transport 事实，两者有独立 owner；把路径放
  transport 或把 candidate 文本放脚本都会产生跨层耦合。marker 让仓库/包清理根固定，typed DTO
  从源头满足最小证据。保留 `candidateTypes` 与 schema v1，兼容 Week 4/5。
- 替代方案：`--exchange-dir`、扫描任意当前目录、在 PowerShell 解析 SDP、引入信令服务器、让
  media/UI 查询 PeerConnection。它们分别扩大文件边界、依赖文本格式、超出 LAN 范围或反转依赖。
- 影响：ON 客户端增加私有 path source/test；transport 公共 connection result 只做 additive 字段。
  没有新线程、无界队列或反向链接。Release WebRTC 测试包独立包含实际 DLL、样本、许可、manifest
  和 runner；正式 RTMP 产品包/配置不变。同机 peer-reflexive 脱敏归为 srflx，不冒充 host。
- 验证证据：两种 endpoint RTP 拓扑取得安全 selected pair；关闭一端后对端有界 Failed、sink 不再
  增长；path 表驱动测试覆盖 repository/portable/invalid。fresh Debug OFF、Debug ON、Release ON
  分别 39/39、45/45、45/45；最终 ZIP 全新展开、禁入扫描、CLI/runner 和同机双包两拓扑各十轮
  通过，Week 4/5 完整回归通过。真实 host/host UDP 与窗口生命周期保持待用户。
- 相关文件：`include/common/webrtc_transport/WebRtcEndpointSession.h`、
  `src/common/webrtc_transport/WebRtcEndpointSession.cpp`、`src/tools/webrtc_client/`、
  `scripts/webrtc/Week6LanCommon.psm1`、`scripts/webrtc/package_week6.ps1`、
  `scripts/webrtc/qualify_week6.ps1`、`scripts/webrtc/week6_lan_test.ps1`、
  `docs/versions/webrtc-v2/weeks/week06/`

## ADR-038 本地双实例作为 Week 6 设计门禁，物理双机资格延期

- 日期：2026-08-24
- 状态：已采用；`W6-DESIGN-GATE` 与本阶段 `W6-GATE` 通过
- 背景：当前没有第二台电脑。最终 Release ZIP 已从干净提交生成并全新展开，在同一主机的两个
  独立包副本中完成 publisher/Offerer ↔ viewer/Answerer、viewer/Offerer ↔ publisher/Answerer
  各十轮，合计 20/20；构建、CTest、selected UDP pair、RTP/AU/decoded/presented、角色反转、
  退出和零残留证据齐全。继续把物理双机作为研发硬阻塞会让设备条件而非代码风险控制进度。
- 决策：用户接受上述本地双实例结果作为 Week 6 设计与开发门禁。`W6-DESIGN-GATE` 和本阶段
  `W6-GATE` 标记通过，Week 7/P2P 解锁。物理 `W6-LAN-01/02/03`、`W6-LIF-01` 归入
  `W6-PHYSICAL-LAN` 延期环境资格；未来可补测，但不再回溯阻塞 Week 7。
- 事实边界：同机结果继续标记 `sameMachinePortable=true`、`lanClaimed=false`，不能宣称两台物理
  Windows、真实网卡/防火墙、跨电脑搬运或 host/host UDP 已验证。设计通过是验收推进决定，不是
  对未执行环境事实的伪造。
- 影响：只修改文档状态、门禁命名和路线推进；不修改 C++、CMake、CLI、schema、PowerShell
  行为、Release 包或模块依赖。既有 VerifyLan 仍可严格校验四份物理报告，其历史错误文字不再
  代表 Week 7 被阻塞。
- 验证：最终 ZIP 本地独立双包 20/20、fresh OFF 39/39、Debug ON 45/45、Release ON 45/45、
  Week 4/5 完整回归和文档 SelfTest 均已有实际通过记录。
- 相关文件：`docs/versions/webrtc-v2/weeks/week06/`、`docs/roadmap/project_plan.md`、
  `docs/memory/project_snapshot.md`、`docs/project_handoff.md`

## ADR-039 固定本机 ICE 配置与地址无关 ICE 事实

- 日期：2026-08-24
- 状态：已采用；R2 实施完成，最终资格结果见 Week 7 test results
- 背景：Week 6 只能使用 host candidate。Week 7 需要测试 STUN 收集和以后公网非 relay 路径，
  但把 URL/凭据放入 CLI、session package、profile 或日志会扩大历史记录和持久化边界；只返回
  Connected 又不足以区分收集、检查、selected pair 和媒体呈现。
- 决策：客户端新增默认 host 的 `--ice-mode host|stun`。stun 只读取 repository/portable 各自
  固定 `local-config/ice-runtime.json`，由 client-private `WebRtcIceRuntimeConfigLoader` 一次性验证
  精确 schema v1、4 KiB 上限和无凭据 STUN；不接受任意路径、URL CLI、TURN 或热更新。
  transport 复用既有 `IceRuntimeConfig`，追加地址无关 `EndpointIceState`，只累计候选类型并返回
  脱敏 selected pair；timeout/failed 仍保留已观察类型与 state。
- 原因：配置位置是部署政策，属于客户端；ICE state、候选和 pair 是 transport 事实；跨轮次
  Direct/NeedsRelay 属于资格脚本。三层分离可保持 transport 不依赖 signaling/media/UI，并从源头
  阻止地址、candidate 和凭据进入上层 DTO。
- 替代方案：`--stun-url`、任意 `--ice-config`、把 URL 塞入 Offer/Answer schema、在 PowerShell
  解析 SDP、默认公共 STUN、在 endpoint 内读取 QFile。它们分别泄露命令历史、扩大文件边界、
  改变冻结 schema、依赖文本格式、启动默认网络或反转职责。
- 影响：旧命令默认 host且不读配置；session schema、media接口和产品profile不变；endpoint公共
  结果只做末尾追加。ICE回调捕获weak state+generation，配置无新线程，断线继续收敛失败且不做
  ICE restart。
- 验证证据：配置数据表、路径表、无效IceServer、timeout保留事实、本地libjuice srflx集成、
  两种endpoint拓扑、迟到回调、OFF边界和便携黑盒由 Week 7 自动资格覆盖。
- 相关文件：`src/tools/webrtc_client/`、`include/common/webrtc_transport/`、
  `src/common/webrtc_transport/`、`tests/WebRtcClientIceConfigTest.cpp`、
  `tests/WebRtcEndpointSessionTest.cpp`、`scripts/webrtc/qualify_week7.ps1`

## ADR-040 Week 7 本地设计门禁通过、真实公网资格延期

- 日期：2026-08-24
- 状态：已采用；设计验收策略由用户确认
- 背景：当前只有一台可用电脑，不能在移动网络与获授权公司网络之间取得真实公网证据。项目仍可
  用确定性回环STUN fixture和最终ZIP两个独立副本验证配置、srflx、角色互换、非relay pair、媒体
  呈现、清理和分类反例。让外部设备条件持续阻塞Week 8不会增加代码确定性。
- 决策：`W7-DESIGN-GATE` 以本地fixture、两个全新便携副本和两种拓扑各十轮为完成口径；通过后
  研发阶段 `W7-GATE` 标记通过并解锁Week 8。`W7-PUBLIC-NETWORK` 延期到当前电脑与公司台式机
  可用且网络/STUN获授权时执行。所有本地结果固定 `sameMachinePortable=true`、
  `publicClaimed=false`，不得声明公网Direct或NeedsRelay。
- 原因：设计门禁与环境资格验证不同风险。前者能确定代码和包是否正确，后者只能由真实设备、NAT
  和网络策略产生。明确拆分比用同机结果冒充公网或让Week 8无限等待更诚实。
- 影响：不扩展TURN、WSS、鉴权、TLS、RBAC、ICE restart、摄像头、多路或正式UI。包内runner和
  VerifyPublic保留以后执行路径；公网报告只有严格Direct或NeedsRelay才通过环境资格。
- 验证边界：本地fixture使用127/8和临时端口，不经过真实NAT、企业防火墙、运营商或CGNAT；即使
  srflx与媒体闭环通过，也只证明设计。真实公网状态在实际四份报告产生前始终为延期/未验证。
- 相关文件：`docs/versions/webrtc-v2/weeks/week07/`、
  `docs/roadmap/webrtc_v2_project_plan.md`、`docs/memory/project_snapshot.md`、
  `docs/project_handoff.md`

## ADR-041 Week 8 采用 ON-only 一次性产品组合层和呈现事实状态

- 日期：2026-08-25
- 状态：已采用；R2 实施完成，`W8-GATE` 本地研发门禁通过
- 背景：Week 5 已证明 ReceiveOnly RTP/H.264/FFmpeg/mailbox/画布闭环，Week 6/7 已证明便携
  文件信令与非 relay ICE 事实，但正式 `rtmp_monitor` 尚无入口。直接把 endpoint 塞进 MainWindow、
  让 transport 访问 media/UI、或提前把 peer 写进 SavedStreamProfile，会反转既有依赖并混淆
  设备身份、控制和 RTMP 回退。计划阶段预估 W8-ARC 为 R3；实际复核没有新增依赖方向、持久化
  schema 或外部公共契约，停线条件未出现，因此按架构门禁实施等级为 R2。
- 决策：WebRTC=ON 时新增非 UI `webrtc_runtime`，独占一条 ReceiveOnly endpoint、worker 和受管
  Offer/Answer；新增 `webrtc_product` 组合层，由 `WebRtcProductSessionController` 组装 runtime、
  弱 `EncodedVideoInputHandle`、mailbox 和普通 `VideoWidget`。一次性 request 只有显示名、信令角色
  和本次 ICE 值，不含 peer/device/profile/autoConnect/RTMP 字段。产品 `Direct` 必须同时满足非
  relay selected pair、endpoint Connected、当前代 presentedFrames>0 和呈现年龄≤1,000 ms；
  NeedsRelay 只接受 ConnectionFailed+ICE Failed+srflx。失败不启动 RTMP、不授权设备控制。
- 原因：runtime/product 分层让 transport、media、ui 保持兄弟模块，跨层知识只在组合层出现；呈现
  事实而非 Connected/RTP/decoded 能诚实表达用户当前看到的画面；运行期 request 避免为单次实验
  提前设计持久身份。激活 render item 与宣布 Direct 分开，避免“未 Direct 不渲染、未渲染无证据”
  的循环依赖，同时不降低状态标准。
- 替代方案：修改 MainWindow 持有 PeerConnection；让 media 链接 transport；扩展 profiles schema v2；
  Connected 即 Direct；失败按同名保存流回 RTMP；把 WebRTC 视频格注册成 MQTT 控制目标。它们分别
  导致 God Class、反向依赖、超范围持久化、不真实状态或隐式协议/权限切换。
- 影响：ON 构建增加 runtime/product 静态目标、主程序条件链接和 WebRTC DLL 部署；OFF 构建无菜单、
  product target/test 或自动网络。controller 一次只允许一条会话；取消顺序为 token 失效、stop/
  endpoint close、join、input close、remove stream/widget；应用退出最后有界 `rtc::Cleanup()`。
  schema v1、SavedStreamProfile、autoConnect、RTMP、MQTT 和设备控制公共契约不变。
- 验证证据：fresh Debug OFF 39/39、Debug ON 47/47、Release ON 47/47；产品测试以两种接收端信令
  角色完成真实 PeerConnection、H.264 解码/呈现、Direct 与取消；层依赖、OFF feature macro、
  controlAuthorized=false、rtmpFallbackStarted=false 和 1,000/1,001 ms 边界通过。正式人工观感、
  真实双机 LAN、公网和 ARM 仍未验证。
- 相关文件：`include/common/webrtc_runtime/`、`src/common/webrtc_runtime/`、
  `include/common/webrtc_product/`、`src/common/webrtc_product/`、`tests/WebRtcProductSessionTest.cpp`、
  `scripts/webrtc/qualify_week8.ps1`、`docs/versions/webrtc-v2/weeks/week08/`

## ADR-042 摄像头发布采用 publisher 内窄 MF source 与单一 h264_mf 回退

- 日期：2026-08-30
- 状态：已采用；真实摄像头环境资格待授权
- 背景：Week 4 的 publisher 只有固定 MP4 样本。摄像头输入需要设备采集、编码能力选择、时间戳和
  阻塞读取停止，但若为此创建通用媒体插件层、保存设备档案或增加多硬件编码器矩阵，会扩大 Week 9
  范围并破坏 publisher 的明确边界。
- 决策：在既有 publisher target 增加具体 `CameraH264PublisherSource`。Windows 条件使用 Media
  Foundation，固定 1280×720@30；实际 AU 证明 baseline、level≤3.1、SPS/PPS、无 B slice 和 IDR
  间隔≤30 帧才原生 Annex-B 直通。不合规时 Shutdown 并重新打开同一设备为 NV12，只使用实际合成
  NV12 编码并由 FFmpeg 解码验证通过的 `h264_mf`；否则稳定阻塞。非 Windows 返回
  platform_unsupported。设备只公开 camera-N 别名，不持久化或哈希标识。
- 原因：publisher 仍只输出既有 `H264SubmitPort` 契约；单一路径矩阵减少驱动组合和停止生命周期风险，
  同时保留原生低开销路径。真实设备事实必须由授权环境产生，不能由 MP4/fixture 推断。
- 替代方案：通用 MediaSource/插件框架；libavdevice；外部 ffmpeg/x264；NVENC/AMF/QSV fallback；
  保存 symbolic link 或设备指纹。它们会引入新依赖、进程、身份持久化或未经资格的组合爆炸。
- 影响：Windows publisher 增加 mf/mfplat/mfreadwrite/mfuuid/ole32 条件链接；source 独占一个 worker，
  无用户态帧队列，停止为 closing/Flush/独立 join 门禁/同指针 reader 清理/MF+FFmpeg 释放。schema、产品 profile 和 OFF 行为
  不变，层门禁明确禁止 publisher 依赖 runtime/product。
- 验证证据：约束位、首个非空 AU、提交时戳、encoder drain、失败后 stop 与 waiter/stop 并发组件
  CTest 在 Debug/Release 通过；真实摄像头 CAM-01/CAM-09 保持 blocked(camera_environment)。
- 相关文件：`include/common/publisher/CameraH264PublisherSource.h`、
  `src/common/publisher/CameraH264PublisherSource.cpp`、`src/common/publisher/CameraH264Policy.cpp`、
  `tests/CameraH264PublisherSourceTest.cpp`

## ADR-043 产品多会话采用 StreamId SessionContext map 并保持三类 generation 分离

- 日期：2026-08-30
- 状态：已采用；四台物理 endpoint 与长时资源资格待验证
- 背景：Week 8 controller 直接拥有单套 slot/runtime/widget/input 和全局 token。扩展到四路若复用
  全局 generation 或在单路故障时停止共享 timer/pool，会让旧回调污染新会话并放大故障范围。
- 决策：controller 私有持有最多四项 StreamId→SessionContext map，每项独占最低空闲 slot、product
  token、widget、input、receive runtime、mailbox、connection、state 和 freshness。endpoint
  generation、product token、media generation 不合并。start 在 worker 成功后才发 signal；逐路取消先
  detach route，取消全部先 detach 并停止全部 runtime 再统一 join，closingAll 阻止 signal 重入。
  无参 API 保留兼容聚合语义。
- 原因：StreamId 已是 media/UI 的运行期关联键；context map 把状态和生命周期放在唯一组合 owner，
  无需让 runtime、media 或 UI 反向认识产品多会话。三个 generation 分属 transport、组合回调和解码
  handle 的不同失效边界，合并会造成跨层全局状态。
- 替代方案：四个固定成员；全局 generation；每路独立诊断 timer/decode pool；把会话 map 放进
  MainWindow 或 playback manager。它们会重复逻辑、混淆所有权或反转依赖。
- 影响：最多四路使用 session-01～04，第五路 capacity_reached；逐路诊断不伪造 OS 资源，无参多路
  快照只给无效 StreamId 的聚合状态。rtc::Cleanup 仍在所有会话结束后的进程退出执行一次。
- 验证证据：四组真实同机 SendOnly/ReceiveOnly PeerConnection 完成 RTP→decode→mailbox→presented→
  Direct，覆盖第五路零副作用拒绝、远端先关闭一路而其余增长、单路小于 1 秒停止、slot/generation
  重建、同步 signal 取消重入和动画期 widget 延迟移除。
- 相关文件：`include/common/webrtc_product/`、`src/common/webrtc_product/`、
  `tests/WebRtcProductSessionTest.cpp`

## ADR-044 低延迟远程操作采用 WebRTC-first 视频与 MQTT 控制分面

- 日期：2026-08-30
- 状态：产品方向已确认；实施须在 W9/W10 门禁通过后分阶段进行
- 背景：当前 WebRTC V2 的 Week 1～10 目标是可交付测试的 P2P Beta，稳定 RTMP 产品链路仍然存在。
  后续产品目标已经明确为低延迟远程操作：每台设备的视频需要独立、可隔离、可恢复的 WebRTC 会话，
  而设备与软件之间已有 MQTT 控制和状态基础设施。若把视频建连、设备身份和控制授权隐式合并，或用
  DataChannel 顺手替换 MQTT，会扩大故障域并破坏现有硬件边界。
- 决策：产品实时视频采用 WebRTC-first，每一路视频对应独立 PeerConnection、StreamId、代次、队列
  和 UI tile；Direct 优先，受限网络使用 TURN Relay，二者都是 WebRTC 链路。MQTT 继续承载命令、
  回执、状态和遥测；WSS 只承载自动信令、trickle ICE 与短期会话授权；ICE/STUN/TURN 只负责可达性。
  应用组合根负责把已授权的设备身份、操作员、WebRTC StreamId/tile 和 MQTT 控制目标绑定为一条
  运行期设备会话。视频建连不得自动授权控制，MQTT 在线也不得自动选择设备，切换 tile 不得静默切换
  控制目标。RTMP 迁移期保留，但不得作为 WebRTC 失败时的静默 fallback；产品门禁全部通过后退出
  实时视频主链路。
- 原因：WebRTC 提供低延迟媒体、拥塞适应和 Direct/Relay 恢复路径；MQTT 保留成熟的硬件中间层职责；
  四个平面由组合根显式关联，能隔离视频故障、控制权限和连接状态，也能让多设备按单会话故障域扩展。
- 替代方案：继续以 RTMP 作为远程操作主链路；只支持纯 Direct 而不部署 TURN；用 WebRTC DataChannel
  替换 MQTT；让多台设备共享一条全局 PeerConnection 或全局 generation。它们分别无法满足产品低延迟
  与公网可达性、重复建设控制协议，或放大跨设备故障和旧回调污染风险。
- 影响：本次只更新路线和架构约束，不修改现有代码、公共接口、schema v1、线程模型、依赖或默认网络
  行为。后续 WSS、TURN、身份授权和运行期 DeviceSession 必须逐项通过独立设计与资格门禁；持久设备
  身份或 schema 变化仍需单独评审。初期产品聚焦一个操作员控制一台设备、最多四路独立视频；多观看者、
  SFU、WHIP/WHEP 和双向音频不是这项决定的隐含范围。
- 验证证据：本次为规划确认，不产生运行时测试证据。W9-GATE 仍为
  `blocked(camera_environment,resource_smoke_not_run)`，Week 10 尚未完成；产品化阶段、失败条件和
  RTMP 退役门禁已写入 WebRTC V2 总计划第 9 节。
- 相关文件：`docs/roadmap/webrtc_v2_project_plan.md`、`docs/roadmap/project_plan.md`、
  `docs/memory/project_snapshot.md`、`docs/project_handoff.md`

## ADR-045 Week 10 采用测试专用代表负载 runner 并分离本机与现场资格

- 日期：2026-08-31
- 状态：已采用；摄像头和物理 LAN 资格仍受外部环境阻塞
- 背景：Week 9 的短时 fixture 集成测试只证明四路生命周期，不能提供 720p30 全程逐路峰值、工作集
  趋势或 30 分钟故障恢复证据。若为资格负载修改生产 publisher API、加入通用媒体框架，或把同机
  P95 写成 LAN P95，会扩大产品契约并混淆证据边界。
- 决策：新增仅在 `BUILD_TESTING && RTMP_MONITOR_ENABLE_WEBRTC` 下构建的资格 runner。它通过既有
  MP4 publisher 读取一轮有界且从 IDR 开始的代表性 AU，保持码流不可变，只重建 33,333 微秒媒体
  时间戳；单个 pacing worker 直接向一或四个既有 `H264SubmitPort` 提交，不增加生产队列或 API。
  media 统计只读追加内部延迟 P50/max。父进程独立采集进程 CPU/工作集，并把
  `sameMachineSoftwareQualified`、`physicalLanQualified`、`performanceQualified` 分开记录。
- 原因：测试组合根可以复用真实 PeerConnection、RTP、解码、mailbox 和 UI 呈现链路，同时把负载生成、
  故障时钟和资格输出留在测试边界；独立资格布尔值防止从同机、交叉构建或短测外推现场能力。
- 替代方案：修改生产 camera/MP4 source 支持循环；新增通用 MediaSource；外部 ffmpeg 进程；用短时
  fixture 或同机 P95 直接关闭 W9/W10 门禁。它们会改变生产职责、增加进程/编码矩阵，或形成错误声明。
- 影响：生产依赖方向、H264 契约、schema v1、MQTT、RTMP、信令和网络默认值不变；runner 是 ON-only
  test 组合目标。候选包 manifest 只记录版本、Git source commit、相对路径和大小，不增加内容哈希。
- 验证证据：fresh Debug/Release OFF 39/39、Debug/Release ON 49/49；正式单路 600 秒、四路
  1,800 秒及停止/重建通过，工作集斜率分别为 0.176、0.134 MiB/min。53 文件 Windows 候选包在
  两个干净副本完成 2/2 本地角色闭环；ARM64 RASTER/GLES3 WebRTC OFF 交叉构建通过。现场摄像头、
  物理 LAN、ARM WebRTC 和 ARM 真机仍未验证，详见 Week 10 `test_results.md`。
- 相关文件：`tests/WebRtcQualificationRunnerMain.cpp`、`scripts/webrtc/qualify_week10.ps1`、
  `scripts/webrtc/week10_performance_worker.ps1`、`docs/versions/webrtc-v2/weeks/week10/`

## ADR-046 第一阶段采用隔离的 MQTT TLS 信令并排除 legacy 公网测试 Broker

- 日期：2026-09-01
- 状态：部分被覆盖；阶段阻塞由 ADR-047 覆盖，现有公网 Broker 的产品定位由 ADR-048 覆盖
- 背景：现有 RTMP 产品使用默认关闭、Broker 地址为空的单客户端 MQTT 3.1.1 控制路径，
  `device/control` 与 `device/status` 分别承担嵌入式设备控制和状态观察。用户另行授权的远程设施只
  提供公网明文 MQTT 与 HTTP 管理面，用于兼容性观察，不具备产品 MQTTS、安全或许可资格。ADR-044
  原先把 WSS 和 TURN 冻结为后续产品面，但当前双方都是原生客户端，首阶段没有浏览器或 relay 需求。
- 决策：第一阶段产品信令使用 MQTT 5 over TLS；signaling 与 control 可以共用未来合格的 Broker
  基础设施，但必须使用独立连接、ClientId、principal、topic、ACL、payload、队列、状态机和指标。
  `device/control` 与 `device/status` 继续属于 legacy/control 平面，不得承载 SDP、candidate 或会话
  授权。首阶段严格 Direct-only，不部署 TURN；WSS 只保留为未来 `ISignalingChannel` adapter。
  当时将现有远程设施限定为 legacy 测试输入并从产品候选中排除；该产品定位现已由 ADR-048 覆盖。
- 安全边界：产品与测试默认均保持网络关闭；真实 Broker/管理地址、凭据和测试 topic 只进入本机忽略
  配置，不进入源码、文档示例、测试资源、普通日志或发布包。legacy 观察只允许随机精确 topic 的有界
  SUBSCRIBE，不发布、不订阅控制/状态 topic、不登录管理后台或调用写 API。
- 原因：MQTT 已是设备侧基础设施，标准 MQTT 5 能承载短期信令；独立平面和 adapter 能避免把协商
  状态、媒体生命周期和设备控制混入同一故障域。排除明文 legacy 服务可以防止测试便利被误当产品
  默认或安全证据。
- 替代方案：继续建设 WSS；把 signaling 塞入 `MqttDeviceClient`；复用 legacy control/status topic；
  在现有明文 Broker 上直接增加产品 listener；提前部署 TURN。它们分别增加未需要的在线服务、扩大
  控制故障域、造成身份/消息混淆、违反测试设施边界或超出 Direct-only 范围。
- 影响：本 ADR 只覆盖 ADR-044 的 WSS/TURN 首阶段选择；ADR-044 的 WebRTC 媒体、MQTT 控制、组合根
  授权绑定、四路独立会话和禁止静默 RTMP fallback 继续有效。现有 schema、legacy topic、产品线程、
  默认离线行为和运行时依赖不变。后续若进入第三方托管/客户嵌入、TURN 或 WSS，必须新建许可/架构 ADR。
- 验证证据：fresh Windows OFF 40/40、40/40，ON 50/50、50/50；ARM64 RASTER/GLES3 OFF、
  341/483 条 DAG 边、fixture self-test、敏感扫描和无 publish legacy observe 通过。隔离产品 Broker
  的 TLS/Auth/ACL/retained/QoS/expiry/limit 矩阵未执行，故阶段为 `blocked(broker_candidate)`；详见
  `docs/versions/webrtc-v2/p2p-direct-00/broker_decision.md` 和 `test_results.md`。
- 相关文件：`docs/roadmap/RtmpMonitor_WebRTC_MQTT_Signaling_Direct_P2P_Productization_Outline_v2.md`、
  `docs/roadmap/RtmpMonitor_WebRTC_MQTT_Signaling_Direct_P2P_Implementation_Plan.md`、
  `docs/versions/webrtc-v2/p2p-direct-00/`

## ADR-047 取消 Broker 安全资格的阶段前置并解锁离线协议契约

- 日期：2026-09-01
- 状态：已采用；用户确认的 R3 产品范围决定
- 背景：ADR-046 原要求先在隔离 EMQX/Mosquitto 上完成 TLS/Auth/ACL/retained/QoS/expiry/limit
  负向矩阵，才允许进入 `P2P-DIRECT-01`。用户明确决定当前研发不需要这组正式产品安全测试，并要求
  解锁 DIRECT-01。
- 决策：将 `P2P-DIRECT-00` 标记为 `passed(scope_reduced_by_user_decision)`。Broker 安全资格从阶段
  前置中完全移除，只作为可选的未来加固项；未执行内容仍写作“未验证”，不得表述为技术通过。
  `P2P-DIRECT-01` 只实现离线身份、topic、消息、状态与 provisioning contract，不连接或修改 Broker。
- 保留边界：默认网络关闭、Broker 地址为空；真实端点仅可位于本机忽略配置，不进入源码、文档示例、
  测试资源、日志或发布包；legacy `device/control`、`device/status` 不承载新信令；不得修改现有 Broker
  核心配置。
- 影响：这是对阶段验收范围的显式缩减，不是对 Broker 安全性的认可。未来需要公网产品信令时可另行
  启动加固评审，但它不再阻塞当前阶段研发。
- 验证证据：用户在本会话明确取消该门禁；DIRECT-00 已有的本地构建、DAG、ARM、fixture 和敏感扫描
  证据保持有效，隔离 Broker 负向矩阵保持未执行。
- 相关文件：`docs/versions/webrtc-v2/p2p-direct-00/`、
  `docs/roadmap/RtmpMonitor_WebRTC_MQTT_Signaling_Direct_P2P_Implementation_Plan.md`

## ADR-048 团队共享公网 MQTT Server 作为当前产品首选 Broker

- 日期：2026-09-02
- 状态：已采用；用户确认的 R3 产品范围决定
- 背景：ADR-046 将现有公网 MQTT 设施永久限定为 legacy 测试输入，ADR-047 只取消了隔离 Broker
  安全资格的阶段前置。用户现已进一步确认：该设施是当前团队共同维护和使用的 MQTT Server，也是
  当前团队性质产品优先使用的公网 MQTT Broker，可用于 DIRECT-02 真实公网自动信令。
- 决策：当前产品部署优先复用 `<team-public-mqtt-broker>`。DIRECT-02 可对该 Broker 执行正常 MQTT
  客户端操作，包括显式连接、精确 topic 订阅、向 `rtmp-monitor/v1/...` 发布、取消订阅和断开；不再
  要求先部署另一台公网服务器。signaling 与 legacy control 仍必须使用独立连接、ClientId、topic、
  队列和状态机，`device/control`、`device/status` 不承载 WebRTC 信令。
- 配置边界：真实 IP、URL、端口组合和凭据不得进入源码、Git、示例、fixture、普通日志、发布包默认值
  或软件自动连接目标。产品二进制仍默认网络关闭、Broker hostname/port 为空；部署时通过 Git 外部的
  本机或受控部署配置显式注入真实 endpoint。
- 运维边界：本决定授权正常 MQTT 客户端数据面使用，不授权登录管理后台执行写操作，也不授权修改
  listener、认证、ACL、用户、插件、限额、retained 数据或其他 Broker 核心配置；如需这些操作必须
  另行取得明确授权。
- 资格表述：当前 endpoint 为明文 MQTT。它可以作为当前产品首选公网 Broker 和功能联调/运行设施，
  但不得表述为 MQTTS、TLS/Auth/ACL 安全资格已通过。TLS 与正式安全加固保留为可选未来工作，不阻塞
  当前团队产品研发。
- 覆盖关系：本 ADR 覆盖 ADR-046 中“现有远程设施永久仅作 legacy 测试、从产品候选排除”的结论，
  不覆盖 ADR-046 的平面隔离、禁止 legacy topic 承载新信令和禁止真实 endpoint 成为默认值等边界。
- 相关文件：`docs/versions/webrtc-v2/p2p-direct-02/broker_scope_decision.md`、
  `docs/roadmap/RtmpMonitor_WebRTC_MQTT_Signaling_Direct_P2P_Implementation_Plan.md`

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
