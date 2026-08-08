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
