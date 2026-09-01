# P2P-DIRECT-00 阶段摘要

> 日期：2026-09-01
> 基线：`Beta` / `23c0949`
> 风险：R2
> 当前状态：`in_progress`

## 范围

本阶段冻结 MQTT TLS signaling 与 legacy MQTT control 的边界，建立测试专用 Broker fixture、CMake
依赖图和分层门禁，并对隔离产品 Broker 候选执行资格测试。现有远程明文服务仅允许兼容性观察，不是
产品候选，也不得成为源码或配置默认值。

## 行为不变量

- `MqttConnectionOptions.enabled=false`、Broker 地址为空；
- `device/control` 与 `device/status` 保留为嵌入式 control/status legacy 契约；
- 新 signaling 不复用 legacy topic、客户端、队列或状态机；
- RTMP、WebRTC 文件信令、SavedStream v1、媒体、UI 和默认启动行为不变；
- 不登录或修改现有 Broker，不发布任何 legacy 测试消息。

## 架构影响

- 测试 fixture 独立拥有 Paho handle、回调状态、超时和关闭，不进入产品对象图；
- Paho TLS 仅作为 fixture 的 PRIVATE 依赖；
- 分层脚本预设后续 identity/signaling/session 路径的单向依赖；
- 只有隔离候选完成 TLS/Auth/ACL/expiry/limit 门禁后，本阶段才可能通过。
