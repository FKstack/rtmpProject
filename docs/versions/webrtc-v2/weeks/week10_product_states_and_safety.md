# V2 Week 10：产品状态、事件与安全语义

## 本周目标

把底层 WebRTC 事实转化为操作者可理解的状态，同时保持现有车辆控制安全语义。

## 知识

- transport state、ICE state、selected path 和真实呈现帧是不同层级事实。
- Connected 不等于正在解码或画面新鲜。
- 自动协议回退会掩盖故障和安全证据。

## 实验

- 在 Connecting、Connected 无帧、Direct、Relayed、重连和错误间逐项注入状态。
- 断开媒体但保留信令，验证真实呈现媒体新鲜度按 100 ms 门槛失效。

## 开发任务

- 视频卡显示 Connecting、Server、Direct、Relayed、Reconnecting、Error。
- 事件中心增加 WebRTC 建连失败/恢复、TURN 回退和媒体服务器不可用。
- 将“RTMP 新鲜度”重命名为协议无关真实呈现媒体新鲜度。
- 只有档案配置 RTMP 回滚地址且用户确认时才切换，并记录事件。
- MQTT `startStream` 保持旧 RTMP 设备契约，不生成 WebRTC 命令。

## 验收

- 状态来源可追溯，不根据配置猜测 Direct/Relayed。
- WebRTC 无帧时移动控制被现有 100 ms 门槛阻止，StopCar 语义保持。
- 无静默 RTMP 回退；确认/取消路径和事件记录通过。
- 事件、日志和 UI 不显示敏感会话数据。

## 风险与停止条件

- 不能用 PeerConnection Connected 代替“真实画面新鲜”。
- MQTT payload、Topic、QoS 或设备返回契约发生变化时按独立 R3 任务处理。

## 下周入口

执行服务器 1/4/8 路和 P2P 1/4 路的故障与资源矩阵。
