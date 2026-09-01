# P2P-DIRECT-00 阶段摘要

> 日期：2026-09-01
> 基线：`Beta` / `23c0949`
> 风险：R2
> 当前状态：`passed(scope_reduced_by_user_decision)`

## 范围

本阶段冻结 MQTT TLS signaling 与 legacy MQTT control 的边界，建立测试专用 Broker fixture、CMake
依赖图和分层门禁，并为隔离产品 Broker 候选保留可选资格测试。现有远程明文服务仅允许兼容性观察，不是
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
- 用户已取消隔离候选 TLS/Auth/ACL/expiry/limit 作为阶段前置；未执行不等于通过。

## 实际结果

- Windows fresh Debug/Release OFF 为 40/40、40/40，Debug/Release ON 为 50/50、50/50；新增项为
  Broker fixture self-test，OFF 版本、target、制品和启动副作用审计通过。
- fixture 离线配置/脱敏 self-test 通过；获授权 legacy 观察完成 MQTT 5 CONNECT、随机精确 topic
  SUBACK、250 ms 无 payload、UNSUBACK 和 DISCONNECT，全程无 publish。
- ARM64 RASTER/GLES3 WebRTC-OFF 交叉构建、AArch64 ELF 与动态依赖审计通过；fixture 未进入 ARM graph。
- 最终 OFF/ON DAG 分别包含 341/483 条规范化依赖边，fixture 只 PRIVATE 指向 `paho-mqtt3as`，未发现
  media/render/device_control/transport/runtime/publisher 到 product/signaling 的反向边。
- 本机和既有 WSL 均无 Docker、Podman、EMQX 或 Mosquitto 隔离运行时；未下载或临时部署 Broker，
  capability 负向矩阵保持未执行。按用户确认的范围缩减，本阶段通过并解锁 DIRECT-01。

## 架构影响

- 风险等级：R2。
- 职责：新增的 Windows 测试 fixture 独立拥有 Paho handle、回调、超时和关闭；产品类未增加职责。
- 依赖：新增 `fixture -> paho-mqtt3as` PRIVATE 边，TLS DLL 进入独立 `broker-fixture/<config>`；产品
  target、ARM 与 WebRTC OFF 主目录不继承它。
- 契约/生命周期：legacy topic、schema、默认离线和产品线程不变；fixture 固定 CONNECT→SUBSCRIBE→
  bounded observe→UNSUBSCRIBE→DISCONNECT→destroy。
- 未验证：隔离 EMQX/Mosquitto 的 TLS/Auth/ACL/retained/QoS/expiry/limit/恶意客户端矩阵。
