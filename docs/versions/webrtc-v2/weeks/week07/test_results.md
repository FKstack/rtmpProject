# WebRTC V2 Week 7 实际测试结果

## 结论

- `W7-DESIGN-GATE`：通过。本机确定性STUN fixture、两个独立便携副本、两种拓扑各10/10。
- 研发阶段`W7-GATE`：按用户确认的本地设计验收通过，Week 8解锁。
- `W7-PUBLIC-NETWORK`：延期/未验证；没有第二台电脑，未声明真实公网Direct或NeedsRelay。
- 本地边界：`sameMachinePortable=true`、`publicClaimed=false`。

## 环境与提交

| 项目 | 实际值 |
| --- | --- |
| 日期/系统 | 2026-08-24；Windows 11 10.0.22631；SDK 10.0.26100.0 |
| 工具链 | Visual Studio 2026 18.9.1；MSVC 19.51.36256；Qt 6.6.1 |
| Feature提交 | `bd37337fe002508c55889a60c017b6c8f7fdb890` |
| 清理修复/最终包提交 | `4ab63ae6bca5b650b4b9a6cc3b5a905f6311683f` |
| 最终ZIP | `RtmpMonitor-WebRTC-Week7-4ab63ae6bca5-windows-x64.zip`（Git忽略out） |

## Fresh构建、CTest与本地20轮

| 配置/拓扑 | 实际结果 |
| --- | --- |
| Debug WebRTC OFF | 39/39；disabled边界通过 |
| Debug WebRTC ON | 46/46 |
| Release WebRTC ON | 46/46 |
| publisher/Offerer ↔ viewer/Answerer | 10/10 |
| viewer/Offerer ↔ publisher/Answerer | 10/10 |

每轮双方均有`srflx_observed`，selected pair为host/host UDP；viewer实际为5045 RTP packet、180个
received AU、180个submitted AU，decoded、rendered、presented、nonBlack和cleanupPassed均为true。
host/host是同机ICE正常优先选择；srflx由独立gathering事件证明，不代表公网。

最终两个副本没有`ice-runtime.json`或session JSON，client、fixture和qualification state零残留。
首次清理复验发现PowerShell 7把ISO时间自动反序列化为DateTime，旧Stop再次Parse产生八小时时区
偏差并按设计拒绝误杀；改为DateTime直接ToUniversalTime后，同一fixture成功停止且重复Stop安全。
原生构建输出也改为Out-Host，使CTest结果字段只保存整数。

## 包与旧周回归

- 最终Week 7 ZIP使用`4ab63ae6bca5...` identity，包内Check与SelfTest通过；不含local-config、
  授权记录、真实URL、fixture exe、调试物、源码、日志、state或session包。
- Week 4、5、6、7 SelfTest全部通过。
- Week 4完整Run：OFF 39/39、ON 46/46、publisher/session自动资格通过。
- Week 5完整Run：OFF 39/39、ON 46/46、双信令viewer媒体自动资格通过。
- Week 6完整Run：OFF 39/39、Debug ON 46/46、Release ON 46/46、包Check/SelfTest、host-only
  两个独立副本20/20通过；物理双机仍延期。

## 未执行范围

没有执行当前电脑移动网络与公司台式机公司网络、获授权公网STUN、真实NAT/CGNAT、防火墙、两种
公网角色各十轮、双方先退、网络变化或VerifyPublic。以后按testing guide收集脱敏报告再更新；
不预填Direct或NeedsRelay。
