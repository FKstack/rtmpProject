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

隔离 EMQX/Mosquitto 的 MQTT 5、TLS/Auth/ACL、retained/QoS/expiry、限额与恶意客户端 fixture
保留为可选未来加固，不再是 DIRECT 阶段前置。未执行这些测试，不宣称任何候选已获产品安全资格。

## 当前结论

受控 legacy 观察实际通过 MQTT 5 CONNECT、随机精确 topic SUBACK、无 payload、UNSUBACK 和
DISCONNECT；没有 publish，也没有订阅 control/status。该结果只证明当前兼容性。

用户已确认缩减当前研发门禁，`P2P-DIRECT-00=passed(scope_reduced_by_user_decision)`。本机与既有
WSL 没有执行隔离产品候选的 TLS/Auth/ACL/retained/QoS/expiry/limit 负向矩阵；fixture 的
`capability` 输出仍不能冒充资格。公网 MQTTS staging 不在 DIRECT-01 范围，legacy 明文 listener
也不得冒充产品环境。
