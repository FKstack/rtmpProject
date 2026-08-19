# RtmpMonitor 文档索引

文档采用“公共项目资料 + 版本资料”两层管理。跨版本事实保留在公共目录，协议、实现、测试和历史记录进入对应版本目录。

## 公共项目资料

- [记忆系统说明](memory/README.md)
- [当前项目快照](memory/project_snapshot.md)
- [重要设计决策](memory/decisions.md)
- [已知问题](memory/known_issues.md)
- [长期路线](roadmap/project_plan.md)
- [WebRTC V2 总计划](roadmap/webrtc_v2_project_plan.md)
- [移动安防产品模块演进建议](roadmap/mobile_security_product_module_recommendations.md)
- [v0.1.0-alpha.1 发布交接清单](roadmap/v0.1.0_alpha1_release_handoff_checklist.md)
- `project_handoff.md`：本机会话交接文件，不纳入 Git。

`memory/`、`roadmap/` 和 `project_handoff.md` 不归属于单个产品版本，继续保留在 `docs/` 根层。

## 版本资料

| 版本目录 | 状态 | 内容入口 |
| --- | --- | --- |
| `versions/rtmp-v1/` | 当前稳定基线 `0.1.0-alpha.1` | [RTMP V1 文档](versions/rtmp-v1/README.md) |
| `versions/webrtc-v2/` | `0.2.0-beta.1` 规划阶段 | [WebRTC V2 文档](versions/webrtc-v2/README.md) |

## 归档规则

1. 新增协议架构、开发指南、构建指南、周任务或版本历史时，必须写入对应 `versions/<version>/`。
2. 跨版本的当前事实、长期决策、已知问题和路线规划分别进入 `memory/` 与 `roadmap/`。
3. 不在 `docs/` 根层重新创建 `architecture/`、`guides/`、`weeks/` 或 `archive/`。
4. 引用文档时使用版本完整路径，避免 RTMP 与 WebRTC 的同名 Week 或指南互相覆盖。
