# P2P-DIRECT-02 RTMP 产品功能复用 ledger

| 功能 | Owner / 唯一实现 | DIRECT-02 状态 | 证据 |
| --- | --- | --- | --- |
| 上下左右、停车按钮 | `DeviceControlPanel` + `DeviceControlController` | 保持 | `DeviceControlPanelTest`、`DeviceControlControllerTest` |
| 键盘与摇杆 | `DeviceControlInputRouter` + `VirtualJoystickWidget` | 保持 | 对应 CTest |
| 固定 MQTT 指令与 legacy topic | `DeviceCommandCodec` + `MqttDeviceClient` façade | 保持 | `DeviceControlContractTest`、`MqttDeviceClientTest` |
| Start/StopStream legacy 行为 | 现有 controller/codec | 保持；DIRECT session 不替换 | legacy MQTT 回归 + Direct Core Test |
| mailbox 与播放管理 | `MultiStreamPlaybackManager` | 保持 | `MultiStreamPlaybackManagerTest` |
| OpenGL/CPU canvas | `VideoCanvasHost`/既有 canvas | 保持 | render/OpenGL smoke、ARM 双构建 |
| 动态网格 | `VideoGridWidget` | 保持 | `VideoGridDynamicTest` |
| 全屏与全屏截图 | 现有 fullscreen classes/service | 保持 | UI/退出 CTest；未新增截图系统 |
| 事件列表、详情、确认/恢复/关闭 | `EventCenterService/Panel` | 保持 | EventCenter CTest |
| 事件证据截图与导出 | `EvidenceCoordinator/Service` | 保持用户触发 | Evidence/EventCenterPanel CTest |
| signaling 连接事件 | `PlatformEventBridge` | 新增稳定值适配 | `transport:mqtt-signaling` 独立资源测试 |
| 自动事件截图 | 无现有实现 | 非目标 | DIRECT-02 未新增 |
| 真实 Direct 视频截图 | 需真实媒体绑定 | DIRECT-04 前未实现 | 不伪造视频资源 |

Owner：现有模块 owner。目标阶段：真实媒体/trickle 为 DIRECT-03，单桌面正式 roster 与现有 tile 映射为
DIRECT-04，其余项目已在 DIRECT-02 回归通过。
