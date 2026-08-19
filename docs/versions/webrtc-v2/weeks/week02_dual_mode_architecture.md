# V2 Week 02：双模式架构与 SRS 回环实验

## 本周目标

理解公网服务器模式和 P2P 的职责差异，冻结实现前架构、所有权和验收基线。

## 知识

- WHIP 用于向服务器发布，WHEP 用于从服务器播放。
- SFU 转发媒体，不等同于信令服务器。
- 公网服务器模式提供集中入口和多观看端能力；P2P 优先减少媒体中转。
- WHEP HTTP resource、PeerConnection 和 Track 都有明确会话生命周期。

## 实验

1. 固定 SRS 6.0.184，配置仅监听 `127.0.0.1` 的学习环境。
2. 使用官方页面完成浏览器 WHIP 发布和 WHEP 播放。
3. 分别停止发布端和 SRS，记录观看端状态。
4. 验证正常 RTMP 配置没有因 WebRTC 学习实验产生副作用。

## 开发与文档任务

- 复核现有 CMake target、FFmpegPlayer、DecodeWorkerPool、邮箱和渲染所有权。
- 冻结 `media → transport` 新依赖以及 transport 禁止访问的外层模块。
- 定义每路会话停止顺序和 generation 规则。
- 记录当前 RTMP 36/36 CTest 基线，不把浏览器实验表述为 Qt 已实现。

## 验收

- 浏览器 WHIP → SRS → WHEP 回环路径可重复。
- 生产代码仍无 WebRTC 依赖，默认网络行为不变。
- 架构评审明确职责、依赖、数据/线程/资源所有权和兼容策略。

## 风险与停止条件

- SRS 版本或配置来源不明确时停止，不混用 7/8 开发线。
- RTMP 基线失败时先恢复稳定路径，不进入 transport 实施。

## 下周入口

按批准边界建立类型安全 MediaSource 和 transport target。
