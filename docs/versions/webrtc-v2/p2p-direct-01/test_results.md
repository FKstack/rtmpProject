# P2P-DIRECT-01 测试结果

> 日期：2026-09-02  
> 最终状态：`passed`

| 门禁 | 结果 | 证据边界 |
| --- | --- | --- |
| Windows Debug / Release OFF | 41/41、41/41 | fresh binary directories，全目标构建与完整 CTest |
| Windows Debug / Release ON | 51/51、51/51 | fresh binary directories，全目标构建与完整 CTest |
| ARM64 RASTER / GLES3 OFF | passed(cross-build) | 283/283、297/297，均为 AArch64 ELF；不代表真机运行 |
| C++ contracts | passed | ID/UUID/ClientId、Topic/ACL、strict JSON、TTL/replay/tombstone/ACK、transition、candidate、config |
| Go unit | passed | `go test ./...` |
| Go race | passed | `go test -race ./...` |
| Go fuzz | passed | 3 秒，260,960 次执行，无 crash/hang |
| CLI smoke | passed | device create、pair create(control)、两份 validate；日志只含脱敏 ID 与计数 |
| layer/DAG | passed | 新目标只互相依赖及 PRIVATE Qt；无产品目标入边 |
| endpoint scan | passed | Git 与 `out/p2p-direct-01` 未命中测试设施地址或管理端口 |

Windows 的最终复验发生在 messageType、nullable envelope 和精确 payload/policy 表与实施计划对齐之后。
Go 版本为本机已配置的 1.22.0；普通终端可直接通过 PATH 使用，无需额外安装。

## 未声明内容

- 没有连接、登录、发布或修改任何 Broker；
- 没有验证 TLS/Auth/ACL 或真实 credential；
- 没有产品 MQTT signaling client、WebRTC 自动协商或 UI；
- 没有 ARM 真机运行；
- 没有创建发布包，因而不存在“发布包已完成产品资格”的声明。
