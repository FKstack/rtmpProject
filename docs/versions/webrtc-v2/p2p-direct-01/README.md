# P2P-DIRECT-01 身份、Topic 与协议契约

> 日期：2026-09-02  
> 状态：`passed`  
> 范围：离线 contract 与 provisioning artifact；不连接 Broker，不进入产品运行路径

- [阶段摘要](summary.md)
- [测试结果](test_results.md)
- [威胁边界](threat_model.md)
- 共用 vectors：`contracts/signaling/v1/`

本阶段通过只表示身份、Topic、消息、状态模型和离线 provisioning contract 已建立并通过本地门禁，
不表示 Broker、MQTTS、真实 P2P 链路或产品 UI 已完成。
