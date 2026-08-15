# RtmpMonitor 项目协作规则

## 新会话启动顺序

从仓库根目录开始工作，并按顺序读取或核对：

1. `docs/memory/project_snapshot.md`
2. `docs/project_handoff.md`
3. `docs/roadmap/project_plan.md` 中与当前任务相关的部分
4. 当前任务涉及的 `docs/guides/` 文档
5. 当前任务涉及的最近 `docs/weeks/` 文档
6. 实际源代码、CMake 配置、Git 状态，以及必要的运行或测试结果

不要只根据摘要或历史记录推断当前实现。开始修改前先检查工作区是否存在用户未提交改动。

## 信息优先级

发生冲突时使用以下优先级：

```text
实际源代码、运行结果和测试结果
> CMake 及真实配置
> docs/memory 中的已验证事实
> docs/project_handoff.md
> docs/roadmap 和 docs/guides
> docs/weeks 中的历史记录
> OpenViking 召回记忆
> 当前对话中的未经验证描述
```

不得静默选择冲突的一方。必须指出冲突、采用较高优先级来源，并更正或删除过期的 OpenViking 记忆。

## 架构质量门禁

- 任何生产代码、公共头文件、CMake 目标、线程模型、持久化、schema、CLI 或测试架构变更，开始编辑前必须使用 `$architect-code-changes`。
- 按 Skill 的 R0～R3 风险等级执行：R1 允许简化检查；R2 必须明确职责、依赖、数据/线程/资源所有权、兼容策略和验证计划；R3 必须暂停有风险的实现并请求确认。
- 保持项目编译依赖方向：`media` 不得依赖 `render` 或 `ui`，`render` 不得依赖 `ui`；`diagnostics` 可以只读组合 `media` 与 `render`，但不得被二者反向依赖；具体对象组装和跨层连接只能位于应用组合根。
- 新增依赖方向、改变公共契约或无法在原任务范围内保持既有边界时，按 R3 处理。未改变外部行为、公共契约或用户授权范围时，可自动采用更内聚的边界内实现。
- 拆分类时必须说明原类的独立变化原因、迁出的状态和生命周期、留下的职责以及调用迁移方式；不得按行数机械拆分类，也不得只移动函数而保留交叉所有权。
- 项目架构事实以 `docs/architecture/progressive_decoupling_architecture.md`、实际 CMake target 和 `cmake/CheckLayerDependencies.cmake` 为准。文档与实现冲突时采用实际代码、构建和测试证据并修正文档。
- 最终交付必须包含简短的“架构影响”：风险等级、职责变化、依赖变化、契约/生命周期以及实际验证；普通 R1 修复无需单独创建 ADR。

## 会话结束规则

- 当前项目状态变化时更新 `docs/memory/project_snapshot.md`。
- 做出重要设计决定时更新 `docs/memory/decisions.md`。
- 发现可复现且未解决的问题时更新 `docs/memory/known_issues.md`。
- 每次有意义的开发会话结束时更新 `docs/project_handoff.md`。
- 长期路线变化时更新 `docs/roadmap/project_plan.md`。
- 普通代码改动不要全部记录为架构决策。
- 未经验证的内容写为“待确认”，不得保存成既定事实。

## 长期记忆安全边界

不得在仓库记忆或 OpenViking 长期记忆中保存：

- API Key、Token、密码、私钥或 `.env` 内容
- 含鉴权参数的完整 RTMP URL
- 大段源码、大量原始日志或临时构建产物
- 未经验证的猜测
- 个人绝对路径，除非它属于已验证并明确标记的本机环境说明

OpenViking 只负责语义召回；仓库中的代码、CMake、测试结果和可审查文档始终是权威来源。

## 网络端点与隐私安全边界

- 禁止把用户、客户、设备或现场的公网 IP、域名、完整服务 URL 设为产品默认值、示例配置默认值或自动连接目标。
- 默认网络功能必须关闭；真实端点只能由用户明确输入，并仅保存到本机忽略目录，不得进入源码、文档示例、测试资源或发布包默认配置。
- 自动化测试优先使用回环地址、RFC 1918 私网地址或 `<broker-host>`、`<rtmp-host>` 等明确的符号占位符。
- 一旦发现真实端点进入 Git 历史，必须立即停止发布，在完成历史脱敏、可达引用审计和制品重建前不得继续分发。
