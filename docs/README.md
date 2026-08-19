# RtmpMonitor 文档索引

项目文档按“当前记忆、路线规划、架构设计、通用指南、分周实现、历史归档”分类。第一次接触项目时，建议先阅读项目快照和架构总览，再按当前任务进入对应指南或周次记录。

## 0. 跨会话记忆

- [记忆系统说明](memory/README.md)：文件职责、恢复流程、维护规则和 OpenViking 边界。
- [当前项目快照](memory/project_snapshot.md)：经验证的当前状态、阻塞和下一步。
- [重要设计决策](memory/decisions.md)：影响长期架构、平台、存储和安全的轻量 ADR。
- [已知问题](memory/known_issues.md)：可复现问题和未完成的验收项。
- `project_handoff.md`：本地会话交接文件，由 `.gitignore` 排除。

## 1. 路线规划

- [项目计划](roadmap/project_plan.md)：项目目标、阶段安排、验收标准和当前进度。
- [WebRTC V2 十二周研发总计划](roadmap/webrtc_v2_project_plan.md)：双模式目标架构、模块边界、Week 1～12 路线、验收门禁和风险；当前仅为 Beta 规划，不代表功能已经实现。
- [移动安防产品模块竞品调研与演进建议](roadmap/mobile_security_product_module_recommendations.md)：基于当前 RTMP/MQTT 实现与海康、大华、Axis、Milestone、移动巡逻机器人公开能力，给出不改 RTMP 底座的模块优先级、目标架构和分期门禁。
- [v0.1.0-alpha.1 版本冻结与交付任务清单](roadmap/v0.1.0_alpha1_release_handoff_checklist.md)：源码交接、Windows 测试包、ARM 真机和最终发布门禁。

## 2. 通用指南

### 2.1 架构与深入学习

- [渐进式架构解耦设计与实施过程](architecture/progressive_decoupling_architecture.md)：详细说明耦合识别、优先级、逐阶段迁移、拆分类职责、线程/组件/CMake 架构图、收益、遗留边界和评审门禁。
- [保存推流与单车 MQTT 控制：设计、使用与测试指南](architecture/saved_stream_and_mqtt_device_control.md)：新增类职责、模块接入、配置与协议、界面操作、本地自动/手工测试、公网与实车安全边界及故障排查。
- [单车值守闭环架构设计](architecture/mobile_security_single_vehicle_operator_loop_design.md)：在不改 MQTT 返回值和硬件的约束下，定义本地控制安全、平台事件、截图证据与可选 SRS DVR PoC 的职责、契约、生命周期和门禁。
- [当前视频渲染架构审查](architecture/current_rendering_architecture_review.md)：以当前源码和构建边界审查渲染链路。
- [GoObject PDF 渲染框架映射](architecture/pdf_rendering_framework_mapping.md)：外部渲染框架与项目实现的逐项映射。
- [产品级视频渲染框架](architecture/video_rendering_framework.md)：共享画布、YUV/OpenGL、CPU 回退和全屏资源边界。
- [低延迟单向音频框架与 MP4 手工测试指南](architecture/low_latency_audio_stream.md)：逐类说明 AAC 下行如何嵌入多路 RTMP/视频框架，并提供 MP4 检查、SRS 启停、FFmpeg 推流、RtmpMonitor 拉流播放、指标观察、自动资格和排障命令。
- [嵌入式设备分级与通用显示优化方案](architecture/embedded_device_rendering_strategy.md)：RASTER/GLES3 分级、CMake 裁剪和板级资格策略。
- [SRS Server 接入实施方案](architecture/srs_server_integration_plan.md)：SRS 版本、部署、Qt 接入、异常恢复及未完成验证边界。
- [ADR：单主 OpenGL 画布与临时全屏画布](architecture/adr/001-video-rendering-architecture.md)：渲染顶层窗口与资源生命周期决策。
- [Week 4～5 架构、维护与深入学习指南](guides/architecture/week4_week5_architecture_guide.md)：从 Week 3 过渡到多路播放、状态、重连和日志框架。
- [Week 4 多路媒体与并发深度学习](guides/architecture/week4_media_concurrency_deep_dive.md)：线程模型、包队列、共享解码池和代码调用链详解。
- [用户事件、系统日志与审计日志架构](guides/architecture/logging_architecture.md)：三类信息边界、配置、轮转、脱敏和维护入口。

### 2.2 开发规范

- [代码规范](guides/development/code_style_guide.md)
- [注释规范](guides/development/comment_style_guide.md)
- [QSS 样式加载与主题扩展](guides/development/style_loading.md)
- [OpenViking 使用、结构观察与测试指南](guides/development/openviking_usage_and_testing.md)：理解上下文数据库、Hook、VikingFS、Web Studio 和可清理的学习实验。

### 2.3 构建与验证

- [Windows x64 与 Linux ARM64 跨平台构建](guides/build-and-testing/cross_platform_build.md)
- [嵌入式二次开发交接与环境填写模板](guides/build-and-testing/embedded_developer_handoff.md)
- [Linux 双路径渲染构建（RASTER/GLES3/AUTO）与板级资格脚本](guides/build-and-testing/linux_dual_render_build.md)
- [SRS 新手完全指南：概念、使用、配置与项目联动](guides/build-and-testing/srs_beginner_guide.md)
- [WebRTC 新手指南](guides/build-and-testing/webrtc_beginner_guide.md)：按信令、SDP、ICE、STUN/TURN、加密、RTP/H.264、WHIP/WHEP 和生命周期顺序建立基础知识。
- [历史 nginx-rtmp 链路说明](guides/build-and-testing/rtmp_chain_verification.md)：其维护者本机脚本已退役；当前链路使用 SRS 指南。
- [SRS 异常恢复与发布门禁验证指南](guides/build-and-testing/srs_failure_recovery.md)
- [Windows 真实摄像头帧率与延迟资格测试](guides/testing/windows_camera_validation.md)：720p30 的 1/4/8 路机器指标门禁、60 FPS 视觉复核和离线分析。

