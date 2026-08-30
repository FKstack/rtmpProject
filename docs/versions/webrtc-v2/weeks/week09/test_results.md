# WebRTC V2 Week 9 测试结果

> 日期：2026-08-30
>
> 总结：实现阶段 fresh 三矩阵通过；P1 与 Windows Qt 本地运行时加固后，当前代码三矩阵全量 CTest
> 再次通过。真实摄像头与代表性 30 分钟资源资格未完成。

## 1. 已执行证据

| 范围 | 实际结果 |
| --- | --- |
| Debug ON 定向构建 | product test、camera source test、client 构建成功 |
| 定向 CTest | client CLI、四路 product、camera source、layer dependency：4/4，通过，9.82 秒 |
| product 单项 | 四路真实 PeerConnection、单路停止/其余增长、slot 重建：通过 |
| camera component 单项 | 原生能力/约束位/NAL/时间戳/SPS-PPS 恢复、`h264_mf` 合成编解码与 drain、失败后 stop、阻塞读取中断、waiter/stop 并发 join：P1 后 Debug/Release 均通过（0.20 秒） |
| product P1 单项 | signal 同步取消重入、动画期取消重试、远端关闭一路/其余持续、第五路 generation 零副作用：P1 后 Debug 9.78 秒、Release 9.36 秒，通过 |
| Release Qt Test 本地运行时 | `rtmp_monitor_webrtc_client_ice_config_test.exe` 的 target 目录 POST_BUILD 复制 MSVC `Qt6Test.dll`；清空全部 Qt/MSVC PATH、只保留系统 PATH 并设置 offscreen 后直接启动，退出码 0，无 MinGW 入口点弹窗 |
| QApplication 测试本地 Qt 部署 | 所有 `QTEST_MAIN`/手工 `QApplication` 测试按配置复制 MSVC Qt runtime 及 `qwindows`/`qoffscreen` 平台插件；清除 Qt 路径、PATH 仅保留系统目录并设置 offscreen 后，Debug ON 的 event-center/device-control panel 2/2（0.47 秒）、Release ON 2/2（0.18 秒）、Debug OFF 2/2（0.50 秒）均通过 |
| Week 9 SelfTest | Week 8 公共资格 self-test 与 Week 9 源码/CMake/脚本检查：通过 |
| fresh Debug OFF | 全目标构建成功，CTest 39/39，通过，122.82 秒 |
| fresh Debug ON | 全目标构建成功，CTest 48/48，通过，201.16 秒 |
| fresh Release ON | 全目标构建成功，CTest 48/48，通过，141.01 秒 |
| 最终当前代码全量 CTest | Debug OFF 39/39（122.66 秒）、Debug ON 48/48（201.71 秒）、Release ON 48/48（173.61 秒），全部通过且无 Qt 弹窗 |
| 短时 Smoke/Status/Stop | 3 秒 `self_test_passed`；敏感输出扫描通过；Status 不显示路径；Stop 保留终态与指标并追加 stopRequested/stopCompleted；无 product 残留 |

fresh 矩阵由 `qualify_week9.ps1 -Action Run` 创建 Week 9 专用 build tree，测试数量从 CTest 动态读取。
测试没有枚举或打开真实摄像头。

## 2. 门禁状态

| 项目 | 状态 | 说明 |
| --- | --- | --- |
| W9-CAM-01 | blocked(camera_environment) | 未授权枚举，未生成真实设备脱敏能力表 |
| W9-CAM-02～08 | implemented / component evidence | 生产 worker、原生优先/回退、时间戳、恢复和停止策略已接入；不等于物理 capture 验收 |
| W9-CAM-09 | blocked(camera_environment) | 未获显式真实摄像头授权 |
| W9-MUL-01 | targeted passed | 四组真实同机 PeerConnection 独立呈现 |
| W9-FLT-01 | targeted passed | 远端先关闭一路、另外三路继续增长；单路取消与重建也通过 |
| W9-RES-01 | partial | C++ 最终 queue 上限与清理通过；缺全程逐路资源峰值和代表性 720p30 负载 |
| 30 分钟资源 Smoke | not_run | 未等待 1,800 秒；当前 runner 只给 lifecycle 证据，`smokePassed=false` |
| W9-GATE | blocked(camera_environment,resource_smoke_not_run) | 不用 fixture 替代真实摄像头或资源资格 |

固定声明：`physicalFourEndpointClaimed=false`、`performanceQualified=false`、`cameraQualified=false`。

## 3. 未验证项

- 真实摄像头 1280×720@30 原生 H.264 或 NV12 capture；
- 真实设备消失、物理阻塞读取 Flush 收敛与 120 秒持续呈现；
- 1,800 秒工作集斜率和首末 60 秒增长门禁；
- 代表性 720p30 四路负载下全程逐路 queue/drop/upload/paint/texture/queueBytes 峰值；
- 四台物理 endpoint、真实 LAN/公网和 Week 10 性能资格。

这些项目不得从合成 `h264_mf`、同机 PeerConnection 或短时脚本结果外推。
