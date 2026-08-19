# RtmpMonitor RTMP V1 文档

本目录对应当前稳定 RTMP 产品线 `0.1.0-alpha.1`。这里记录已经实现或曾经验证过的架构、指南、周次和历史资料；文档中的历史测试结果不自动代表新机器或未来版本。

## 架构

- [渐进式架构解耦](architecture/progressive_decoupling_architecture.md)
- [保存推流与 MQTT 设备控制](architecture/saved_stream_and_mqtt_device_control.md)
- [单车值守闭环](architecture/mobile_security_single_vehicle_operator_loop_design.md)
- [当前视频渲染架构审查](architecture/current_rendering_architecture_review.md)
- [外部渲染框架映射](architecture/pdf_rendering_framework_mapping.md)
- [产品级视频渲染框架](architecture/video_rendering_framework.md)
- [低延迟单向音频](architecture/low_latency_audio_stream.md)
- [嵌入式渲染策略](architecture/embedded_device_rendering_strategy.md)
- [SRS Server 接入](architecture/srs_server_integration_plan.md)
- [渲染架构 ADR](architecture/adr/001-video-rendering-architecture.md)

## 指南

- [跨平台构建](guides/build-and-testing/cross_platform_build.md)
- [嵌入式开发交接](guides/build-and-testing/embedded_developer_handoff.md)
- [Linux 双路径渲染](guides/build-and-testing/linux_dual_render_build.md)
- [SRS 新手指南](guides/build-and-testing/srs_beginner_guide.md)
- [SRS 故障恢复](guides/build-and-testing/srs_failure_recovery.md)
- [RTMP 历史链路验证](guides/build-and-testing/rtmp_chain_verification.md)
- [Windows 摄像头资格测试](guides/testing/windows_camera_validation.md)
- [架构学习指南](guides/architecture/week4_week5_architecture_guide.md)
- [多路媒体与并发](guides/architecture/week4_media_concurrency_deep_dive.md)
- [日志架构](guides/architecture/logging_architecture.md)
- [开发规范](guides/development/code_style_guide.md)
- [注释规范](guides/development/comment_style_guide.md)
- [样式加载](guides/development/style_loading.md)
- [OpenViking 使用与测试](guides/development/openviking_usage_and_testing.md)

## 分周记录

- [Week 2：动态界面](weeks/week2/)
- [Week 3：单路播放与延迟](weeks/week3/)
- [Week 4：多路播放与并发](weeks/week4/)
- [Week 5：状态、日志与重连](weeks/week5/)
- [Week 6：产品级渲染与实证](weeks/week6/)
- [Week 7：SRS Server 接入](weeks/week7/)

## 历史归档

- [旧版根 README](archive/README_legacy_v0.1.0-alpha.1.md)
- [遗留测试脚本索引](archive/legacy_test_scripts.md)
- [Week 6 OpenGL 暂停与恢复](archive/week6_opengl_pause_and_recovery_2026-08-04.md)
