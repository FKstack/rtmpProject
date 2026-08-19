# V2 Week 08：公网安全部署

## 本周目标

形成 Nginx、SRS、Go 信令/鉴权和 coturn 的可重复、默认安全部署模板。

## 知识

- TLS termination、WSS upgrade 和内部服务网络隔离。
- 短期应用 Token 与 TURN REST 临时凭据。
- Nginx/SRS/coturn 的公网端口和最小暴露面。
- Secret 生命周期、证书续期和日志脱敏。

## 实验

- 在受控 Linux 主机使用测试域名和证书完成 HTTPS/WSS。
- 分别阻断信令、SRS RTC 和 TURN 端口，记录客户端诚实状态。

## 开发任务

- Docker Compose 编排 Nginx、SRS 6.0.184、Go 服务和 coturn。
- Nginx 作为唯一 HTTPS/WSS 入口；SRS HTTP API 只在内部网络监听。
- 应用 Token 默认 5 分钟，TURN 临时凭据默认 10 分钟。
- 仓库模板只含占位符和 `.env.example`，真实 Secret 使用部署机 secret store。
- 建立健康检查、启动顺序、优雅停止和日志轮转。

## 验收

- HTTPS/WSS 证书验证正常，不允许客户端跳过 TLS 校验。
- 过期 Token/凭据被拒绝，强制 relay 能建立且状态为 Relayed。
- SRS、信令、coturn 分别停止与恢复的状态和事件符合事实。
- 镜像/配置/日志扫描不含真实端点或 Secret。

## 风险与停止条件

- SRS API 直接暴露公网或静态 TURN 密码进入客户端时停止发布。
- 未验证证书续期和 Secret 轮换前只允许测试环境。

## 下周入口

将三类 MediaSource 和本机配置引用接入 Qt 保存流 UI。
