# V2 Week 01：WebRTC 基础认知

## 本周目标

建立信令、连接、加密和媒体四层心智模型。本周不修改生产代码，不安装公网服务。

## 知识

- Offer/Answer 解决能力和会话协商，不承载视频。
- SDP 描述 codec、方向、ICE 和 DTLS fingerprint，属于敏感会话数据。
- ICE 收集和检查候选路径；STUN 帮助发现映射，TURN 在直连失败时中继。
- DTLS 协商密钥，SRTP/SRTCP 保护媒体。
- 信令服务交换控制消息，正常 P2P 媒体不经过信令服务。

## 实验

1. 使用浏览器 WebRTC 内部页面建立一个只在本机运行的 PeerConnection 示例。
2. 观察 Offer、Answer、ICE gathering 和 selected candidate pair，但不复制原始值到仓库。
3. 关闭其中一端，记录连接状态从 Connected 到 Disconnected/Failed/Closed。
4. 用脱敏表格记录状态顺序、candidate 类型和建连耗时。

## 开发与文档任务

- 完成新手指南第 1～5 节复核。
- 画出“信令路径”和“媒体路径”两张图。
- 建立术语表：SDP、ICE、STUN、TURN、DTLS、SRTP、RTP、RTCP。
- 列出禁止进入日志和长期记忆的 WebRTC 数据。

## 验收

- 能解释为什么 P2P 仍需要服务器，以及何时媒体会经过 TURN。
- 能区分 SDP、ICE candidate、RTP packet 和解码后 VideoFrame。
- 实验只使用回环地址，仓库端点扫描无新增公网地址或凭据。

## 风险与停止条件

- 无法区分信令和媒体路径时不进入 Week 2。
- 浏览器实验产生的 SDP/candidate 只能放在临时内存或忽略目录，结束后删除。

## 下周入口

进入 WHIP/WHEP、SFU 与 SRS 6.0.184 回环链路学习。
