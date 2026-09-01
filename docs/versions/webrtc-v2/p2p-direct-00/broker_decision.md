# P2P-DIRECT-00 Broker 决策记录

## 已验证 legacy 事实

用户明确授权的远程设施仅作测试。2026-09-01 的只读检查得到：

- `plaintext_mqtt_reachable=true`；
- `mqtts_standard_port_reachable=false`；
- `dashboard_http_exposed=true`；
- `dashboard_family=emqx_dashboard_5`；
- `public_status_reports_running=true`；
- `unauthenticated_node_api_allowed=false`。

这些事实不包含实际地址、端口组合或凭据。关于“任意凭据可以登录”的描述未通过猜测密码验证，保持
`unverified`。本阶段不登录管理面、不调用写 API、不修改 listener、用户、ACL 或 retained 数据。

## 决策

legacy 设施从产品候选中排除。它最多用于一次有界的匿名 CONNECT/SUBACK 兼容性观察：随机 ClientId、
Clean Session、随机精确审计 topic、无通配符、无 publish、无业务 payload、超时后 unsubscribe 和
disconnect。不得订阅 `device/control` 或 `device/status`。

产品候选为隔离的 EMQX 6.2.3 单节点，必须通过 MQTT 5、TLS/Auth/ACL、retained/QoS/expiry、限额和
恶意客户端 fixture。内部自用单节点是当前许可前提；若交付或托管给第三方必须重新审查 BSL 1.1。
EMQX 任一门禁失败后才运行 Mosquitto 2.1.2 同等 fixture，不自研 Broker 插件。

## 当前结论

`blocked(broker_candidate)`：隔离 EMQX/Mosquitto 技术 fixture 与供应链审计尚未形成实际结果。公网
MQTTS staging 部署属于 `P2P-DIRECT-02`，不得用 legacy 明文 listener 代替。
