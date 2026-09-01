# P2P-DIRECT-02 当前产品 Broker 范围决定

> 日期：2026-09-02
> 权威决定：ADR-048
> 状态：已确认，DIRECT-02 尚未实施

## 已确认事实

- `<team-public-mqtt-broker>` 是当前团队共同使用的公网 MQTT Server，也是当前团队性质产品优先使用的
  Broker；DIRECT-02 不需要另外准备公网服务器。
- 当前 endpoint 使用明文 MQTT。它可以承载当前产品自动信令，但不能被描述成 MQTTS、TLS/Auth/ACL
  安全资格已经通过。
- DIRECT-00 曾将其定位为 legacy 测试设施；该产品定位已被 2026-09-02 的 ADR-048 明确覆盖。

## 获授权的数据面操作

- 通过显式运行配置连接；
- 对 `rtmp-monitor/v1/...` 执行精确订阅；
- 发布 session、Offer、Answer、candidate、ACK、presence 等新协议消息；
- 正常取消订阅和断开；
- 按协议使用 QoS/expiry；SDP、ICE 和 control command 永不 retained，只有协议明确允许的非敏感
  presence/capabilities 可使用 retained。

## 未授权的运维操作

- 不登录管理后台执行写操作；
- 不创建或修改 listener、用户、ACL、认证、插件、限额或集群配置；
- 不删除或改写 Broker 中其他团队业务的 retained/session 数据；
- 如实现确需上述任一操作，必须暂停并取得新的明确授权。

## 持续红线

- 仓库只写 `<team-public-mqtt-broker>` 等占位符，不记录真实 IP、完整 URL、端口组合或凭据；
- 软件默认网络关闭，Broker hostname/port/credential 为空，不自动连接；
- 真实 endpoint 只由 Git 外本机或受控部署配置显式注入；
- signaling 与 legacy control 使用独立连接、ClientId、topic、队列和状态机；
- `device/control`、`device/status` 不承载 WebRTC 信令；
- 日志不得输出完整 topic 身份列表、SDP、candidate、nonce、credential 或原始 payload。

## DIRECT-02 的准确验收表述

若两个真实进程通过该 Broker 完成自动信令，只能记为
`passed(plaintext_team_broker)` 或等价的明确范围状态；它证明当前团队公网 MQTT 功能链路可用，不证明
MQTTS/TLS/Auth/ACL 安全资格。TLS 和正式 Broker 加固是可选未来工作，不阻塞当前团队产品研发。
