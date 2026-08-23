# WebRTC V2 Week 2 测试结果

> 日期：2026-08-20
> 自动复核：2026-08-21
> 平台：Windows x64 / MSVC / Qt 6.6.1 Debug
> libdatachannel：0.24.5，`LibDataChannel::LibDataChannel`，MPL-2.0

## 自动验证

| 验证 | 结果 | 证据摘要 |
|---|---|---|
| ON 全新配置 | 通过 | 精确发现 0.24.5 和真实 imported target |
| ON 全目标构建 | 通过 | probe/test 运行文件隔离到 WebRTC 专用输出目录 |
| ON 完整 CTest | 通过 | 39/39；含 session、H.264、10 轮 host-candidate loopback 和层依赖门禁 |
| loopback 压力复核 | 通过 | 单测连续执行 20 次，每次 10 个连接/重复关闭周期，共 200 个周期 |
| OFF 全新配置 | 通过 | 同时设置 `CMAKE_DISABLE_FIND_PACKAGE_LibDataChannel=TRUE`；变量未被项目使用，证明 OFF 未调用发现逻辑 |
| OFF 全目标构建 | 通过 | 无 WebRTC target、probe 或相关运行 DLL |
| OFF 完整 CTest | 通过 | 37/37；含专门的 disabled artifact 测试和全部既有回归 |
| ON 缺依赖负向配置 | 通过 | 配置阶段按预期失败；项目错误只报告所需版本和修复动作，不报告依赖安装路径 |
| CLI 真实文件 loopback | 通过 | 空 ICE server、host candidate、连接成功、输入/输出会话包清零 |
| 双进程 Offer/Answer 自动交换 | 通过 | Offer 文件先落盘后才启动 Answer；两端均按 `description_exported -> connected` 完成并以 0 退出，脱敏 session 一致 |
| CLI cleanup | 通过 | 成功完成，受管残留计数为 0 |
| 输出隐私扫描 | 通过 | SDP/candidate、ICE 凭据、fingerprint、IP、IPv6、STUN/TURN、Token、完整 UUID、交换目录和盘符路径模式 0 命中 |
| `git diff --check` | 通过 | 无空白错误；仅有既有 Windows 换行提示 |

## session 与文件测试覆盖

- 严格七字段、未知 schema、缺失/额外字段、错类型、非法 UUID/角色、角色与 description 不一致；
- RFC 3339 UTC 毫秒格式、10 分钟固定寿命、过期、未来 2 分钟容差和 session 关联；
- 非法 UTF-8、256 KiB 文件上限、192 KiB SDP 上限；
- `QSaveFile` 原子提交、Windows 受保护且仅当前用户一个允许 ACE 的 DACL；
- 规范文件名、规范父目录、非链接对象和目录外删除拒绝；
- 过期受管文件删除、畸形文件保留、正常回环后 Offer/Answer 文件清零；
- session ID 只输出 8 位 SHA-256 截断，错误只输出固定分类。

## 运行时依赖边界

ON 的 WebRTC 专用运行目录包含 `datachannel.dll`、`juice.dll`、`srtp2.dll`、
`libssl-3-x64.dll` 和 `libcrypto-3-x64.dll`，以及 probe/test 所需 Qt Debug DLL。产品 Debug 输出目录
不存在 WebRTC probe/test、内部库或上述五个 DLL。OFF 输出目录同样为零命中。

## 已处理的测试观察

- 迁移运行目录前留下的本地 Qt/WebRTC 旧生成物曾让一个既有 GUI 测试因平台插件解析而阻塞；删除
  已核对的旧生成物并固定 WebRTC runtime/archive 输出目录后，该测试恢复通过。源码和产品链接未受影响。
- 一次 loopback 测试在 PeerConnection 已连接但 selected candidate pair 尚不可查询时得到空类型；
  改为使用 gathering-complete 时已验证的本地类型作为回退后，连续 200 个周期通过。该修复不记录
  candidate 内容，也不增加 sleep。
- 2026-08-21 自动启动两个独立隐藏 probe 进程复核真实手工信令路径：启动 Offer 后确认目录中恰有
  一个 Offer 包，再启动 Answer；双方事件均为 `description_exported,connected`，退出码均为 0，
  candidate 类型仅为 host，stderr、敏感模式和最终受管文件数均为 0。随后 WebRTC 专项 3/3 与
  ON 完整 CTest 39/39 通过。前两次编排尝试仅因测试汇总器对空 stderr 的 null 统计错误退出，
  `finally` 已清理进程和临时文件；这不属于 probe 失败。

## 周末最小人工复核

1. 复述 Offer、Answer 与 non-trickle 的关系，以及为什么本周空 ICE server 只验证 host candidate。
2. 在两个控制台分别运行 `--mode=offer` 和 `--mode=answer`，确认双方连接成功。
3. 确认成功后交换目录中的对应 Offer/Answer 文件已删除，再运行 `--mode=cleanup`。
4. 人工阅读两端输出，确认只有允许字段，没有 SDP、candidate、地址、端口、凭据、fingerprint、完整
   UUID 或完整路径。

完成并记录以上人工结果前，`W2-GATE` 保持阻塞，Week 3 不开始。
