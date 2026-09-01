# CMake target DAG 基线

本文件只保存 target-to-target 关系，不保存生成器、工作区或第三方安装绝对路径。实际 OFF/ON fresh
Graphviz 输出位于忽略目录，完成四矩阵后从真实配置更新本页结果。

## 依赖不变量

```text
application -> app/ui/media/render/device_control
application -> webrtc_product                 (WebRTC ON only)
webrtc_product -> runtime/media/video_canvas  (组合层单向装配)
webrtc_runtime -> transport/contracts
transport/publisher/media -> h264 contracts   (兄弟模块)
diagnostics -> media/render                    (只读)
broker fixture -> paho-mqtt3as                 (WIN32 + BUILD_TESTING only)
```

禁止 `media -> render/ui/signaling`、`render -> ui/signaling`、`device_control -> media/render/ui/signaling`、
`transport -> product/media/ui` 和任何生产 target 对 broker fixture 的依赖。
