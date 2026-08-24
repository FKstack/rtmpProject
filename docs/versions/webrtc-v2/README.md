# RtmpMonitor WebRTC V2 文档

本目录对应计划版本 `0.2.0-beta.1`。当前采用“双客户端优先”路线：同一测试客户端未来可分别运行
publisher/viewer，并独立选择 Offerer/Answerer。Week 2 已完成默认关闭的开发者信令与回环基础，
Week 3 已完成协议无关 H.264 契约、外部解码入口和 RTMP 兼容；Week 4 已完成对称 endpoint、固定
MP4 publisher 和 SendOnly H.264 Track；Week 5 已在同一测试客户端完成 ReceiveOnly depacketize、
既有 FFmpeg/mailbox/CPU 画布闭环和双信令角色自动技术门禁；Week 6 已增加脱敏 selected-pair、
便携信令根、Release 测试包、同机双包黑盒和双机 runner。用户已接受最终 ZIP 本地双实例
20/20 作为 Week 6 设计验收；Week 7 已增加默认host/显式stun的固定本机配置、地址无关ICE事实、
确定性本地fixture、两个便携副本资格和延期公网runner。本地设计门禁用于解锁Week 8；真实双机
LAN、公司网络/移动网络公网、正式产品UI和ARM真机仍未验证。RTMP `0.1.0-alpha.1` 仍是稳定路径。

## 总计划

- [WebRTC V2 双客户端优先十周研发总计划](../../roadmap/webrtc_v2_project_plan.md)

所有 Week 1～10 任务、架构边界、接口目标、网络风险和退出门禁只在总计划维护，不再拆成重复的
周任务文件。SRS/WHIP/WHEP、自动 WSS 信令和 TURN Relay 在获得公网基础设施后另立计划。

## 六章零基础动手教程

这是一条“一天完成”的 Kilo 式学习路线：每次只改一小块代码，立刻构建、运行、观察，再解释现象。
读者只需掌握 C++ 基础，不要求预先理解 WebRTC、ICE、SDP 或异步网络。六章始终操作同一个独立
[`webrtc_minilab`](../../../tutorials/webrtc-minilab/)；仓库只保留最终源码，中间检查点只创建在 Git
已忽略的 `out/learn-webrtc-minilab/`。

### 教程契约

| 项目 | 固定约定 |
| --- | --- |
| 实测环境 | Windows 11 build 22631、Visual Studio Community 2026 18.9.1/MSVC 19.51.36256、CMake 3.27.7（最低 3.21）、Ninja 1.13.2、C++17；Visual Studio 2022 兼容但不是本轮实测主线 |
| 依赖 | vcpkg `x64-windows`、libdatachannel 0.24.5，文档命令只使用 `$env:VCPKG_ROOT` 占位，不记录个人路径 |
| 最终成果 | 单进程内两个真实 `PeerConnection`，内存交换 non-trickle Offer/Answer，经 DataChannel 完成 `ping → pong` |
| 明确非目标 | 文件信令、Track/RTP/H.264、视频出画、双机 LAN、公网、STUN/TURN、GUI 与产品集成 |
| 输出边界 | 只输出轮次、阶段、固定状态、耗时和固定错误分类；不输出 SDP、candidate、地址、端口、凭据或原始异常 |
| 学习时间 | 约 5.5～7 小时；首次下载或编译依赖的等待时间另计 |

每章都有“当前问题 → 小块代码修改 → 构建 → 运行 → 实际证据 → 稳定通过条件”，以及一个不能只靠
照抄主线完成的小实验、完整答案和恢复方法。API 只在第一次出现时详细解释；后续章节只说明它在当前
执行链中的新作用并回链首次说明。每个代码块后立即说明函数职责、参数、返回值、状态修改、线程、
所有权、失败方式、设计原因和当前限制。

修改已有代码时，教程使用下面的显示契约：

- 浅红区块标记要删除或替换的旧片段；
- 浅黄区块标记新增片段；
- 紧随其后的普通代码围栏是没有变更符号、可以直接复制的权威版本。

如果 Markdown 预览器过滤背景色，区块内的“删除（红色）/新增（黄色）”文字仍可辨认；复制代码时
始终使用普通代码围栏，不复制彩色说明块。教程不使用统一差异围栏、补丁头、hunk header 或行首
加减号变更标记。每一步只能从该章声明的精确前置检查点继续，不能跨章跳着替换代码。

