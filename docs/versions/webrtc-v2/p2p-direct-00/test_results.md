# P2P-DIRECT-00 测试结果

> 日期：2026-09-01
> 状态：`blocked(broker_candidate)`

| 门禁 | 当前状态 | 证据边界 |
| --- | --- | --- |
| 文档/ADR/parity 基线 | passed | ADR-046 与每项 owner/status/stage/evidence 已提交 |
| fixture self-test | passed | 配置、通配符、control topic、legacy credential、未知字段拒绝通过 |
| Debug/Release OFF | passed | 40/40、40/40；版本和 OFF 制品审计通过 |
| Debug/Release ON | passed | 50/50、50/50 |
| ARM64 RASTER/GLES3 OFF | passed(cross-build) | AArch64 ELF 与动态依赖通过；不代表真机 |
| legacy observe | passed(test-only) | MQTT5、随机 SUBACK、无 payload、UNSUBACK、DISCONNECT；无 publish |
| isolated EMQX 6.2.3 | blocked(environment) | 候选实例尚未提供或创建 |
| Mosquitto 2.1.2 fallback | not_applicable | 仅 EMQX 失败后执行 |
| OFF/ON target DAG | passed | 341/483 边；fixture PRIVATE TLS 边，无 lower→product 反向边 |
| 敏感端点扫描 | passed | Git、源码、文档、脚本和结果零真实端点；本机配置被忽略 |

## 预验证问题

第一次复用 Week 10 build root 时，旧 fixture 布局遗留的 OpenSSL DLL 使 OFF 制品测试失败；fixture 随即
改为独立输出目录，针对性 3/3 复验通过。随后一次调用因传入相对 BuildRoot 导致版本探针路径重复；
改用绝对全新 build root 后四矩阵全部通过。这两项均没有被隐藏为最终通过前的噪声。

四矩阵完成后的最终 diff 审查又把所有异步等待状态延长到 Paho handle `destroy()` 完成，并在销毁后才
释放 MQTT5 properties，防止超时 callback 越过栈生命周期。该修订重新构建 Debug OFF fixture，
optional-transport、layer dependency、fixture self-test 3/3 通过，并用最终二进制复验无 publish legacy
观察通过；它不进入任何产品或其他测试 target。

## 最终判定

本地工程门禁全部通过，但产品 Broker 候选未运行，故 `P2P-DIRECT-00=blocked(broker_candidate)`。
任何 `blocked` 或 `not_applicable` 不得写成阶段通过，也不得进入 `P2P-DIRECT-01`。
