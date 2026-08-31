# WebRTC V2 Week 10 测试结果

> 日期：2026-08-31
>
> 当前结论：四套 fresh Windows 矩阵、正式本机长稳、Windows 候选包与 ARM64 交叉构建已通过；
> 摄像头和物理 LAN 环境资格保持阻塞。

## 1. Windows 自动矩阵

| 配置 | 实际 CTest | 结果 |
| --- | ---: | --- |
| Debug / WebRTC OFF | 39/39（118.28 秒） | 通过 |
| Release / WebRTC OFF | 39/39（91.53 秒） | 通过 |
| Debug / WebRTC ON | 49/49（199.56 秒） | 通过 |
| Release / WebRTC ON | 49/49（167.55 秒） | 通过 |

ON 比 Week 9 增加资格 runner self-test；OFF 仍保持 39 项。四套均从 fresh configure 开始，OFF 目标与
启动副作用审计通过。新增 P50/max、空样本、20,100 次有界样本、时间戳循环、参数错误和脱敏输出测试
均包含在当前矩阵中。

## 2. 性能与恢复

输入为既有 1280×720@30、Baseline Level 3.1、零 B 帧、GOP 30 固定样本；runner 循环复用一轮
180 个不可变 AU，只重建单调时间戳。单路和四路均为真实 PeerConnection→RTP→解码→mailbox→
presented 链路。

| 项目 | 单路 | 四路 |
| --- | --- | --- |
| 正式时长 | 预热 60 秒 + 测量 600 秒，通过 | 预热 60 秒 + 测量 1,800 秒，通过 |
| 工作集斜率 | 0.176 MiB/min，通过 | 0.134 MiB/min，通过 |
| 末 60 秒相对首 60 秒 | +1.678 MiB，通过 | +2.802 MiB，通过 |
| CPU mean/P95/max | 0.503% / 0.871% / 1.257% | 1.828% / 2.614% / 3.385% |
| 内部延迟 P50/P95/max | 18 / 33 / 37 ms | 四路 18 / 32～33 / 37 ms |
| queue 峰值 | transport 0；decode 0 | transport 0；decode 1 包 / 60,434 bytes |
| cleanup/脱敏扫描 | 通过 / 通过 | 通过 / 通过 |
| 停止/重建/旧端口/其余三路 | 不适用 | 第 600 秒停止、第 720 秒重建；780 秒请求、782 秒 Direct；全部通过 |

CPU 和工作集是 runner 进程总量，不是逐路 OS 资源。upload/paint/texture 是共享画布值，以
`sharedRenderer.scope=shared_renderer` 输出；逐路只记录可归属的状态、generation、呈现、队列、
丢弃、延迟和显示间隔。短时编排 self-test 另行通过，但不计为资格。

本机结果为 `sameMachineSoftwareQualified=true`、`localPerformanceQualified=true`。它关闭
W9-RES-01，使 W9-GATE 收敛为 `blocked(camera_environment)`；它不替代物理 LAN，故固定
`physicalLanQualified=false`、`performanceQualified=false`。

## 3. 包与 ARM

| 项目 | 状态 | 证据边界 |
| --- | --- | --- |
| Windows Beta 候选包 | 通过 | 53 个文件；`sourceCommit=392d9aa`；不等于正式发布，不创建 tag |
| 两个干净展开副本 | 2/2 通过 | 主程序版本、客户端 help/非法参数、本地角色闭环 2/2、敏感输出和残留检查通过 |
| ARM64 RASTER/GLES3 | 通过 | AArch64 ELF 与动态依赖已审计；WebRTC OFF |
| ARM WebRTC/真机 | blocked(environment/dependency) | `armWebRtcQualified=false`、`armDeviceQualified=false` |

## 4. 最终门禁边界

- 未获得摄像头 index 授权：`cameraQualified=false`，不枚举或打开真实设备。
- 未取得两台物理 LAN 环境：`physicalLanQualified=false`，同机延迟不写成 LAN P95。
- 同机四路不等于四台物理 endpoint：`physicalFourEndpointClaimed=false`。
- `performanceQualified` 表示全局现场性能资格，不能由 `sameMachineSoftwareQualified` 代替。
- 最终 `W10-GATE=blocked(camera_environment,physical_lan_environment)`；自动项已独立通过。
- `packagePassed=true`、`armCrossBuildPassed=true`；`armWebRtcQualified=false`、
  `armDeviceQualified=false`、`cameraQualified=false`、`physicalLanQualified=false`、
  `performanceQualified=false`。未创建正式标签、未推送。

## 5. 当前发送端中国大陆 STUN 预检

2026-08-31 在用户明确授权具体第三方目的地及公网 IP/UDP 映射元数据外发后，使用 Week 10 正式
候选客户端从当前发送端执行 5 轮真实 ICE gathering。服务属于中国大陆厂商公开提供的测试 STUN；
实际域名、公网 IP、映射端口、candidate 和 Offer 未写入仓库。每轮只验证配置读取、STUN Binding、
候选类型和 Offer 导出，不发送视频，也不连接公司电脑。

| 轮次 | 配置读取 | 候选类型 | STUN 观察 | 候选收集耗时 |
| ---: | --- | --- | --- | ---: |
| 1 | 通过 | host、srflx | srflx_observed | 747 ms |
| 2 | 通过 | host、srflx | srflx_observed | 486 ms |
| 3 | 通过 | host、srflx | srflx_observed | 569 ms |
| 4 | 通过 | host、srflx | srflx_observed | 476 ms |
| 5 | 通过 | host、srflx | srflx_observed | 459 ms |

汇总为成功率 5/5、P50 486 ms、均值 547.4 ms、P95/最大值 747 ms，五轮均在 1 秒内获得 srflx。
每轮最终退出码 3 是探针故意不提供 Answer、等待 3 秒后按信令缺失退出的预期结果，不影响 STUN
成功判定。STUN 不承载媒体，因此这些毫秒数不是视频 RTT、带宽或帧延迟；公司电脑 STUN 可达性、
双方 Direct、实际媒体吞吐和 viewer 出画仍必须按人工手册执行，W10-GATE 不因此关闭。
