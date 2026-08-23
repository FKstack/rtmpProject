# WebRTC V2 Week 6 实际测试结果

> 本页只记录实际执行事实。最终提交后的 fresh OFF/ON/Release、便携包与旧周回归将在资格运行完成后更新；真实双机项目必须由用户在第二台获准 Windows 电脑上执行。

## 当前环境

| 项目 | 实际值 |
| --- | --- |
| 日期 | 2026-08-23 |
| 系统 | Windows 11 22631，x64 |
| 编译器 | Visual Studio 2026 18.9.1，MSVC 19.51.36256 |
| Qt | 6.6.1 MSVC x64 |
| libdatachannel | 0.24.5，vcpkg x64-windows |
| FFmpeg | 8.1.2 工具；vcpkg FFmpeg 运行库 |
| 分支 | `Beta` |

## 已执行的增量验证

| 验证 | 实际结果 |
| --- | --- |
| CMake VS2026 ON 配置 | 通过 |
| `rtmp_monitor_webrtc_endpoint_test` | 通过；两种真实 RTP 拓扑、脱敏 selected pair、单端关闭后约 30 秒进入终态、迟到 sink 计数不增加 |
| `rtmp_monitor_webrtc_runtime_paths_test` | 通过；仓库、便携 marker 优先、无布局、重复解析和 sample 路径 |
| endpoint + runtime paths CTest | 2/2，61.07 秒 |
| Week 6 客户端目标编译 | Debug 通过 |
| PowerShell 5.1 parser | `QualificationCommon`、Week 5、Week 6 四个新文件共 6/6 通过 |
| Week 6 SelfTest | 通过 |
| `<qt-root>` 占位符负向检查 | Week 5/Week 6 均在 `Test-Path` 前返回明确替换提示 |
| 文档字符量 | summary 12,053 个中文字符；testing guide 8,022 个中文字符 |
| SVG | 6 张，XML 解析待最终 SelfTest 汇总 |

selected pair 的同机真实观察可能把远端 peer-reflexive 脱敏归类为 `srflx`。这项结果没有被改写成
`host`；同机自动化只接受安全类型与 UDP，真实双机门禁继续要求 `host/host + udp`。

## 最终矩阵状态

| 项目 | 当前状态 |
| --- | --- |
| fresh Debug WebRTC OFF 全构建/CTest | 待最终提交后执行 |
| fresh Debug WebRTC ON 全构建/CTest | 待最终提交后执行 |
| fresh Release WebRTC ON 全构建/CTest | 待最终提交后执行 |
| Release stage/ZIP/许可/manifest/包扫描 | 待最终提交后执行 |
| 同机两个独立包副本，两拓扑各 10 轮 | 待最终提交后执行；不得声明 LAN |
| Week 4/Week 5 SelfTest/Run | 待最终提交后回归 |
| W6-LAN-01/02/03、W6-LIF-01 | 等待用户真实双机执行 |
| W6-GATE | 等待四份双机报告与生命周期人工材料 |

## 未覆盖范围

没有第二台获准 Windows 电脑，因此当前不能证明真实网卡、防火墙、两台机器之间的文件搬运、
`host/host + udp` 或桌面双方先关体验。没有执行公网、STUN/TURN、WSS、摄像头、多路、正式产品 UI、
ARM 真机或安全基础设施测试；这些都不属于 Week 6 技术完成口径。
