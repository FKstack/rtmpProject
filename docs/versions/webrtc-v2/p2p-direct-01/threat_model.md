# P2P-DIRECT-01 离线威胁边界

| 风险 | 本阶段控制 | 后续边界 |
| --- | --- | --- |
| credential 泄漏 | artifact 只含 reference，禁止 inline secret，日志脱敏 | secret store 与轮换/撤销执行延期 |
| topic spoofing / 跨设备 | source-bound 精确 Topic codec 与 roster-aware ACL policy | Broker ACL adapter 延期 |
| retained SDP / ICE | signaling policy 固定 retained=false | MQTT metadata/runtime 拒绝在 DIRECT-02 |
| QoS1 重复 | principal + MessageId 的有界 ReplayGuard | Broker DUP metadata 在 DIRECT-02 |
| ClientId takeover | signal/control ClientId 分离并确定性生成 | connection epoch 与 broker enforcement 延期 |
| 旧 nonce / 已关闭 session | 10 分钟 TombstoneStore，nonce 不进入日志 | nonce rotation runtime 延期 |
| 慢消费者 / control starvation | 本阶段无队列或网络；不会占用 control 路径 | 有界 MQTT 队列在 DIRECT-02 |
| candidate 洪泛 | 64 条、64 KiB、4 KiB/条、EOC 和语义去重纯策略 | 唯一 runtime buffer 在 DIRECT-03 |

当前真实 Broker 始终只允许作为显式、受控的开发测试设施；地址只在忽略配置中，默认网络保持关闭，
不得修改其核心配置。Broker 正式安全资格已按 ADR-047 从阶段前置中取消，但未因此获得安全认证。
