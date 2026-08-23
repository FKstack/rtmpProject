# WebRTC V2 Week 2 实施摘要

> 日期：2026-08-20
> 分支：`Beta`
> 状态：自动技术门禁通过；`W2-GATE` 等待用户周末人工复核
> 前置例外：按“开发优先”授权实施，`W1-GATE` 的学习状态仍保持“未开始”

## 本周完成

- 接入本机已安装的 `libdatachannel:x64-windows 0.24.5`（MPL-2.0），真实 CMake target 为
  `LibDataChannel::LibDataChannel`；没有下载或提交第三方二进制。
- 新增默认 `OFF` 的 `RTMP_MONITOR_ENABLE_WEBRTC`。只有 ON 才创建
  `rtmp_monitor_webrtc_signaling`、`rtmp_monitor_webrtc_probe_core` 和开发者 CLI
  `rtmp_monitor_webrtc_probe`。现有产品、media、render、ui、RTMP 与 MQTT target 均不依赖它们。
- 冻结一次性 schema v1：严格七字段、UUID v4、UTC 毫秒时间、10 分钟有效期、2 分钟未来时钟
  容差、256 KiB 文件上限和 192 KiB SDP 上限。原子写入、Windows 当前用户专用 DACL、规范路径、
  非链接文件检查、成功导入/正常退出清理和启动过期清理均已实现。
- CLI 支持 `offer`、`answer`、`loopback`、`cleanup`。工具只使用仓库内被忽略的固定交换目录，
  不接受 SDP、candidate、端点或任意文件路径参数；PeerConnection 显式使用空 ICE server 配置，
  只创建 DataChannel，不发送媒体。
- 回调通过 weak state、互斥量和条件变量提交状态；控制路径按 gathering/connection deadline 等待，
  `close()` 可重复调用并先失效回调。一次候选类型可见性竞态已改为使用 gathering-complete 的稳定
  类型作为 selected-pair 尚不可查时的回退，没有加入固定 sleep。
- 使用离线固定 H.264 样本验证 Annex-B、payload type、SSRC、90 kHz clock、1200-byte fragment
  上限、marker/timestamp、FU-A 重组和缺片输入；没有创建 Track 或 Week 3 媒体路径。
- 日志采用字段允许列表，只输出哈希截断 session、角色、字节数、candidate 类型、耗时、计数和错误
  分类；不把原始异常、SDP、candidate、地址、端口、ICE 凭据、fingerprint 或完整路径交给日志层。

## 架构影响

- 风险等级：R2。
- 职责：`signaling` 拥有 schema、校验和受限文件生命周期；`probe_core` 拥有开发阶段
  PeerConnection/DataChannel 状态机；CLI 是唯一组合根。
- 依赖：固定为 `probe/test -> probe_core -> signaling/libdatachannel`。层依赖门禁禁止 WebRTC
  内部代码包含 app、media、render、ui、server 等产品层。
- 契约与兼容：新增接口仅为默认关闭的 CMake 开关、开发者 CLI 和一次性 schema v1；OFF 时产品
  CLI、运行时、安装边界及既有公共契约不变。
- 明确未做：Transport、MediaSource、schema v2、产品 UI、STUN/TURN、WHEP/SRS 配置以及 P2P
  H.264 发送/接收路径均未创建。

## 门禁状态

Week 2 的自动化技术项已有构建、测试、真实文件回环和脱敏扫描证据。`W2-GATE` 仍为“阻塞/等待
用户周末人工复核”，不得据此进入 Week 3。人工项见 `test_results.md`。
