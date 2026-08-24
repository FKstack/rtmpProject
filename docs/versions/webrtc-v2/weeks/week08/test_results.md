# WebRTC V2 Week 8 测试结果

> 执行日期：2026-08-25
>
> 结论：`W8-GATE` 本地研发门禁通过；真实双机 LAN、公网和 ARM 资格未执行。
> 原始构建、日志、媒体夹具和 JSON 位于 Git 忽略的 `out/`，本文只保存脱敏摘要。

## 环境

| 项目 | 实际值 |
| --- | --- |
| 系统 | Windows 11 build 22631，x64 |
| 工具链 | Visual Studio Community 2026 18.9.1，MSVC 19.51.36256 |
| CMake | Visual Studio 安装内 CMake 4.3.1-msvc1 |
| Qt | 6.6.1 MSVC x64 |
| WebRTC | libdatachannel 0.24.5，vcpkg x64-windows |
| 测试平台 | 常规 UI/OpenGL 使用 `windows`；产品集成测试由 CTest 单独使用 `offscreen` |
| 分支/起始提交 | `Beta` / `ac5605f26405d1a42b71ead30d0c08ce49fcb1a2` |

## 最终自动矩阵

执行入口：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  scripts\webrtc\qualify_week8.ps1 -Action Run `
  -QtRoot '<qt-root>' -VcpkgRoot '<vcpkg-root>'
```

| 矩阵 | 构建 | CTest | 用时 | 关键结论 |
| --- | --- | ---: | ---: | --- |
| Debug OFF | 通过 | 39/39 | 125.20 s | product target/test 不存在，feature macro=0，旧路径通过 |
| Debug ON | 通过 | 47/47 | 196.46 s | product test 存在，feature macro=1，主程序和 datachannel runtime 存在 |
| Release ON | 通过 | 47/47 | 167.05 s | 优化构建、正式主程序、运行时部署和全量测试通过 |

脚本 `Check` 和 `SelfTest` 也分别返回 0。SelfTest 验证公共契约不含持久身份字段、CMake 接线存在、
两篇文章达到详细度下限、七张 SVG 可解析且图片链接有效。

## 新增产品测试结果

`rtmp_monitor_webrtc_product_test` 在 Debug ON 和 Release ON 都通过。最终 Debug 矩阵中为 1/1、
3.32 秒；测试内部数据行覆盖：

- `receiver-answerer`：正式产品接收端等待发布端 Offer；
- `receiver-offerer`：正式产品接收端先生成 Offer，发布端作为 Answerer。

两行都使用真实 SendOnly/ReceiveOnly PeerConnection 和真实 H.264 IDR，实际经过 RTP、H.264 receive
pipeline、`EncodedVideoInputHandle`、FFmpeg 解码、mailbox 与 CPU 画布。断言包括：

- ON-only 菜单 action 初始/运行/取消状态正确；
- 启动后恰好一个视频格、一个运行期 media stream；
- 受管 Offer/Answer 经 codec 校验且 sessionId 对应；
- selected candidate pair 存在且非 relay；
- decodedFrames、presentedFrames 均大于零；
- 最近呈现年龄位于 0～1,000 ms；
- 产品状态达到 `Direct`；
- 停止呈现超过 1,000 ms 后进入 Error，并观察 `MediaInterrupted`；
- 新鲜 IDR 再次呈现后恢复 Direct，并观察 `MediaRecovered`；
- `controlAuthorized=false`；
- `rtmpFallbackStarted=false`；
- 取消后状态 Idle、视频格 0、流 0、action 恢复、受管会话文件 0；
- 进程最终有界完成 `rtc::Cleanup()`。

纯策略测试同时通过：空显示名被拒绝、TURN 被当前一次性入口拒绝、ICE Failed+srflx 才分类
NeedsRelay、普通连接超时为 Error、呈现年龄 1,000 ms 接受而 1,001 ms 拒绝。

## 全量回归范围

最终矩阵还覆盖既有事件中心、证据、profiles、设备控制、MQTT、H.264 契约、解码、WebRTC signaling/
transport/client/viewer、publisher source、层依赖、OpenGL、动态网格、FFmpeg player、多路、音频、
生命周期、日志、URL/server monitor 和版本 CLI。`rtmp_monitor_layer_dependency_test` 在 ON/OFF 都通过。

完整 Debug ON 的最终资格结果为 47/47、196.46 秒。第一次运行过程中
记录并排除了两类测试环境假失败：未明确 Qt plugin path 时旧 UI 测试停在 QApplication；全局使用
offscreen 时 Windows OpenGL/窗口边框测试失败。最终脚本固定 plugin path，常规测试使用 windows，只有
product test 使用自身 offscreen 属性。

## 文档与静态门禁

- `summary.md`：17,916 字符、5,894 个中文字符，建议阅读 25～35 分钟；
- `testing_guide.md`：12,328 字符、4,581 个中文字符，分自动和人工两个版本；
- SVG：7/7 可作为 XML 解析，Markdown 链接存在；
- PowerShell：`qualify_week8.ps1` 在 Windows PowerShell 5.1 可解析并完成 Check/SelfTest/Run；
- `git diff --check`：无 whitespace error；行尾仅显示仓库既有 Windows CRLF 转换提示。

## 人工与外部环境状态

| 场景 | 状态 | 说明 |
| --- | --- | --- |
| 正式主程序可视菜单/对话框 | 未人工验收 | 自动 action 与对象树断言已通过，不冒充人的观感 |
| 同机两个可视程序文件交换 | 未人工验收 | 自动真实产品链通过，详细步骤已提供 |
| 物理双机 LAN | 延期/未验证 | Week 6 的同机便携结果不能替代 |
| 公司网络与移动网络 | 延期/未验证 | Week 7 runner 已存在，本轮未执行 |
| 公网 Direct/NeedsRelay | 未声称 | `publicNetworkClaimed=false` |
| ARM 交叉构建/真机 | 未验证 | 留给后续平台资格 |

## 最终结论

Week 8 的本地研发门禁通过：正式客户端 ON-only 一次性接收、产品状态证据、取消生命周期、脱敏事件/诊断、
无 RTMP 回退、无设备控制授权、schema/autoConnect 不变以及 OFF 兼容均有自动证据。允许进入 Week 9 的
后续开发，但所有真实网络和人工观感结论仍必须按 [测试指南](testing_guide.md) 独立执行和记录。
