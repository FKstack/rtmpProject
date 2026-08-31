# WebRTC V2 Week 10 测试指南

## 1. 前置与自检

从仓库根目录执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  scripts\webrtc\qualify_week10.ps1 -Action Check
powershell -NoProfile -ExecutionPolicy Bypass -File `
  scripts\webrtc\qualify_week10.ps1 -Action SelfTest
```

脚本从已配置 build cache 解析 Qt/vcpkg，也允许显式传入本机路径。固定样本默认位于已忽略的 Week 7
资格资产目录。自动动作不枚举或打开真实摄像头，不使用真实公网端点。

## 2. 四套 fresh Windows 矩阵

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  scripts\webrtc\qualify_week10.ps1 -Action Run
```

`Run` 顺序执行 Debug OFF、Release OFF、Debug ON、Release ON，全目标构建后运行完整 CTest；测试总数
从 CTest 动态读取。Qt GUI 测试使用随 target 部署的同配置 MSVC runtime 和 Windows 平台插件，避免
从开发机 PATH 误载另一套 Qt。OFF 审计要求无 WebRTC target、测试或运行 DLL，版本探针不创建交换目录。

## 3. 本机性能与 Week 9 资源补测

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  scripts\webrtc\qualify_week10.ps1 -Action Performance
powershell -NoProfile -ExecutionPolicy Bypass -File `
  scripts\webrtc\qualify_week10.ps1 -Action Status
powershell -NoProfile -ExecutionPolicy Bypass -File `
  scripts\webrtc\qualify_week10.ps1 -Action Stop
```

正式动作先运行单路预热 60 秒、采样 600 秒，再运行四路预热 60 秒、采样 1,800 秒；四路在采样第
600 秒停止 slot 2，第 720 秒重建。只有精确时长才会形成 `sameMachineSoftwareQualified=true`。
开发者可以显式缩短各时间参数做编排自检；短测状态只会是 `self_test_passed`，不应用正式内存阈值，
也不能关闭任何资格门禁。逐路 JSON 只包含可归属指标；画布 upload/paint/texture 以
`sharedRenderer.scope=shared_renderer` 明确标记为共享统计。

`Status` 不显示路径；内部状态记录 PID、可执行路径和启动时间。`Stop` 先写 stop request 让 runner
正常收敛，只有超时后才针对身份仍完全匹配的受管进程执行有界终止。进程 CPU 是总量，不伪装成逐路
OS CPU；逐路只使用链路自身可归属指标。

## 4. Windows Beta 候选包

先确保 Week 10 源码已经形成可追溯本地 commit，再执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  scripts\webrtc\qualify_week10.ps1 -Action Package
```

输出只是资格候选，不是正式发布。脚本构建 fresh Release ON，复用正式主程序打包器，然后审计精确
WebRTC DLL/许可证、调试物、敏感内容和可执行文件集合。两个干净展开副本在收敛 PATH 下完成主程序
`--version`、客户端帮助/非法参数以及 publisher Offerer + viewer Answerer 本地闭环。manifest 不包含
内容哈希。

## 5. ARM 与最终状态

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  scripts\webrtc\qualify_week10.ps1 -Action Arm
powershell -NoProfile -ExecutionPolicy Bypass -File `
  scripts\webrtc\qualify_week10.ps1 -Action Finalize
```

`Arm` 只证明 WebRTC OFF 的 ARM64 RASTER/GLES3 交叉构建与 ELF 依赖。`Finalize` 分别合并四矩阵、本机
性能、候选包和 ARM 结果，再保留摄像头与物理 LAN 外部阻塞；不会因同机 P95 或交叉构建而伪造现场
资格，也不会创建 tag 或推送远端。

## 6. 当前电脑发送、公司电脑接收的人工 P2P 测试

需要把本机 MP4 投到另一台 Windows 电脑时，按
[Week 10 从零双电脑 P2P 视频测试](manual_two_computer_p2p_video_test.md)执行。该手册覆盖完整 ZIP
展开、同包核对、host/STUN 选择、Offer/Answer 安全搬运、接收端可视证据、自定义 MP4 替换、清理和
故障定位。跨网络测试必须使用获授权 STUN；当前版本没有 TURN，不能把需要 Relay 的环境冒充通过。
