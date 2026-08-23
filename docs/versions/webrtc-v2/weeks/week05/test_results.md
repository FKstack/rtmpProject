# WebRTC V2 Week 5 自动测试结果

## 环境

- 日期：2026-08-23
- Windows 11 build 22631
- Visual Studio Community 2026 18.9.1、MSVC 19.51.36256、Windows SDK 10.0.26100.0
- Qt 6.6.1、libdatachannel 0.24.5、FFmpeg 8 系列库、vcpkg x64-windows
- Debug，Visual Studio 18 2026 生成器

个人工具路径、SDP、candidate、地址、端口和原始日志不写入本文。

## 自动门禁结果

| 门禁 | 结果 | 实际证据 |
| --- | --- | --- |
| `qualify_week5.ps1 SelfTest / Check` | 通过 | 脚本解析、工具发现、样本契约、状态与清理 helper 通过 |
| WebRTC OFF fresh 配置/全构建/CTest | 通过 | 39/39；无客户端、libdatachannel、viewer 入口或 WebRTC DLL 依赖 |
| WebRTC ON fresh 配置/全构建/CTest | 通过 | 44/44；含 endpoint 接收测试和 viewer pipeline 测试 |
| H.264 接收契约 | 通过 | 双信令角色、错误 profile/fmtp、畸形/超限、SPS/PPS/IDR、时间戳回绕、容量丢弃、generation 和重复关闭 |
| `WebRtcViewerPipelineTest` | 通过 | 真实 Track → depacketizer → FFmpeg → mailbox → CPU canvas；64×64 解码帧、sequence、render/present 和非黑 framebuffer |
| 容量恢复 | 通过 | 首次 sink 返回容量丢弃后，只有下一组当前代 SPS/PPS+IDR 才恢复提交和呈现 |
| publisher/Offerer ↔ viewer/Answerer | 通过 | 180 AU、5045 RTP packet、180 submitted、0 receive drop；decoded/presented=true，1280×720 CPU canvas |
| viewer/Offerer ↔ publisher/Answerer | 通过 | 同上；receiver 可以创建 Offer 并完成媒体闭环 |
| CLI 与 Qt 启动 | 通过 | help=0、非法参数=2、viewer+source 拒绝；`qwindows`/`qoffscreen` 自动部署，无平台插件弹窗 |
| 退出与清理 | 通过 | 两种拓扑各 2 轮自然退出；受管 Stop、owned PID、交换文件和状态文件最终为 0 |
| Week 4 完整回归 | 通过 | 最终代码下 OFF 39/39、ON 44/44，旧测试 peer 两种角色与 recoverable keyframe 证据通过 |

Week 5 资格脚本只依据稳定 JSONL 事件和数值证据：`description_exported`、`connected`、
`media_received`、`frame_decoded`、`frame_presented`、`completed`。`frame_presented` 在 mailbox/render
计数增加并取得非黑 framebuffer 后才产生。

## 本轮发现并修复的问题

1. Qt 客户端曾在 `QApplication` 初始化时弹出“no Qt platform plugin could be initialized”。CMake
   现将 `qwindows` 与 `qoffscreen` 插件复制到运行目录的 `platforms/`，无环境变量 help 测试通过。
2. 首版 viewer 在 `paintEvent` 的同步呈现信号中抓取 framebuffer，造成递归重绘和栈溢出。证据抓取
   改为 UI event loop 的下一轮执行，并用 pending 标记合并重复请求。
3. Week 4 回归发现缺样本测试会先进入信令。publisher 现在在任何交换文件或 PeerConnection 副作用
   前检查固定样本，恢复 `file_not_found`、退出码 4 的旧契约。

上述问题均已在最终代码和完整回归中关闭，没有新增可复现的 Week 5 未解决问题。

## 人工与范围边界

桌面人工观感以及 viewer/publisher 分别先关闭的窗口操作尚未执行；它们不阻塞本轮自动技术完成，但正式交付前仍应按
`testing_guide.md` 记录。自动结果只证明同机 host-candidate 闭环，不证明双机 LAN、公网、TURN、
正式产品 UI、样本分发许可或 ARM 真机。
