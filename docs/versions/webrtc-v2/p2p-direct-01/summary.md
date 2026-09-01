# P2P-DIRECT-01 阶段摘要

## 实施结果

- `rtmp_monitor_identity_contracts` 提供五类 opaque ID、四类 canonical UUIDv4 和固定 MQTT ClientId codec。
- `rtmp_monitor_signaling_contracts` 提供固定 `rtmp-monitor/v1` source-bound Topic codec、理论 ACL、严格
  envelope/message policy，以及 TTL、ReplayGuard、TombstoneStore、AckTracker、Session/DeviceAgent
  transition 和无缓存 candidate policy。
- `rtmp_monitor_runtime_config` 冻结安全默认值：`enabled=false`、Broker hostname/port/CA/credential 为空、
  protocol=5；view-only roster 禁止 control ClientId/credential reference。
- 标准库-only Go CLI `rtmpmonitor-provision` 支持 `device create`、`pair create` 和 `validate`。artifact 只含
  credential reference；`secretProvisioning`、rotation 和 revoke 执行状态均固定为 `deferred`。
- C++/Go 共用 `contracts/signaling/v1` 的 envelope 与 ACL vectors。真实端点、credential、业务 payload、
  legacy topic 和 wildcard ACL 均不进入 fixture。

## 产品行为

本阶段没有界面改动、菜单入口、Broker 连接、网络线程、Paho callback、WebRTC transport 或运行时
candidate buffer。现有 RTMP、WebRTC 文件信令、legacy MQTT control/status 和默认离线行为不变。

## 架构影响

- 风险等级：代码为 R2 纯契约/CLI；DIRECT-00 解锁是用户确认后的 R3 范围决定。
- 职责：identity 拥有标识校验与 ClientId；signaling contracts 拥有 wire/topic/时间/幂等/迁移纯策略；
  runtime-config 拥有非敏感配置不变量；Go provisioning 拥有离线 artifact 生成、校验和原子写入。
- 依赖：`signaling_contracts -> identity_contracts`，Qt Core 仅 PRIVATE 用于 JSON；
  `runtime_config -> identity_contracts` PRIVATE；Go 仅标准库。产品、media、render、transport、control 和 UI
  均不依赖这些新目标。
- 生命周期：没有线程或网络资源。Replay/tombstone/ACK/candidate 状态由调用者按值持有且容量有界；CLI
  临时文件在成功 rename 或失败清理后结束。
