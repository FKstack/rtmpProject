# WebRTC V2 Week 9 测试指南

## 1. 自动技术矩阵

入口：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  scripts\webrtc\qualify_week9.ps1 -Action Check
powershell -NoProfile -ExecutionPolicy Bypass -File `
  scripts\webrtc\qualify_week9.ps1 -Action SelfTest
powershell -NoProfile -ExecutionPolicy Bypass -File `
  scripts\webrtc\qualify_week9.ps1 -Action Run
```

`Run` 复用已验证的资格公共模块，fresh 构建 Debug OFF、Debug ON、Release ON，并从 CTest 动态读取
测试数量。自动测试不会调用 `--list-cameras`，也不会打开物理摄像头。摄像头组件测试使用不进入公共
target include 的私有 worker seam 模拟索引、打开失败、设备消失、阻塞读取中断和重复停止，并验证
能力策略、时间戳、Annex-B 恢复及合成 NV12 的真实 `h264_mf` 编码/解码预检。

## 2. 30 分钟 Smoke

先完成 `Run`，再启动、查询或安全停止：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  scripts\webrtc\qualify_week9.ps1 -Action Smoke
powershell -NoProfile -ExecutionPolicy Bypass -File `
  scripts\webrtc\qualify_week9.ps1 -Action Status
powershell -NoProfile -ExecutionPolicy Bypass -File `
  scripts\webrtc\qualify_week9.ps1 -Action Stop
```

默认 Smoke 使用 Release ON 四路集成测试，1 Hz 采样 1,800 秒；第 10 分钟停止一路，第 12 分钟重建。
当前输入是小型 fixture，脚本只采集进程 CPU/工作集，C++ 只在结束时断言 queue；它没有形成代表性
720p30 负载或全程逐路资源峰值，因此只能产生 `lifecycleEndurancePassed`，不能关闭 W9-RES-01。

Week 9 原脚本的 `smokePassed` 始终为 false；`performanceQualified` 也永远为 false。Status 只显示进程角色
和 PID。内部状态保存 PID/启动时间，Stop 再从受管 build root/PowerShell 角色解析预期可执行路径，并在
每次停止前重新核对 PID、路径和启动时间；先停 product，再停 worker。终态与指标不会被 Stop 覆盖。

开发者可用 `-SmokeDurationSeconds 20` 做脚本短测，但短测只能得到 `self_test_passed`，不能得到
`smokePassed=true`。W9-RES-01 已由 Week 10 的代表负载 runner 正式补测关闭；复现时使用
`qualify_week10.ps1 -Action Performance`，不要再把 Week 9 fixture smoke 当成性能资格入口。

## 3. 真实摄像头资格

只有设备所有者显式授权并给出本次运行 index 时才能执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  scripts\webrtc\qualify_week9.ps1 -Action Camera -CameraIndex <index>
```

当前脚本在缺少已授权的配对 receiver 编排时会明确返回
`W9-GATE blocked(camera_environment)`，不会用 MP4、fixture 或合成 NV12 冒充 CAM-09。正式资格需要
预热 20 秒并连续呈现 120 秒，记录的设备只能是 `camera-N` 别名；不得复制设备名称、symbolic link、
序列号、帧、截图或裸码流到结果。

## 4. 结果判读

- 四路同机 PeerConnection 通过：只证明软件隔离，`physicalFourEndpointClaimed=false`。
- Camera action 未实际完成：CAM-09 与 W9-GATE 必须 blocked。
- Week 9 原 runner 的 1,800 秒结果不构成性能资格；当前权威资源结果来自 Week 10 runner。
- 任何报告都不得把进程 CPU/内存包装成精确逐路 OS 资源；逐路只报告可归属 queue/drop、上传/绘制
  CPU、texture/queue bytes。