## 3. 分周实现

### V2 Week 1～12：WebRTC 双模式研发计划

- [Week 1：WebRTC 基础认知](weeks/webrtc-v2/week01_webrtc_fundamentals.md)
- [Week 2：双模式架构与 SRS 回环实验](weeks/webrtc-v2/week02_dual_mode_architecture.md)
- [Week 3：协议无关 Transport 边界](weeks/webrtc-v2/week03_transport_boundary.md)
- [Week 4：H.264 RTP 重组](weeks/webrtc-v2/week04_h264_rtp.md)
- [Week 5：公网服务器单路 PoC](weeks/webrtc-v2/week05_whep_server_poc.md)
- [Week 6：Go WSS 信令服务](weeks/webrtc-v2/week06_signaling_service.md)
- [Week 7：P2P、STUN 与 TURN](weeks/webrtc-v2/week07_p2p_stun_turn.md)
- [Week 8：公网安全部署](weeks/webrtc-v2/week08_secure_deployment.md)
- [Week 9：保存配置与三模式 UI](weeks/webrtc-v2/week09_profiles_and_ui.md)
- [Week 10：产品状态、事件与安全语义](weeks/webrtc-v2/week10_product_states_and_safety.md)
- [Week 11：多路与故障矩阵](weeks/webrtc-v2/week11_multistream_failure_matrix.md)
- [Week 12：性能、跨平台与 Beta 发布门禁](weeks/webrtc-v2/week12_release_qualification.md)

以上文件是未来研发任务书，不是完成记录。每周必须在门禁通过后，才可把对应条目标记为已实现。

### Week 2：动态界面

- [Qt 动态视频网格实现](weeks/week2/week2_ui_layout.md)
- [动态视频网格与添加动画](weeks/week2/week2_dynamic_grid.md)
- [拖拽换位与单路全屏](weeks/week2/week2_drag_and_fullscreen.md)
- [功能调用链详解](weeks/week2/week2_feature_call_flow.md)

### Week 3：单路播放与延迟

- [从零理解并测试 FFmpegPlayer](weeks/week3/week3_ffmpeg_player.md)
- [桌面实况端到端延迟测试](weeks/week3/week3_desktop_latency_test.md)

### Week 4：多路播放与并发

- [首批四路 RTMP 独立播放](weeks/week4/week4_multi_stream_playback.md)
- [16 路动态连接、解码架构与性能验收](weeks/week4/week4_sixteen_stream_validation.md)
- [16 路模块变更与测试操作记录](weeks/week4/week4_release_test_and_module_changes.md)
- [新对话公开交接文档](weeks/week4/week4_conversation_handoff.md)

### Week 5：设备状态、日志与重连

- [设备状态、日志与可配置重连](weeks/week5/week5_device_status_and_logging.md)

### Week 6：产品级 OpenGL 渲染与实证

- [产品级 OpenGL 视频渲染与验证总览](weeks/week6/week6_opengl_environment_and_validation.md)
- [产品视频渲染框架教学篇](weeks/week6/week6_product_rendering_framework_tutorial.md)
- [CPU/OpenGL 自动化对照测试实战篇](weeks/week6/week6_renderer_performance_test_guide.md)
- [正式 Renderer 对照脱敏结果 JSON](weeks/week6/week6_renderer_comparison_results.json)

### Week 7：SRS Server 接入

- [SRS 接入实施与逐 Phase 验收记录](weeks/week7/week7_srs_server_integration.md)
- [SRS 新手完全指南：概念、使用、配置与项目联动](guides/build-and-testing/srs_beginner_guide.md)
- [SRS 异常恢复与发布门禁验证指南](guides/build-and-testing/srs_failure_recovery.md)

## 4. 推荐阅读路径

只熟悉 Week 1～3 时：

1. [项目计划](roadmap/project_plan.md)
2. [Week 4～5 架构指南](guides/architecture/week4_week5_architecture_guide.md)
3. [多路媒体与并发深度学习](guides/architecture/week4_media_concurrency_deep_dive.md)
4. [Week 5 实现记录](weeks/week5/week5_device_status_and_logging.md)
5. [Week 6 产品视频渲染框架教学篇](weeks/week6/week6_product_rendering_framework_tutorial.md)
6. [Week 6 自动化对照测试实战篇](weeks/week6/week6_renderer_performance_test_guide.md)

进行日常维护时：

1. 先用架构指南确定模块职责。
2. 再阅读对应周次的实现与测试文档。
3. 修改前核对开发规范，修改后按构建与验证文档执行检查。

## 5. 文档归档约定

- [旧版根 README](archive/README_legacy_v0.1.0-alpha.1.md)：保留发布前的长篇项目介绍和历史结果，不作为当前操作入口。
- [已退役的本机测试脚本](archive/legacy_test_scripts.md)：记录删除原因、替代入口和 Git 查看方式。
- [Week 6 OpenGL 暂停与恢复记录](archive/week6_opengl_pause_and_recovery_2026-08-04.md)：保留已完成问题的历史恢复过程，不作为当前操作入口。
- 项目目标和进度放入 `roadmap/`。
- 跨周复用的架构、开发和构建资料放入 `guides/`。
- 某一阶段的实现、测试和交接记录放入 `weeks/weekN/`。
- `docs/project_handoff.md` 是被 Git 忽略的本地临时交接材料，不作为公开索引中的权威文档；需要共享的交接内容应放入对应周次目录。
