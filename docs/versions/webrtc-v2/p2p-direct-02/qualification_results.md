# P2P-DIRECT-02 资格结果

> 日期：2026-09-02
> 结论：`passed(plaintext_team_broker)`

## 本地与构建门禁

| 门禁 | 结果 |
| --- | --- |
| Windows Debug WebRTC OFF | 全目标构建；CTest 43/43 |
| Windows Debug WebRTC ON | 全目标构建；CTest 53/53 |
| Windows Release WebRTC OFF | 全目标构建；CTest 43/43 |
| Windows Release WebRTC ON | 全目标构建；CTest 53/53 |
| ARM64 RASTER WebRTC OFF | 302/302 交叉构建；AArch64 ELF；无 Qt OpenGL/EGL/GLES NEEDED |
| ARM64 GLES3 WebRTC OFF | 316/316 交叉构建；AArch64 ELF；保留既有 Qt OpenGL 依赖 |
| ARM64 纯 Core | QEMU 用户态 `direct_session_core_passed` |
| OFF 产品边界 | 无 DIRECT CLI、无 Device Harness、无 WebRTC 产品入口 |

完整 CTest 覆盖 legacy MQTT、控制按钮/controller、摇杆、动态网格、OpenGL、EventCenter、Evidence、
全屏/退出、WebRTC viewer pipeline 和 DIRECT contracts。追加的双设备 Core 用例证明两条 source-bound
route 独立，跨设备消息稳定拒绝；retained session request 无副作用拒绝。

## 团队公网 MQTT 数据面

所有配置和原始结果位于 Git 忽略目录；仓库仅记录分类事实和脱敏计数。

| 场景 | Operator | Device Harness | 结论 |
| --- | --- | --- | --- |
| normal | publish 1、receive 4、connected | receive 1、publish 4、action 1 | 通过 |
| duplicate | publish 2、receive 7、duplicate reply 3 | receive 2、duplicate request 1、action 1 | 通过；副作用只执行一次 |
| reconnect | 主动断开后重新精确 SUBACK，publish 1、receive 5 | receive 1、action 1 | 通过 |
| two-route isolation | 两个现有 desktop 进程各 connected | 两个 Harness 各 action 1，四进程退出 0 | 通过；无跨 route 消息 |

没有登录管理后台、修改 listener/用户/ACL/插件/限额、删除 retained 数据或中断团队 Broker。malformed、
错误 QoS、retained session 和错误设备 route 均只在本地 Core/fixture 测试。

## 安全与诚实边界

- tracked Git、文档、源码和 CMake 对真实 Broker/管理地址扫描为零；配置默认关闭、hostname/port 为空。
- 结果不含 endpoint、完整 topic、payload、nonce 或 credential。
- 当前是明文 MQTT 功能资格，不是 MQTTS/TLS/Auth/ACL 资格。
- 两 route 公网用例使用两个现有 desktop 进程；单一桌面内正式 roster、多 tile/session 映射属于
  DIRECT-04。真实 WebRTC RTP、trickle ICE 和 STUN 属于 DIRECT-03。
