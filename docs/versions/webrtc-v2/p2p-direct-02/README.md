# P2P-DIRECT-02 团队公网 MQTT 产品信令

> 状态：`passed(plaintext_team_broker)`（2026-09-02）
> Broker 范围：见 [当前产品 Broker 范围决定](broker_scope_decision.md)

本目录是 DIRECT-02 的权威结果入口。当前已实现共享 MQTT transport、broker-neutral Direct
Operator/Device Core、MQTT5 adapter、现有桌面组合根和 Windows BUILD_TESTING-only Device Harness。

- [实施总结](implementation_summary.md)
- [资格结果](qualification_results.md)
- [CMake 依赖 DAG](cmake_target_dag.md)
- [RTMP 产品功能复用 ledger](parity_ledger.md)

本状态证明团队公网明文 MQTT 上的自动逻辑 session、重复去重和主动重连可用；不代表 MQTTS、安全
Broker、trickle ICE、真实 WebRTC 媒体或 ARM 真机已经完成。
