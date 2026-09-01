# P2P-DIRECT-02 实施总结

## 结果

`P2P-DIRECT-02=passed(plaintext_team_broker)`。WebRTC/MQTT Direct 在本阶段只替换手工信令底座，
没有创建第二套桌面产品。

## 实际实现

- `MqttAsyncTransport` 成为唯一 Paho MQTTAsync 生命周期实现。legacy `MqttDeviceClient` 保留原 API、
  `mqtt-control.json`、`device/control`、`device/status`、MQTT 3.1.1/QoS0、4 KiB 截断和 drop-oldest；
  signaling 使用另一实例、MQTT5/QoS1、expiry 和 fail-on-overflow。
- `ISignalingChannel`、`DirectOperatorCore`、`DirectDeviceCore` 保持 broker-neutral。Core 复用 DIRECT-01
  Topic/Envelope/TTL/ReplayGuard/AckTracker/状态迁移，`session.request`/`session.cancel` 分别表示
  START_STREAM/STOP_STREAM，不替换小车控制指令。
- `MqttSignalingChannel` 只通过共享 transport 连接 Broker。Paho 类型仅在 transport `.cpp`，CMake
  Graphviz 证明确为 PRIVATE 边。
- WebRTC-ON 的现有 `rtmp_monitor.exe` 是唯一 Operator 产品进程。只有显式提供 Git 外
  `--direct-config` 才创建 signaling 连接；OFF 构建拒绝 DIRECT 参数。
- `rtmp_monitor_direct_device_harness` 仅在 Windows、BUILD_TESTING、WebRTC-ON 生成，只替换未来设备
  runtime shell；协议、TTL、去重、状态机和 ACK 均在 Direct Core。
- signaling 连接状态经现有 `PlatformEventBridge` 投递到 EventCenter 的独立资源
  `transport:mqtt-signaling`。malformed/TTL/replay 不生成事件风暴。

## UI 与产品连续性

默认启动和可见界面没有变化。唯一主窗口仍是 `MainWindow`；上下左右、停车、Start/StopStream、
键盘、摇杆、固定 MQTT 指令、动态网格、OpenGL/CPU、全屏、截图、EventCenter 和 Evidence 均继续
使用原实现。DIRECT-02 没有真实媒体绑定，因此也没有伪造可截图视频资源或新增自动事件截图。

## 架构影响

- 风险等级：R2；团队 Broker 产品范围沿用用户已确认的 ADR-048 R3 决定。
- 职责：Paho 生命周期从 `MqttDeviceClient` 下沉到共享 transport；legacy façade 只保留控制契约；
  Direct Core 拥有协议状态，组合根只负责装配和稳定值事件映射。
- 依赖：`device_control -> mqtt_transport`；`mqtt_signaling -> mqtt_transport + signaling contracts/core`；
  应用组合根依赖具体实现。media/render/UI 不依赖 signaling。
- 生命周期：control/signaling 为两个 handle、ClientId、订阅、epoch、队列和 shutdown；应用退出先停止
  signaling，再沿用原 control/event/media 关闭顺序。
- 兼容：legacy MQTT 字节、topic、UI 与默认离线行为不变；新运行配置只接受显式 tcp/anonymous
  MQTT5 配置，真实 endpoint 不进入默认值或 Git。
