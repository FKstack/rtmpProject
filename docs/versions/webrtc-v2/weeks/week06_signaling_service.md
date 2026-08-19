# V2 Week 06：Go WSS 信令服务

## 本周目标

提供一发布端、一观看端的受限 room 信令，不承载媒体，不保存会话内容。

## 知识

- WebRTC 为什么不规定信令协议。
- WSS upgrade、Origin、Authorization 和反向代理边界。
- 短期签名 Token 与 DTLS-SRTP 各自解决的问题。
- 容量、TTL、背压和断线清理为何属于安全边界。

## 实验

- 用两个本机测试客户端交换脱敏的 offer/answer/candidate 测试对象。
- 填满发送队列和房间容量，观察明确拒绝而非内存持续增长。

## 开发任务

- Go 1.22 + Gorilla WebSocket 1.5.3 单进程服务。
- 每 room 一个 publisher、一个 viewer；正常消息只路由给同 room 对端。
- 默认 TTL 10 分钟、消息 256 KiB、队列 32、房间 1024。
- Token 绑定 room/peer/role/exp，只接受 Authorization header。
- 停止和重启清空全部内存态 room；日志不含 payload、SDP、candidate 或 Token。

## 验收

- Token 过期、篡改、身份不匹配和错误 role 被拒绝。
- 跨房间隔离、角色占用、TTL、容量、背压和断线清理通过。
- 服务重启后无持久化会话；Go test、race 检查和 vet 通过。

## 风险与停止条件

- 不提供公开 Token 签发入口；鉴权签发在部署模块设计。
- 不把 Token 放查询参数，不打印 WebSocket 消息。
- 没有有界背压策略时不得暴露 WSS 入口。

## 下周入口

Qt/reference publisher 接入信令，验证 P2P host、srflx 和 relay。
