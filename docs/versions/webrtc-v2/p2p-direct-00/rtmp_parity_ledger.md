# RTMP parity ledger

| 能力 | 结论 | Owner | 目标阶段 | 当前证据 |
| --- | --- | --- | --- | --- |
| 最大 16 路 RTMP | 有意识收敛为 4 路 WebRTC | product session owner | DIRECT-05/08 | Week 10 四路同机通过；物理资格待补 |
| 单向 AAC 音频 | 产品取消，不迁移 WebRTC Audio Track | product owner | DIRECT-08 | ADR-028 与本计划产品范围 |
| SRS DVR/录像 | 产品取消 | product/deployment owner | DIRECT-08 | 既有 SRS PoC 与本计划产品范围 |
| SavedStream v1 | 迁移版本只读/显式导出绑定 | profiles owner | DIRECT-07/08 | schema v1 回归仍通过 |
| SRS health | 替换为 Broker/TLS、STUN、视频、控制分面健康 | diagnostics/product owner | DIRECT-05/08 | ADR-044/046；实现待后续 |
| `StartStream(rtmpUrl)` | 只保留 legacy RTMP tile，最终退役 | device control owner | DIRECT-06/08 | 当前 control 契约测试 |
| FFmpeg | 保留解码、像素处理与 fixture | media owner | 持续 | Week 10 媒体/长稳证据 |
| 上下左右、停车、键盘和摇杆 | 保留现有 UI/controller/codec 和固定 MQTT 指令 | device control owner | 持续/DIRECT-06 | 当前 control/controller/input tests |
| OpenGL/CPU、动态网格和全屏 | 原实现持续复用，不另建 DIRECT 渲染体系 | render/ui owner | 持续 | canvas/grid/fullscreen/WebRTC viewer tests |
| 事件列表、详情和目录导出 | 原 EventCenter store/service/panel 持续复用 | event owner | 持续 | EventCenter service/panel tests |
| 事件证据截图和全屏截图 | 保持当前用户触发行为，不新增自动截图 | evidence/ui owner | 持续 | Evidence 与 VideoGrid screenshot tests |
| 项目/二进制名称 | 退役阶段一次性迁移 | release owner | DIRECT-08 | 当前不改名 |

本表所有项均有明确结论、owner、目标阶段和证据入口；“目标阶段”不是完成声明。只有对应现场、包、迁移
和安全门禁实际通过后才能关闭条目。