### 学习顺序

建议在一天内按顺序完成：

1. [环境搭建与第一个 MiniLab 程序](guides/01_webrtc_from_rtmp.md)
2. [创建两个 PeerConnection](guides/02_sdp_offer_answer_and_manual_signaling.md)
3. [Offer、Answer 与 non-trickle ICE](guides/03_ice_stun_turn_and_public_networks.md)
4. [让两个端点说话：DataChannel ping → pong](guides/04_rtp_rtcp_h264_media_pipeline.md)
5. [异步等待与安全退出](guides/05_libdatachannel_qt_integration.md)
6. [完成 MiniLab：测试、隐私与 RtmpMonitor 映射](guides/06_p2p_testing_security_and_troubleshooting.md)

| 章节 | 建议时间 | 这一章结束时程序会做什么 |
| --- | ---: | --- |
| 第 1 章 | 60～90 分钟 | 配置依赖并输出 `miniwebrtc ready` |
| 第 2 章 | 45～60 分钟 | 创建、观察并关闭两个 PeerConnection |
| 第 3 章 | 75～90 分钟 | 内存交换完整 Offer/Answer，双方 Connected |
| 第 4 章 | 45～60 分钟 | DataChannel 完成 `ping → pong` |
| 第 5 章 | 90～120 分钟 | 有界等待、幂等关闭、重复运行和受限 CLI |
| 第 6 章 | 45～60 分钟 | CTest、隐私门禁与最终验收 |

MiniLab 是独立 C++17/CMake 项目，只依赖 libdatachannel 0.24.5，不进入根工程或产品安装包。它只
证明单进程 host-candidate DataChannel 通路；双控制台文件信令继续使用 Week 2 probe，WebRTC
Track、RTP/H.264 收发、双客户端视频、LAN 和公网能力仍不在教程成品中。

### API 首次出现索引

- 第 1 章：`find_package(EXACT CONFIG)`、imported target、`target_link_libraries(PRIVATE)`。
- 第 2 章：`rtc::Configuration`、`PeerConnection`、状态回调、`rtc::Cleanup()`。
- 第 3 章：description API、gathering callback、`condition_variable::wait_until()`。
- 第 4 章：`onDataChannel`、`onOpen`、`onMessage`、`send()` 的 buffered 返回语义。
- 第 5 章：weak state、generation、`resetCallbacks()`、有界 Cleanup、`from_chars()`。
- 第 6 章：CTest、测试超时和输出正则门禁。

## Week 1～10 结果

- [周结果目录规则](weeks/README.md)

Week 2 已有自动技术结果，但 `W2-GATE` 仍等待用户双控制台人工复核；用户随后明确授权先完成
Week 3，其自动技术门禁已通过，实际结果见 [Week 3 summary](weeks/week03/summary.md) 与
[test results](weeks/week03/test_results.md)。Week 4 的自动技术结果见
[summary](weeks/week04/summary.md) 与 [test results](weeks/week04/test_results.md)。Week 5 的接收播放
闭环、测试结果和用户测试步骤见 [summary](weeks/week05/summary.md)、
[test results](weeks/week05/test_results.md) 与 [testing guide](weeks/week05/testing_guide.md)。Week 1
学习状态保持未开始。Week 6 的 Week 5 深度回顾、便携包实现与类职责见
[summary](weeks/week06/summary.md)，实际结果见 [test results](weeks/week06/test_results.md)，自动与
真实双机步骤见 [testing guide](weeks/week06/testing_guide.md)。其他周只有实际完成相应任务后才写入结果。大型日志、会话包、SDP/candidate
和二进制制品不进入 Git。

Week 7 的深度实现说明见 [summary](weeks/week07/summary.md)，自动、本地双实例和以后两台电脑
公网步骤见 [testing guide](weeks/week07/testing_guide.md)，实际与延期事实见
[test results](weeks/week07/test_results.md)。

## 安全提醒

- 默认网络功能保持关闭。
- 真实 STUN 地址只能由用户明确输入，不作为源码或文档默认值。
- Offer/Answer 会话包只进入被忽略的 `out/webrtc-p2p/session-exchange/`，默认 10 分钟后过期。
- SDP、candidate、IP、端口、Token 和临时凭据不得进入日志、周报、issue 或仓库。
- 没有非 relay selected candidate pair 和真实媒体证据时，不得声明 Direct。
