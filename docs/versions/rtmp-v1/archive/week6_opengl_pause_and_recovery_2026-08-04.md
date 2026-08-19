# Week 6 OpenGL 对照测试恢复记录（2026-08-04）

> 历史记录：本文提到的本机性能脚本已经退役，不是当前操作入口。参见[遗留脚本索引](legacy_test_scripts.md)。

> 最终结论：用户恢复任务后，首帧黑屏问题已修复；四组 600 秒性能与 Quality 门禁全部通过，默认 Renderer 已切换为 `auto`。本文件保留原暂停点和恢复过程供审计。

## 暂停原因与安全状态

用户要求暂停，因为当前桌面环境每次启动 RTMP/GUI 测试都需要人工授权。已立即终止正在运行的 CPU 诊断，并通过各脚本的 `Stop` 路径按 PID、可执行文件路径和启动时间清理。

暂停时只读复核结果：

- TCP 1935 未监听。
- 未发现 `rtmp_monitor`、`ffmpeg` 或 `nginx` 测试进程。
- 没有执行四组 600 秒正式测试。
- 暂停当时 CLI 默认后端仍为 `cpu`；随后只在四组正式门禁全部通过后切换为 `auto`。
- 所有修改仍在用户原有 dirty worktree 中，未提交、未暂存、未推送。

## 本次已经实现

- 指标 schema 已从 v2 升至 v3，增加请求/实际 Renderer、fallback、Desktop GL/ES、vendor、renderer、version，以及根级渲染统计。
- Windows Desktop GL 增加双槽、非阻塞 `GL_TIME_ELAPSED` 查询；结果尚不可用时保持 `-1`，不等待 GPU。
- `test_week6_opengl.ps1` 会构建生产 Renderer，运行 OpenGL 标签测试和完整 CTest，并单独导出 framebuffer 质量数值与差异图。
- `test_16_stream_video.ps1` 与 `test_16_stream_live_latency.ps1` 已统一按逻辑处理器数归一化 CPU，记录实际 Renderer、FPS、frame age、内部延迟、paint/upload/GPU 时间、纹理字节、覆盖计数、百分位和工作集斜率。
- 新增 `compare_renderers.ps1`，具备 `Check|Run|Status|Stop|SelfTest` 和 `Video|LiveLatency|Quality|All` 接口。
- 对照总控已改为 CPU/OpenGL 共用同一批预编码素材；修复了 PowerShell 5.1 拆分 FFmpeg `drawtext` 空格参数、编码失败退出码被误判、子门禁 stderr 提前终止汇总等脚本问题。
- 新增 Snapshot 诊断计数：RenderItem、可见 RenderItem、绑定邮箱数量。

## 已取得但需正确解读的证据

恢复任务后重新运行完整 Windows Debug CTest：12/12 通过，总耗时 75.48 秒。生产 framebuffer 用例覆盖 YUV420P/NV12、BT.601/709/2020 NCL、Limited/Full；8 例均通过，观测到：

- 最低 PSNR：46.0896 dB。
- 最大平均绝对误差：0.8889。
- 最大 P99 通道误差：2。
- 门槛分别为 35 dB、3、8。

这证明测试覆盖的颜色转换相对 CPU 参考无可见退化，不代表 OpenGL 天然更清晰。

30 秒教学性 Video A/B 已成功进入实际 `opengl` 后端并记录 NVIDIA GL 身份与非零纹理字节，但结果不能作为资格结论。该短测曾发现 CPU 组 `renderItemCount=16`、`boundMailboxCount=16`，而 `visibleRenderItemCount=0`；第一层修复是让 Snapshot 只依据业务 `frameVisible`，不把 QWidget 瞬时可见性固化为长期渲染状态。

恢复后根据用户提供的两段录像继续定位，发现决定性的第二层根因：`VideoGridWidget` 把 `VideoWidget::renderStateChanged` 连接到 lambda，同时指定 `Qt::UniqueConnection`。Qt 6 不支持以这种形式对 lambda 做唯一连接，运行时会拒绝连接，因此首帧到达时 Snapshot 根本没有刷新；进入全屏/退出全屏触发了另一条刷新路径，才让四路画面一起出现。现已改为连接到 `VideoGridWidget::handleRenderStateChanged` 成员函数，并保留 Snapshot 的业务可见性语义。

验证证据：

- 新增 `transientWidgetVisibilityDoesNotSuppressSharedCanvas()` 回归，测试不再手工调用刷新函数，而是验证 `showFrame()` 信号链能够更新 Snapshot。
- `VideoGridDynamicTest` 29/29 通过，且不再出现 `unique connections require a pointer to member function` 警告。
- Windows Debug 完整 CTest 12/12 通过。
- 复用本机既有四路 RTMP 输入，只启动修复后的客户端；未做任何双击或全屏操作，四路在连接后立即显示。
- schema v3 指标记录 4 路 Playing、4 个 RenderItem、4 个可见 RenderItem、4 个绑定邮箱，`uploaded=1600`、`rendered=1636`、`paintCalls=419`。

短测产生的 CPU/OpenGL 数值受 30 秒窗口、10 秒故障注入和上述 CPU 可见性缺陷影响，不得写入正式性能结论。

## 后续执行顺序

下次会话先执行不启动 GUI 的检查：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\compare_renderers.ps1 -Action SelfTest
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\compare_renderers.ps1 -Action Check -Suite All
```

恢复后上述步骤均已完成：120 秒 Video/LiveLatency 演练发现并修正了相对 metrics 路径、15 FPS 双重节流和状态文件共享竞态；四组 600 秒正式门禁与 Quality 全部通过，脱敏结果已回填 Week 6 总览，CLI 默认已切换为 `auto` 并完成 12/12 回归。

任意阶段需要停止时：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\compare_renderers.ps1 -Action Stop
```

原始短测材料和报告位于被忽略的 `out/`，不应提交；后续正式结果只提交脱敏摘要。
