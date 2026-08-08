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
- 相关文档和代码：`docs/guides/build-and-testing/cross_platform_build.md`、`docs/weeks/week6/week6_opengl_environment_and_validation.md`

## ISSUE-002 Windows 16 路长时性能资格测试

- 状态：已解决（2026-08-05）
- 影响范围：`--renderer=auto` 是否可成为发布默认值、性能发布结论。
- 已验证现象：16 路预录和双屏延迟 CPU/OpenGL 四组各 600 秒、20 秒预热与 Quality 门禁全部完成。OpenGL 16 路平均 CPU 1.50%（CPU 4.85%，降低 69.08%）、显示 14.91 FPS；双屏最差流 P95 196 ms、最大 317 ms；frame age/内部延迟、UI、队列、工作集、纹理和 8 例画质门槛全部通过。
- 复现步骤：按 `docs/weeks/week6/week6_renderer_performance_test_guide.md` 运行 `compare_renderers.ps1 -Action Run -Suite All`。
- 已排除内容：OpenGL 实际后端/fallback 误判、短窗口替代长测、纹理持续增长、颜色/stride 可见退化和源延迟缺样本均已排除。
- 解决方式：修正 15 FPS timer 的双重节流量化；对照流使用独立前缀；状态文件原子替换；纹理门禁允许计划内断流短暂释放，但要求不高于预热基线且末 60 秒完全恢复。CLI 默认切换为 `auto`，保留显式 CPU 回滚。
- 后续观察：在驱动、硬件、分辨率或部署环境变化后重新执行正式门禁；真实 ARM64 仍由 ISSUE-001 跟踪。2026-08-08 居中 16:9 网格的第一轮 120 秒预录 Video 快速对照通过，但它早于标题覆盖和 F11 监控墙。最终监控墙版本的同口径短测中，CPU、FPS、内部延迟、UI gap、队列、工作集和纹理门禁通过，latest frame age P95 为 CPU 47 ms、OpenGL 52 ms，超过相对限值 51.7 ms 0.3 ms，因此总控判定失败。该毫秒级差异需要正式长测判断，不能事后放宽门槛。2026-08-04 的四组 600 秒正式结果早于当前布局；当前布局需要新的完整发布认证时必须重新运行正式套件，不能用短测或旧数据替代。此前默认 Cover 只是已被替代的中间方案。
- 2026-08-04 恢复补充：Snapshot 的业务可见性修复已回归；同时根据用户录像定位并修复 `renderStateChanged + lambda + Qt::UniqueConnection` 被 Qt 6 拒绝、首帧不刷新主画布的问题。动态网格测试 29/29、完整 CTest 12/12 和四路无全屏实机验证均通过。
- 相关文档和代码：`README.md`、`docs/architecture/video_rendering_framework.md`、`docs/weeks/week4/week4_sixteen_stream_validation.md`

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
- 相关文档和代码：`docs/project_handoff.md`、`docs/guides/development/openviking_usage_and_testing.md`、`/etc/systemd/system/openviking.service`

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
- 相关文档和代码：`docs/memory/decisions.md`、`docs/guides/development/openviking_usage_and_testing.md`、WSL 安装内 `codex_responses_adapter.py`

## ISSUE-007 WSL 到 Codex backend 的代理 TLS 仍有间歇性 EOF

- 状态：未解决
- 影响范围：OpenViking 的文件摘要、Session commit 和记忆抽取；基础 `/health`、`/ready` 和本地存储不受影响。
- 已验证现象：2026-08-03 经既有 `127.0.0.1:7890` 代理对目标 endpoint 连续只读探测 10 次，3 次得到预期 HTTP 405，7 次为 `SSL unexpected eof while reading`。第一轮历史回填在第 4 个 Session 因同一 `httpx.ConnectError` 失败并完整回滚；第二轮在 Session 级精确回滚/重试保护下六份全部完成。
- 已排除内容：不是摘要脱敏规则、OpenViking Server 基础健康、embedding 或本地数据目录故障。按用户约束，本轮没有修改 Clash、代理规则、节点、订阅、端口、systemd 代理 drop-in 或防火墙。
- 下一步验证：由用户先确保当前代理核心稳定，再做连续 20～30 次只读 endpoint 探测；只有零 TLS 错误后，才将大批量 Resource/Session 任务视为稳定。OpenViking 侧继续要求任务终态和失败回滚。
- 临时规避：把批量写入拆成可审计的独立 Session，每份轮询到 `completed`；失败时根据对应 `memory_diff.json` 精确回滚后重试，不重复上传 Resource。
- 相关文档和代码：`docs/project_handoff.md`、`docs/guides/development/openviking_usage_and_testing.md`、`/etc/systemd/system/openviking.service.d/proxy.conf`

## Issue 模板

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
