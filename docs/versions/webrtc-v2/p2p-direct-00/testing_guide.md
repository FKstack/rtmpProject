# P2P-DIRECT-00 测试指南

## 本地静态与 fixture 自测

从仓库根目录执行新的 P2P-DIRECT-00 资格脚本 `Check` 和 `SelfTest`。它只读取真实依赖、验证配置拒绝、
执行敏感端点扫描并运行测试 fixture 的离线模式，不访问公网。

## Windows 四矩阵与 DAG

复用 Week 10 fresh Debug/Release × WebRTC OFF/ON 构建与完整 CTest。资格脚本另外在 OFF/ON fresh 配置
生成 Graphviz target DAG，规范化后核对禁止依赖边。测试数量动态读取，不固定为旧的 39/49。

## Legacy 观察

运行配置必须位于忽略目录并显式提供。观察只进行 CONNECT、随机精确 topic SUBSCRIBE、等待有界时间、
UNSUBSCRIBE、DISCONNECT；不发布、不订阅 control/status、不记录地址或 payload。该结果只能证明当前
连接兼容性，不能证明 TLS、安全、设备在线或命令执行。

## 产品候选

对隔离 EMQX 6.2.3 运行完整 capability fixture。当前仓库未包含或部署候选实例，未取得真实结果前阶段
保持 `blocked(broker_candidate)`。Mosquitto 只在 EMQX 任一门禁失败后用同一 fixture 复验。
