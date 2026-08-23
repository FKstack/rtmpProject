# WebRTC V2 Week 5 测试指南

## 这周要验证什么

Week 5 的核心不是“两个 PeerConnection 能连上”，而是同一个客户端在 publisher/viewer、
Offerer/Answerer 任意正交组合下，viewer 都能把真实 RTP 解码并呈现在现有 CPU 画布。测试分为
自动脚本版和人工桌面版；前者已通过，后者留给用户确认动态画面和窗口操作感受。

## 版本 A：自动化脚本

### 前置条件

- Windows、Visual Studio C++、Qt、vcpkg/libdatachannel 和 FFmpeg 已按项目开发环境安装。
- 在仓库根目录打开 PowerShell。
- 将下面的占位符替换为本机目录；不要把个人绝对路径提交到文档或配置。

### 快速检查

```powershell
& scripts/webrtc/qualify_week5.ps1 -Action SelfTest
& scripts/webrtc/qualify_week5.ps1 -Action Check `
  -QtRoot '<qt-root>' `
  -VcpkgRoot '<vcpkg-root>' `
  -VsDevCmd '<visual-studio>\Common7\Tools\VsDevCmd.bat'
```

期望两条命令分别输出 `self-test passed` 和 `prerequisites passed`。

### 完整资格测试

```powershell
& scripts/webrtc/qualify_week5.ps1 -Action Run `
  -QtRoot '<qt-root>' `
  -VcpkgRoot '<vcpkg-root>' `
  -VsDevCmd '<visual-studio>\Common7\Tools\VsDevCmd.bat'
```

脚本会执行：

1. fresh WebRTC OFF 配置、全构建与 39 项 CTest；
2. 生成并验证 6 秒、1280×720、30 fps、Constrained Baseline H.264 MP4；
3. fresh WebRTC ON 配置、全构建与 44 项 CTest；
4. 检查 help、非法参数、缺样本和 Qt 平台插件；
5. 运行 publisher/Offerer ↔ viewer/Answerer，两轮；
6. 运行 viewer/Offerer ↔ publisher/Answerer，两轮；
7. 检查 RTP、AU、decoded、rendered、presented、退出与零残留。

成功终态是：

```text
Week 5 automated qualification passed.
```

运行期间可以在另一个 PowerShell 查看或停止受管任务：

```powershell
& scripts/webrtc/qualify_week5.ps1 -Action Status
& scripts/webrtc/qualify_week5.ps1 -Action Stop
```

`Stop` 只终止状态文件中 PID、完整 executable 路径和启动时间都匹配的进程。停止后再次执行
`Status` 应显示 idle，交换目录和资格状态不应残留。

### Week 4 兼容回归

```powershell
& scripts/webrtc/qualify_week4.ps1 -Action SelfTest
& scripts/webrtc/qualify_week4.ps1 -Action Run `
  -QtRoot '<qt-root>' `
  -VcpkgRoot '<vcpkg-root>' `
  -VsDevCmd '<visual-studio>\Common7\Tools\VsDevCmd.bat'
```

期望最后输出 `Week 4 automated qualification passed.`。这证明公共脚本模块和新客户端仍兼容 Week 4
的 publisher/测试 peer 流程。

## 版本 B：人工桌面测试

完整脚本会在 `out/build-windows-x64/week5/on/webrtc/Debug/` 生成客户端及本地资格样本。人工测试时
不要设置 `QT_QPA_PLATFORM=offscreen`。

### 场景 1：publisher 发 Offer

终端 A：

```powershell
& out/build-windows-x64/week5/on/webrtc/Debug/rtmp_monitor_webrtc_client.exe `
  --media-role publisher --signaling-role offer --source sample --timeout-ms 30000
```

观察到 `description_exported` 后启动终端 B：

```powershell
& out/build-windows-x64/week5/on/webrtc/Debug/rtmp_monitor_webrtc_client.exe `
  --media-role viewer --signaling-role answer --timeout-ms 30000
```

### 场景 2：viewer 发 Offer

终端 A：

```powershell
& out/build-windows-x64/week5/on/webrtc/Debug/rtmp_monitor_webrtc_client.exe `
  --media-role viewer --signaling-role offer --timeout-ms 30000
```

观察到 `description_exported` 后启动终端 B：

```powershell
& out/build-windows-x64/week5/on/webrtc/Debug/rtmp_monitor_webrtc_client.exe `
  --media-role publisher --signaling-role answer --source sample --timeout-ms 30000
```

### 每个场景的观察项

| 检查项 | 期望结果 | 实际记录 |
| --- | --- | --- |
| 启动 | 无 Qt platform plugin 弹窗 | 待用户填写 |
| 画面 | viewer 显示动态 1280×720 测试画面，无持续黑屏 | 待用户填写 |
| 窗口 | 拖动、缩放、最小化和恢复均响应 | 待用户填写 |
| 事件 | viewer 依次出现 connected、media_received、frame_decoded、frame_presented、completed | 待用户填写 |
| viewer 先关 | publisher 有界退出，无残留弹窗/进程 | 待用户填写 |
| publisher 先关 | viewer 在 timeout 内收敛，无卡死 | 待用户填写 |
| 重复运行 | 每种拓扑至少 3 轮均可重新连接出画 | 待用户填写 |

人工关闭一端后，可执行 `qualify_week5.ps1 -Action Stop` 清理受管资格进程；手工直接启动的进程不在
资格状态文件中，应在对应窗口或终端正常退出。

## 常见问题

- 弹出 Qt platform plugin 错误：确认 executable 同级 `platforms/` 下有与 Debug/Release 匹配的
  `qwindows`，重新构建客户端；不要通过重装 Qt 掩盖部署目录错误。
- viewer 报 `media_timeout`：确认两端使用相反信令角色、publisher 有固定样本，并从交换目录为空的
  状态重试。
- publisher 报 `file_not_found`：先运行 Week 5 `Run` 生成资格样本，或按 Week 4 资产边界准备本地
  样本；不要提交 MP4。
- 有旧交换文件或受管进程：执行 `-Action Stop`，再执行 `-Action Status`；不要手工删除不属于本次
  资格任务的进程。
- 只有 `connected` 没有 `frame_presented`：这不是通过。查看 endpoint/decoder/mailbox/canvas 四层
  稳定 JSONL 证据，禁止用 ICE 状态替代画面证据。
