# WebRTC V2 Week 3 自动测试结果

## 环境与范围

- 日期：2026-08-21
- 平台：Windows 10 x64，Visual Studio 2022/MSVC，Qt 6.6.1
- 依赖：FFmpeg 8.1.2；ON 路径为 libdatachannel 0.24.5
- 配置：既有独立 Windows Debug OFF/ON 构建目录重新配置并全目标增量构建
- 范围：H.264 契约、外部解码入口、RTMP/AAC 回归、Week 2 WebRTC 回归、层依赖和完整 CTest

测试使用 46 字节的离线合成 16×16 黑色 Annex-B SPS/PPS/IDR 固定向量，原始 x264 SEI 已移除。
它不含用户画面、网络信息或外部文件依赖，只用于证明 AU 能进入实际 FFmpeg decoder 和共享邮箱；
它不是 Week 6 社团测试视频，也不解除社团资产的许可/来源/分发门禁。

## 实际命令与结果

```powershell
cmake --build out/build-windows-x64/webrtc-week2-off --config Debug -j 4
ctest --test-dir out/build-windows-x64/webrtc-week2-off -C Debug --output-on-failure

cmake --build out/build-windows-x64/webrtc-week2-on --config Debug -j 4
ctest --test-dir out/build-windows-x64/webrtc-week2-on -C Debug --output-on-failure
```

| 配置 | 全目标构建 | CTest | 结论 |
| --- | --- | ---: | --- |
| WebRTC OFF | 通过 | 39/39，126.26 秒 | 主程序、RTMP/AAC、UI、MQTT、OFF 产物和层门禁通过 |
| WebRTC ON | 通过 | 41/41，126.87 秒 | 上述回归及 Week 2 session/H.264 API/loopback 通过 |

最终审计又在当前 ON 产物上启动了两个独立隐藏 probe 进程。Offer 包落盘后才启动 Answer；两端
均按 `description_exported,connected` 结束且退出码为 0，脱敏 session 一致，candidate 类型仅
`host`，stderr/敏感模式/最终会话文件计数均为 0。这证明 Week 3 变更后真实文件信令交换仍正确。

专项测试还单独执行：

```powershell
ctest --test-dir out/build-windows-x64/webrtc-week2-on -C Debug `
  --output-on-failure `
  -R "rtmp_monitor_(h264_contract|encoded_video_decode)_test"
```

结果 2/2 通过。此前包含 RTMP façade 与多路 manager 的四项专项组合结果为 4/4 通过。

一次 OFF 全量重复运行曾在 `rtmp_monitor_ffmpeg_player_test` 退出时出现 Windows heap
错误。独立 MSVC AddressSanitizer 构建把问题定位到新增测试自身的局部变量析构顺序：测试中的
lambda 在 `QStringList` 已析构后仍接收 `FFmpegPlayer` 析构阶段的兼容状态信号。调整测试观察者
寿命后，同一 ASan lifecycle 测试连续 5 轮通过且无 sanitizer 报告，随后 OFF/ON 全量 CTest 均
通过。该问题未进入产品实现，不构成未解决的 known issue。

最终代码审计还发现层门禁中的 media→WebRTC contract 检查曾误放在 `webrtc_dev` 循环。检查已
移到 media 循环，避免漏检同时允许探针未来合法复用低层会话契约；OFF/ON 重新全目标构建通过，
两边的层依赖专项测试均为 1/1 通过。

## 文档与改动范围审计

- `git diff --check` 通过；只有 Git 对现有 Windows 换行策略的提示，没有空白错误。
- WebRTC V2 总 WBS 为 135 项、440 人时；ID 135/135 唯一，重复 ID 和缺失前置引用均为 0。
- 本轮涉及的 17 份 Markdown 本地链接扫描缺失为 0。
- 工作区范围扫描没有发现 app、ui、profiles、server、保存流 schema 或其他未授权生产模块改动；
  变更只落在已授权的 CMake/层门禁、低层契约、media、Week 2 隔离边界、测试与文档范围。
- 分支仍为 `Beta`，HEAD 未改变；没有提交、推送或创建标签。

## 覆盖证明

- `rtmp_monitor_h264_contract_test`
  - 三/四字节 Annex-B separator、空/无 NAL/错 separator、负时间戳和大小边界；
  - Offerer/Answerer × SendOnly/ReceiveOnly 四种正交组合；
  - WebRTC session 配置只包含信令角色、媒体方向和运行时 ICE 材料。
- `rtmp_monitor_encoded_video_decode_test`
  - 固定 Annex-B IDR 实际解码为 16×16 帧并进入原 `LatestFrameMailbox`；
  - 空、超容量、等待关键帧、旧 generation、关闭后提交的分类结果；
  - 容量 1 压缩队列溢出可观察，队列最终为 0；
  - manager handle 连续十轮创建/重复关闭/撤销，无残留 StreamId；
  - 关闭后晚到状态不能把流从 Disconnected 复活。
- `rtmp_monitor_ffmpeg_player_test`
  - 原 RTMP URL 拒绝、error→state signal 顺序、重连中断、有限重试和幂等停止；
  - 可选真实 RTMP 环境测试入口保持不变。
- `rtmp_monitor_multi_stream_test` 与完整 CTest
  - 0～16 路稳定 StreamId、共享 worker、显式单路音频、指标 schema、停止和原产品回归；
  - Week 2 真实 libdatachannel 文件/session、H.264 API 和无 STUN/TURN DataChannel 回环未回归。

## 未由本轮证明

- 未创建 WebRTC Track，未发送或接收 RTP H.264，未证明 P2P 出画。
- 未实现 MP4 publisher、双角色测试客户端、产品 UI、LAN 双机或公网链路。
- W2 双控制台人工 Offer/Answer、文件清理和人工隐私检查仍待用户补充。
- 未执行 ARM64 交叉构建或 ARM 真机测试；Week 3 变更新增 FFmpeg/media 源，跨平台资格应在后续
  Beta/ARM 门禁中重新验证。
