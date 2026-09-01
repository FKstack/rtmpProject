# P2P-DIRECT-00 测试结果

> 日期：2026-09-01
> 状态：`in_progress`

| 门禁 | 当前状态 | 证据边界 |
| --- | --- | --- |
| 文档/ADR/parity 基线 | pending | 等待首个提交 |
| fixture self-test | not_run | 尚未实现 |
| Debug/Release OFF | not_run | 尚未执行 fresh CTest |
| Debug/Release ON | not_run | 尚未执行 fresh CTest |
| ARM64 RASTER/GLES3 OFF | not_run | 尚未执行 |
| legacy observe | not_run | 只允许随机订阅、禁止 publish |
| isolated EMQX 6.2.3 | blocked(environment) | 候选实例尚未提供或创建 |
| Mosquitto 2.1.2 fallback | not_applicable | 仅 EMQX 失败后执行 |
| 敏感端点扫描 | pending | 真实端点不得进入 Git |

最终状态在全部实际命令完成后更新。任何 `not_run`、`blocked` 或 `pending` 不得写成通过。
