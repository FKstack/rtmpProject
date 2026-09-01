# P2P-DIRECT-00 基线冻结与 Broker 选型门禁

本目录保存产品级 MQTT 信令实施前的可审查基线。阶段只允许文档、测试/审计工具、CMake 测试目标和
依赖门禁变化；不修改产品运行路径、legacy MQTT 契约、持久化 schema 或默认网络行为。

权威结论：

- `summary.md`：阶段范围、状态和架构影响；
- `broker_decision.md`：legacy 测试设施与产品候选的分离决定；
- `dependency_inventory.md`：真实构建依赖和供应链边界；
- `rtmp_parity_ledger.md`：RTMP 能力迁移、保留或取消责任表；
- `cmake_target_dag.md`：去除个人路径的 OFF/ON target 依赖摘要；
- `testing_guide.md`、`test_results.md`：可重放验证和实际结果。

真实 Broker 地址、管理入口、凭据、topic、SDP、candidate 和网络标识禁止进入本目录。所有外部输入只
存在于 Git 忽略的 `out/p2p-direct-00/`，仓库证据仅记录脱敏能力分类。
