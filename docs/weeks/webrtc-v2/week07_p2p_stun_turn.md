# V2 Week 07：P2P、STUN 与 TURN

## 本周目标

完成 Qt 与参考发布器的一对一 P2P，真实区分 Direct、Relayed 和 Error。

## 知识

- trickle ICE candidate 的时序和 end-of-candidates。
- host、srflx、relay candidate pair 如何决定真实媒体路径。
- ICE restart 与重新创建 PeerConnection 的边界。
- TURN UDP、TCP/TLS 的适用场景和成本。

## 实验

1. 同机/同网段只使用 host candidate。
2. 两个受控网络使用 STUN 验证 srflx 直连。
3. 设置 relay-only 强制所有媒体经过 coturn。
4. 停止 coturn，确认 UI 不会显示 Direct 或假成功。

## 开发任务

- 信令交换 offer、answer、candidate、end-of-candidates 和 bye。
- PeerConnection 使用每路独立 generation，candidate 回调跨线程安全投递。
- 读取 selected candidate pair，生成 Direct/Relayed 事实状态。
- TURN 不可用、ICE Failed 和 WSS 断开采用有界重连。
- 参考发布器输出 H.264，复用 Week 4 RTP 规则。

## 验收

- P2P 单路 720p30 host、srflx、relay 分别通过。
- TURN relay 不显示 Direct；TURN 不可用显示明确 Error。
- 信令重连不复用旧 SDP/candidate，停止后无旧帧提交。

## 风险与停止条件

- 不用局域网成功代替公网 NAT 结论。
- 不在日志记录 candidate IP 或 SDP。
- 网络切换导致 generation 混用时停止进入部署模块。

## 下周入口

把回环组件组织成默认离线的公网安全部署模板。
