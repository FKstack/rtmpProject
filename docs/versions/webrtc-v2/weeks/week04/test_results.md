# WebRTC V2 Week 4 自动测试结果

## 环境

- 日期：2026-08-23
- Windows 11 build 22631
- Visual Studio Community 2026 18.9.1、MSVC 19.51.36256、Windows SDK 10.0.26100.0
- Qt 6.6.1、libdatachannel 0.24.5、vcpkg x64-windows
- Debug，Visual Studio 18 2026 生成器

## 总控

入口为 `scripts/webrtc/qualify_week4.ps1`，支持 `Check | Run | Status | Stop | SelfTest`。`Run` 使用
fresh ON/OFF 配置，按 PID、完整 exe 路径和启动时间管理子进程；Answer 端只在观察到脱敏的
`description_exported` 事件后启动。脚本不读取或输出 SDP。

## 结果

| 门禁 | 结果 | 脱敏证据 |
| --- | --- | --- |
| PowerShell 解析、SelfTest、Check | 通过 | VS/Qt/vcpkg/FFmpeg 工具定位成功 |
| WebRTC OFF fresh 构建与 CTest | 通过 | 39/39，含 OFF 产物和层依赖审计 |
| WebRTC ON fresh 构建与 CTest | 通过 | 43/43，含 Week 2/3 回归及两个 Week 4 C++ 测试 |
| Endpoint 配置/状态/生命周期 | 通过 | 四种角色方向、非法角色、generation、容量 2 溢出、等待 IDR、恢复、十轮幂等关闭 |
| 真实 Track 两种 publisher 角色 | 通过 | publisher=Offerer 与 publisher=Answerer 均连接、发送并自然退出 |
| 测试 peer 媒体验证 | 通过 | RTP 经 H.264 depacketizer 重组；首个可恢复 AU 含 SPS/PPS/IDR |
| MP4 source | 通过 | 180 AU、6 个关键帧、AVCC→Annex-B、单调时间戳、约 6 秒 pacing、立即停止；缺视频、非 H.264、B 帧和损坏文件精确拒绝 |
| CLI | 通过 | help=0、非法参数=2；仅固定 JSONL 事件/错误枚举，无窗口 |
| 资源与隐私 | 通过 | owned PID、受管交换文件和资格状态最终为 0；日志敏感模式零命中 |

完整 ON CTest 的新增项为 `rtmp_monitor_webrtc_endpoint_test` 和
`rtmp_monitor_h264_publisher_source_test`。WebRTC 集成测试串行，单项上限 120 秒；全局 Cleanup
外层上限 10 秒。

## 结论边界

自动测试证明本机 host-candidate 条件下 publisher/session/Track/AU 与资源清理成立。它不证明
viewer 已解码出画、两台电脑可达、LAN/公网可用或分发样本合规。computer-use 未作为本周验收依据。
