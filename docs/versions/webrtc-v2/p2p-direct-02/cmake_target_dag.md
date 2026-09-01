# P2P-DIRECT-02 CMake target DAG

以下关系来自 fresh WebRTC-ON CMake Graphviz 输出，只保留 target 名，不记录绝对路径：

```text
rtmp_monitor
├─ rtmp_monitor_device_control
│  └─ rtmp_monitor_mqtt_transport
│     └─ paho-mqtt3a                         [PRIVATE]
├─ rtmp_monitor_mqtt_signaling
│  ├─ rtmp_monitor_mqtt_transport            [PRIVATE]
│  ├─ rtmp_monitor_identity_contracts         [PRIVATE]
│  ├─ rtmp_monitor_signaling_contracts        [PRIVATE]
│  ├─ rtmp_monitor_signaling_channel
│  └─ rtmp_monitor_signaling_session
│     ├─ rtmp_monitor_signaling_channel
│     └─ rtmp_monitor_signaling_contracts
├─ rtmp_monitor_webrtc_product
│  ├─ rtmp_monitor_media
│  └─ rtmp_monitor_ui
└─ rtmp_monitor_ui
   ├─ rtmp_monitor_device_control
   ├─ rtmp_monitor_render
   ├─ rtmp_monitor_event_center
   └─ rtmp_monitor_evidence

rtmp_monitor_direct_device_harness            [WIN32 + BUILD_TESTING + WebRTC ON]
├─ rtmp_monitor_mqtt_signaling
└─ rtmp_monitor_signaling_session
```

`CheckLayerDependencies.cmake` 通过。media、render、UI、control 均未反向依赖 signaling；具体 adapter
只在应用组合根或 Harness 组装。
