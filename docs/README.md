# RtmpMonitor 文档索引

项目文档按“路线规划、通用指南、分周实现”分类。第一次接触项目时，建议先阅读项目计划和架构总览，再按周次进入具体实现。

## 0. 跨会话记忆

- [记忆系统说明](memory/README.md)：文件职责、恢复流程、维护规则和 OpenViking 边界。
- [当前项目快照](memory/project_snapshot.md)：经验证的当前状态、阻塞和下一步。
- [重要设计决策](memory/decisions.md)：影响长期架构、平台、存储和安全的轻量 ADR。
- [已知问题](memory/known_issues.md)：可复现问题和未完成的验收项。
- `project_handoff.md`：本地会话交接文件，由 `.gitignore` 排除。

## 1. 路线规划

- [项目计划](roadmap/project_plan.md)：项目目标、阶段安排、验收标准和当前进度。
- [v0.1.0-alpha.1 版本冻结与交付任务清单](roadmap/v0.1.0_alpha1_release_handoff_checklist.md)：源码交接、Windows 测试包、ARM 真机和最终发布门禁。

## 2. 通用指南

### 2.1 架构与深入学习

- [Week 4～5 架构、维护与深入学习指南](guides/architecture/week4_week5_architecture_guide.md)：从 Week 3 过渡到多路播放、状态、重连和日志框架。
- [Week 4 多路媒体与并发深度学习](guides/architecture/week4_media_concurrency_deep_dive.md)：线程模型、包队列、共享解码池和代码调用链详解。
- [用户事件、系统日志与审计日志架构](guides/architecture/logging_architecture.md)：三类信息边界、配置、轮转、脱敏和维护入口。
- [嵌入式设备分级与 Linux 双后端实施方案](architecture/embedded_device_rendering_strategy.md)：明确无 GPU 的 Qt Raster/QImage/QPainter/linuxfb 路径、有 GPU 的 OpenGL ES 3.0 路径、CMake 可裁剪构建、板级路数自测和 WSL2 ARM64 验收边界，可直接交给 Kimi K3 实施。

### 2.2 开发规范

- [代码规范](guides/development/code_style_guide.md)
- [注释规范](guides/development/comment_style_guide.md)
- [QSS 样式加载与主题扩展](guides/development/style_loading.md)
- [OpenViking 使用、结构观察与测试指南](guides/development/openviking_usage_and_testing.md)：理解上下文数据库、Hook、VikingFS、Web Studio 和可清理的学习实验。

### 2.3 构建与验证

- [Windows x64 与 Linux ARM64 跨平台构建](guides/build-and-testing/cross_platform_build.md)
- [Linux 双路径渲染构建（RASTER/GLES3/AUTO）与板级资格脚本](guides/build-and-testing/linux_dual_render_build.md)
- [SRS 新手完全指南：概念、使用、配置与项目联动](guides/build-and-testing/srs_beginner_guide.md)
- [RTMP 推流链路验证脚本](guides/build-and-testing/rtmp_chain_verification.md)
- [SRS 异常恢复与发布门禁验证指南](guides/build-and-testing/srs_failure_recovery.md)

## 3. 分周实现

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

- 项目目标和进度放入 `roadmap/`。
- 跨周复用的架构、开发和构建资料放入 `guides/`。
- 某一阶段的实现、测试和交接记录放入 `weeks/weekN/`。
- `docs/project_handoff.md` 是被 Git 忽略的本地临时交接材料，不作为公开索引中的权威文档；需要共享的交接内容应放入对应周次目录。
# 渲染架构（2026-08-03）

- [当前视频渲染架构审查](architecture/current_rendering_architecture_review.md)
- [GoObject PDF 渲染框架映射](architecture/pdf_rendering_framework_mapping.md)
- [产品级视频渲染框架](architecture/video_rendering_framework.md)
- [嵌入式设备分级与通用显示优化方案](architecture/embedded_device_rendering_strategy.md)
- [ADR：单主 OpenGL 画布与临时全屏画布](architecture/adr/001-video-rendering-architecture.md)
