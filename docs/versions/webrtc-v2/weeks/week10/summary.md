# WebRTC V2 Week 10：性能、发布包、跨平台与 Beta 资格

> 完成日期：2026-08-31
>
> 版本：`0.2.0-beta.1` 资格候选；未创建正式标签、未推送。
>
> 最终门禁必须同时区分本机软件资格与外部环境资格，详见 `test_results.md`。

## 1. 本周完成范围

Week 10 在不改变 WebRTC 媒体、MQTT 控制、schema v1 或默认网络关闭策略的前提下，补齐了本机性能
采样、Windows 四构建矩阵、候选包和 ARM64 交叉构建入口。`LatestFrameMailboxStats` 与
`StreamMetrics` 只读追加内部延迟 P50 和 max，既有 P95 保留；诊断 JSON 同步增加同名字段。

测试专用 `rtmp_monitor_webrtc_qualification_runner` 只在 `BUILD_TESTING=ON` 且 WebRTC ON 时构建。
它从既有 1280×720@30、Baseline Level 3.1、零 B 帧、GOP 30 样本读取一轮有界 AU，循环复用不可变
码流并生成 33,333 微秒单调时间戳。单路和四路都经过真实 PeerConnection、RTP、Annex-B、FFmpeg
解码、容量 1 mailbox 和 presented 链路；四路场景在第 10 分钟停止一路、第 12 分钟使用最低空闲 slot
重建，同时检查第五路拒绝、旧端口失效、其余三路连续呈现和 10 秒内恢复 Direct。

父进程每秒独立采集进程 CPU 与工作集。CPU 只记录 mean/P95/max；正式内存门槛是线性斜率不超过
2 MiB/min，末 60 秒均值相对首 60 秒增长不超过 64 MiB。逐路 JSONL 只输出可归属的状态、generation、
呈现增量、队列、丢弃、内部延迟和显示间隔；现有画布的上传/绘制 CPU 与 texture bytes 是共享 renderer
指标，单独以 `scope=shared_renderer` 输出，不伪装成精确逐路值。结果不输出信令、地址、端口、UUID、
设备标识或绝对路径。

## 2. Windows 构建、OFF 行为与候选包

源码版本集中为 `0.2.0-beta.1`。`qualify_week10.ps1` 提供 `Check`、`SelfTest`、`Run`、
`Performance`、`Status`、`Stop`、`Package`、`Arm` 和 `Finalize`。`Run` fresh 构建 Debug/Release ×
WebRTC OFF/ON，并动态读取 CTest 数量。OFF 构建还审计相关 target、测试、二进制、菜单入口和版本探针
副作用。

候选包复用正式 Windows 打包器，再加入同版本 publisher/viewer 测试客户端、固定样本、qoffscreen、
datachannel/juice/SRTP/OpenSSL 运行 DLL及许可证。manifest 只记录版本、Git source commit、相对路径和
大小；不生成 SHA-256 或任何内容哈希。包中不包含 PDB、LIB、测试 EXE、日志、用户状态、local-config、
会话包或真实端点。两个全新展开副本均须在收敛 PATH 下通过主程序版本、客户端帮助/非法参数、两种本地
信令角色闭环、敏感输出和残留进程检查。

## 3. ARM 与资格边界

ARM64 只重新执行 RASTER/GLES3 的 WebRTC OFF 交叉构建、AArch64 ELF 和动态依赖审计。当前 sysroot
没有已资格的 libdatachannel，因此交叉构建成功也只能记录 `armCrossBuildPassed=true`；固定
`armWebRtcQualified=false`、`armDeviceQualified=false`。

本机四路是真实 WebRTC 软件链路，但不是两台物理 LAN 设备、四台物理 endpoint 或公网资格。未获得
摄像头 index 授权，本轮不枚举或打开物理摄像头。全部自动项通过后，最终状态仍是
`W10-GATE=blocked(camera_environment,physical_lan_environment)`；只有后续真实通过这两项，才允许
创建正式 `v0.2.0-beta.1` 标签。

面向“当前电脑发送、公司电脑接收”的首次人工操作，使用
[从零双电脑 P2P 视频测试手册](manual_two_computer_p2p_video_test.md)。手册先用包内已验证 sample
隔离网络与信令，再说明如何只在发送端替换自定义 H.264 MP4；不会把物理双电脑结果预写为通过。

## 4. 实际资格结果

fresh Windows 四矩阵全部通过：Debug/Release WebRTC OFF 为 39/39、39/39，Debug/Release ON 为
49/49、49/49；OFF 目标、菜单、制品和启动副作用审计通过。正式单路 600 秒和四路 1,800 秒测量均
通过本机门槛：工作集斜率分别为 0.176、0.134 MiB/min，末 60 秒相对首 60 秒分别增长 1.678、
2.802 MiB。四路停止、最低 slot 重建、旧端口失效、其余三路连续呈现和 10 秒恢复门禁全部通过。

Windows 候选包 `RtmpMonitor-0.2.0-beta.1-windows-x64` 包含 53 个文件；两个全新展开副本均通过
版本、CLI、本地两角色闭环、敏感输出、调试制品和残留进程审计。包的 `sourceCommit` 为
`392d9aa`，manifest 只有版本、source commit、相对路径和大小，没有内容哈希。ARM64 RASTER/GLES3
WebRTC OFF 交叉构建、AArch64 ELF 和动态依赖审计通过；它不代表 ARM WebRTC 或真机资格。

因此 `sameMachineSoftwareQualified=true`、`localPerformanceQualified=true`、
`armCrossBuildPassed=true`、`packagePassed=true`。W9-RES-01 已关闭，W9-GATE 收敛为
`blocked(camera_environment)`；物理摄像头和物理 LAN 均未执行，最终
`W10-GATE=blocked(camera_environment,physical_lan_environment)`。本轮未创建正式标签、未推送。

## 5. 架构影响

- 风险等级：R2。
- 职责：生产 media 只增加已有统计对象的只读分位值；资格编排位于测试组合根，不进入产品 runtime。
- 依赖：保持 `application → webrtc_product → runtime/media/ui`；publisher、transport、media 仍是
  兄弟模块。WebRTC OFF 不生成 runner 或 WebRTC 产品目标。
- 所有权：runner 拥有样本循环、发送端口、单个 pacing worker、产品 controller 和临时交换根；停止时
  先终止 pacing，再关闭逐路 endpoint/controller，最后执行一次全局 cleanup。
- 契约：延迟字段为 additive；H264、控制、持久化、RTMP、信令和网络默认行为不变。
