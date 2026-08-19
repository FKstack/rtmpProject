# RtmpMonitor 当前项目快照

> WebRTC V2 规划状态（2026-08-19）：本地 `Beta` 已先精确恢复到远端稳定基线
> `116e33b`，提前产生的 WebRTC 提交、未提交 RTP 重组源码、Go 信令服务及其测试均已撤销；
> `out/build-windows-x64/beta-vs`、`out/build-windows-x64/debug` 和 `out/go-cache` 已删除。
> Windows ZIP 保持 23,189,479 字节，Linux ARM64 tar.gz 保持 12,509,074 字节，本机 vcpkg 的
> libdatachannel 0.24.5 及依赖保留。`Beta` 当前只新增 WebRTC 总计划、新手指南、Week 1～12
> 任务书和文档入口，没有修改 CMake、生产源码、schema 或测试代码；WebRTC 实现、构建和运行
> 证据均为“尚未开始”，不得把计划目标表述为已交付能力。

> Linux ARM64 RASTER 打包状态（2026-08-18）：从干净 Alpha `89483c0` 创建
> `codex/linux-arm64-package`，新增严格的 `scripts/package_linux_arm64.sh`、linuxfb 启动器、`qt.conf`、
> ARM64 专用说明和第三方通知。在独立目录使用 Jammy ARM64 sysroot、AArch64 GCC 11、Qt 6.2.4、
> FFmpeg 8.1.2、Paho MQTT C 1.3.16 完成 Release/RASTER 交叉构建；生产 OpenGL 后端和测试目标关闭。
> 产物 `out/packages/RtmpMonitor-0.1.0-alpha.1-linux-arm64-raster.tar.gz` 为 12,509,074 字节，阶段目录
> 41 个文件、归档 56 个条目，随包 12 个 Qt/FFmpeg/Paho 动态库并列出 87 个 Jammy 系统 SONAME
> 依赖。全新解压与阶段目录逐文件一致，AArch64 ELF、FFmpeg LGPL 8.1.2、QEMU `--version`、禁止
> 源码/调试/用户状态/SRS/DVR 内容及回环端点门禁通过；不计算、不比对也不生成 SHA-256。该包仍为
> Engineering Preview，真实 ARM 板的 glibc/QPA/framebuffer/输入/字体/音频/视频与性能待实机验收。

> 模块四 PoC 状态（2026-08-16）：已从最新 Alpha `db84951` 创建 `codex/module4-srs-dvr-poc`，新增
> 默认不被加载的 SRS DVR 配置模板、Python 标准库回环回调适配器、schema v1 原子分段收据、14 项
> 单元测试和独立 WSL/Linux 故障矩阵。固定 SRS 6.0.184 实测连续分段、关键帧起始、断流尾段、重复/
> 乱序 callback、适配器和 SRS 分别重启、低磁盘拒绝、适配器离线时推拉流继续及原始
> `srs-minimal.conf` 零 DVR/spool 副作用全部通过。PoC 不读取文件内容、不执行内容哈希、不进入
> Windows 发布包，也不接入 Qt、事件或 EvidenceService。Windows Debug 全目标与 CTest 36/36
> （129.49 秒）、Windows Release、ARM64 RASTER/GLES3 全目标、AArch64 ELF/动态依赖审计及 QEMU
> 逻辑门禁通过；RASTER 仍无 OpenGL 依赖。事件录像关联、回放、导出、保留策略、服务化安装和 ARM
> 现场磁盘资格仍延期。

> 模块三 Windows 便携包状态（2026-08-16）：基于 `v0.1.0-alpha.1` 提交 `1155aa7` 使用独立干净
> Release 目录完成全量构建，并在同时清空宿主 `PATH`/`Path` 后以 Release、Qt 6.6.1、vcpkg Release
> 与 Windows System32 的收敛路径通过 CTest 36/36（95.39 秒）。新包位于
> `out/packages/module3-1155aa7/RtmpMonitor-0.1.0-alpha.1-windows-x64.zip`，包含 38 个条目、
> 23,189,479 字节；不计算或生成 SHA-256，不调用微软签名。全新目录解压后 38 个文件、版本 CLI、
> 禁止 Debug/开发/用户配置文件、Qt OpenSSL/FFmpeg 媒体插件与内容端点扫描通过；唯一运行端点是
> 示例配置中的回环 `127.0.0.1`，其余 URL 来自许可证/来源声明或符号占位符。Codex Windows 宿主
> 拒绝创建隔离 GUI 进程，因此本次 GUI 双击启动仍待人工验证，不冒充通过。

> 模块三实施状态（2026-08-16）：已从最新 `v0.1.0-alpha.1` 基线创建
> `codex/module3-evidence-export`，新增 Qt Core + Qt Gui 的 `rtmp_monitor_evidence`、schema v1
> `QSaveFile` 原子证据目录、有界单线程截图写入、事件详情、无有效画面失败登记、事件目录导出、
> 启动一致性恢复和本地证据子系统事件。事件存储向后兼容 v1 并写入 schema v2；EvidenceCatalog
> 是关联唯一事实源，事件 `evidenceIds` 是可重建投影。按产品决定不执行 SHA-256 或其他内容哈希
> 完整性校验，UI 与 manifest 明确不提供防篡改、来源真实性或可信时间戳保证。Windows Debug 全目标
> 与 CTest 36/36、Windows Release 全目标、ARM64 RASTER/GLES3 全目标、AArch64 ELF/动态依赖审计和
> QEMU 逻辑测试通过。MQTT/心跳/RTMP/FFmpeg/media/render/硬件契约未改；SRS DVR 仍未实施，
> ARM 真机和现场证据流程待人工验收。

> 模块二 Phase 2A 实施状态（2026-08-16）：已从最新 `v0.1.0-alpha.1` 基线创建
> `codex/module2-event-center`，新增 Qt Core-only `rtmp_monitor_event_center`、schema v1 `QSaveFile`
> 原子 JSON、八类 MQTT/心跳/RTMP/SRS/本地控制/人工事实事件、应用层只读 `PlatformEventBridge`，
> 以及默认隐藏的底部“平台事件中心”和状态栏活动事件徽标。系统事件只能由匹配观察恢复；未观察到
> 恢复时必须二次确认并填写原因才能关闭。损坏、高版本或写失败会保留原文件并只禁用事件写入，
> 不影响播放和安全停车。稳定资源只保存 camera/device ID 或脱敏 SHA-256；不保存完整 URL、Broker、
> payload 或凭据。Windows Debug 全目标与 CTest 34/34（131.13 秒）、Windows Release 全目标、ARM64
> RASTER/GLES3 全目标、AArch64 ELF/动态依赖审计和 QEMU 逻辑测试通过；RASTER 未引入 GL/EGL/GLES。
> 该记录描述模块二交付时点；事件详情、截图证据和目录导出后续已由上方模块三完成，SRS DVR 未实施。
> 自动化仍不证明 Broker/设备接收或车辆执行。

> 模块一实施状态（2026-08-15）：已从固化并推送的 `v0.1.0-alpha.1` 基线创建
> `codex/module1-control-safety`，落地 Qt Core-only `rtmp_monitor_control_policy`、应用层
> `DeviceControlTransport`/MQTT 适配器、统一车辆移动解锁、100 ms RTMP/真实呈现帧新鲜度检查、
> 首次失效单次停车、原目标待补发快照和诚实控制审计。`MqttDeviceClient` payload、心跳、Topic、QoS、
> publish 返回契约以及 RTMP/FFmpeg/media/render 链路未修改；事件中心、证据和 SRS DVR 未实施。
> Windows Debug 全量 CTest 31/31（115.31 秒）、Windows Release 全构建、ARM64 RASTER/GLES3 全目标
> 构建、AArch64 ELF/依赖审计与 QEMU 逻辑测试通过。受控车辆台架的“按住移动、释放停车”、ARM 真机
> QPA/GPU/视频仍待人工验证；自动化结果只证明本地门禁与提交语义，不证明 Broker 或设备接收/执行。

> 单车值守闭环设计状态（2026-08-15）：在不改变 MQTT 控制/心跳返回、Topic、QoS、设备固件和硬件
> 的前提下，已形成 `docs/architecture/mobile_security_single_vehicle_operator_loop_design.md` 架构
> 基线，并经产品经理会话两轮只读评审最终接受，记录为 ADR-031。当前可实施顺序收敛为本地控制安全
> 与诚实审计、基于现有 MQTT/心跳/RTMP/SRS 信号的平台
> 事件、截图证据与目录导出，以及默认关闭的 SRS DVR PoC。命令 ACK、多车、TLS/RBAC、遥测、地图、
> 巡逻、SOS、AI、对讲和动态码率明确延期，不增加占位生产接口。该文档定义未来 R2 边界与门禁，
> 设计评审本轮当时没有修改生产代码；其 Phase 1 后续实施状态与验证证据以上方最新记录为准。

> 移动安防产品规划状态（2026-08-15）：已依据当前源码/CMake/测试、现有单车 MQTT 心跳与控制边界，
> 以及海康 HikCentral、大华 DSS、Axis Camera Station、Milestone XProtect、Knightscope/SMP 等公开
> 产品资料，形成 `docs/roadmap/mobile_security_product_module_recommendations.md`。建议在不改变
> RTMP/SRS/FFmpeg/渲染底座的前提下，优先建设定向控制回执与失联安全、鉴权/RBAC/控制权、事件
> 中心、录像证据，再扩展地图、多车、巡逻、运维、对讲、AI 和弱网策略。该文档是待产品负责人按
> 具体试点场景确认的建议，不等于路线图已批准，也没有改变生产代码、协议、schema 或运行时行为。

> MQTT 设备心跳与定向推流状态（2026-08-15）：设置 schema 升级为 v2，v1 本机配置读取时自动
> 补充状态 Topic；安全默认仍为 MQTT 禁用、Broker 为空。单个 Paho session 以 QoS 0 同时订阅
> `device/control` 与 `device/status`，两项 SUBACK 均成功后才 Connected。RTMP URL 末段绑定设备
> ID，视频卡保存稳定 StreamId 控制目标并独立显示 Waiting/Online/Offline/Unavailable；心跳按本地
> 单调时钟在 30 秒边界离线，缓存上限 64。`startStream` 写入所选卡运行时 URL 到 `data.url`，观察
> 列表统一脱敏；Start/移动要求设备 Online，StopStream/StopCar 在存在目标时不受心跳过期阻断。
> 当前控制 payload 没有 `client_id`，同一控制 Topic 仍只允许一台真正受控设备。Windows Debug
> 干净全构建与 CTest 29/29（127.76 秒）、Windows Release、ARM64 RASTER/GLES3 全目标构建通过；
> 两者均为 AArch64 ELF，RASTER 直接 NEEDED 无 Qt6OpenGL/EGL/GLES。用户已在真实设备链路确认
> MQTT 连接、控制/状态双 Topic 订阅、设备 Online、目标视频拉流均成功；车辆动作安全验收仍应
> 在受控台架中单独执行。
> ARM64 总控脚本的 `both` 模式已改为先物化模式数组，避免 CMake 消费循环 stdin 后只构建 RASTER；
> 脚本自测试及修复后的 RASTER→GLES3 双模式无工作增量复验通过。

> 最新普通便携包（2026-08-15）：清理并完整重建 Windows Release 后，在仅包含 Release、Qt、vcpkg
> 与 Windows System32 的运行库路径下 CTest 29/29（98.13 秒）通过。重新生成
> `out/packages/RtmpMonitor-0.1.0-alpha.1-windows-x64.zip`，共 38 个条目、23,004,877 字节；不生成
> SHA-256、不调用微软签名、不携带用户 MQTT/保存推流配置、PDB、测试媒体、VC 安装器或 Qt FFmpeg
> 媒体后端。包内 app-local MSVC 运行库为 14.44.35211.0，必需 Qt Multimedia、Windows 音频插件、
> FFmpeg/swresample 与 Paho DLL 齐全；全新解压后的 `--version` 冒烟通过，已知现场端点命中为 0。

> 网络端点安全基线（2026-08-15）：MQTT 首次启动改为 `enabled=false`、Broker 为空、Topic
> `device/control`，无配置时不会执行 DNS、TCP、MQTT CONNECT 或订阅。禁用状态允许空 Broker；
> 启用或测试连接时必须填写合法 `mqtt://主机[:端口]`。已有合法本机 AppConfig 继续兼容，真实端点
> 只保存在本机，不进入源码、文档示例或发布包默认配置。仓库规则已增加公网端点泄露即停止发布并
> 脱敏历史的硬性门禁；`master` 与 `v0.1.0-alpha.1` 由同一个脱敏单根提交重建。Windows Debug
> 全目标构建及 CTest 29/29、Windows Release 全构建、ARM64 RASTER/GLES3 增量构建和依赖审计
> 已通过，RASTER 未引入 GL/EGL/GLES。普通便携 ZIP 包含 38 个文件，不生成 SHA-256、不执行微软
> 签名；脱敏根 Tree、打包目录和全新解压目录的旧现场端点命中均为 0。临时原子移开既有本机配置后，
> 全新 ZIP 在真正无配置场景隐藏启动 5 秒，外部 TCP 连接数为 0；原本机配置随后原样恢复。
> 远程脱敏分支复核完成后，本地三个历史重建临时 ref 与敏感 Bundle 已删除，reflog 已过期并执行
> `git gc --prune=now`；两个已知现场地址在当前工作树、全部 refs 与现有便携 ZIP 中均为 0 命中，
> `git fsck --unreachable --no-reflogs` 未发现残留不可达对象。该本地历史备份与旧对象不可恢复。

> Windows 单路全屏回归状态（2026-08-15）：已修复“已有画面时第一次进入全屏仍沿用隐藏画布
> 旧尺寸，画面缩在左上角”以及跨会话 `BlankCursor` 残留。全屏窗口现在先显示画布并同步零边距
> 布局，再按最终尺寸发布 `RenderSnapshot`；Windows/DPI 延迟布局由带会话代次保护的下一事件循环
> 同步收敛。进入前强制恢复 ArrowCursor，退出先关闭 Chrome presentation 并失效旧计时回调。
> Windows Debug 全量 CTest 29/29、Release 全构建、ARM64 RASTER/GLES3 增量交叉构建和依赖门禁
> 均通过，RASTER 无 GL/EGL/GLES。阿里云正常 MP4 → FFmpeg → WSL2 SRS → Windows Debug 的
> 真实链路中，`auto` 首次进入与后续 10 次往返均为 1920×1080 且画面正常铺开，每次进入光标
> 均立即可见；`cpu` 首次进入 A/B 同样通过。现场证据位于忽略目录
> `out/fullscreen-regression-20260815/`；NVIDIA Overlay 与 Visual Studio F5 组合仍由维护者按发布
> 清单复核。修复后的最新 Release 已重新生成普通便携 ZIP
> `out/packages/RtmpMonitor-0.1.0-alpha.1-windows-x64.zip`；不执行 SHA-256 或微软签名，包内 38 个
> 条目、21.92 MiB，必需 Qt Multimedia/FFmpeg/Paho/本地 MSVC 运行库齐全且不含 VC 安装器、PDB、
> 测试素材或 Qt FFmpeg 媒体插件。全新目录解压后的版本检查与 5 秒 GUI 启动冒烟通过。
>
> 低延迟单向音频状态（2026-08-15）：RTMP/FLV 输入支持可选 AAC-LC；全应用唯一
> `AudioPlaybackEngine` 在专用 Qt 事件循环线程完成 AAC 解码、48 kHz mono S16 重采样、100 ms
> 有界 PCM、12 包/256 KiB 压缩包队列和默认 `QAudioSink` 输出。Sink 请求 60 ms 缓冲，真实欠载后
> 以 40 ms 预缓冲恢复；A/V 同步在全屏画布交接时从陈旧呈现 PTS 回退到同代次最新解码 PTS。
> 阿里云境内正常 MP4 → FFmpeg H.264/AAC → WSL2 SRS 6.0.184 → 正式播放器的三轮 320 秒和一轮
> 600 秒全部通过；600 秒为 647 样本、P50 89.561 ms、P95 111.632 ms、最大 125.396 ms、Sink
> 欠载 0。真实 GUI 全屏复测和最终 ZIP 解压后播放/静音/退出冒烟均无欠载。Windows Debug/Release
> CTest 均为 29/29；ARM64 RASTER/GLES3 交叉构建与门禁通过，RASTER 无 GL/EGL/GLES。最终便携包为
> `out/packages/RtmpMonitor-0.1.0-alpha.1-windows-x64.zip`，包含 app-local MSVC 运行库、Qt Windows
> 音频插件和 swresample，不含测试素材、Qt FFmpeg 媒体插件或 VC 安装器。软件延迟口径止于
> QAudioSink 写入；声学硬件与 ARM 真机 ALSA 仍待验证，见 ISSUE-014。
>
> 音频文档状态（2026-08-15）：`docs/architecture/low_latency_audio_stream.md` 已按当前源码扩展为完整
> 框架与手工测试指南，逐类记录组合根、RTMP 分轨、包所有权、单例音频引擎、线程/停止顺序、
> A/V 时钟、UI 转发、指标和资格观察接缝；同时提供 Windows 三终端 MP4 → FFmpeg → WSL2 SRS →
> RtmpMonitor 的可执行推拉流步骤、GUI/全屏静音检查、指标判断、自动资格入口和常见故障排查。
>
> 公开文档整理状态（2026-08-14）：根 README 已同步深石墨 UI、Dock 优先级、桌面双模式控制和
> Windows Debug 27/27 最新验证；`docs/README.md` 重新按记忆、路线、架构、指南、分周和归档建立
> 单一索引。SRS 实施基线已移入 `docs/architecture/`，完成的 Week 6 暂停/恢复过程已移入
> `docs/archive/`，全部仓库引用同步更新；根级 `/Testing/` 忽略规则不再误伤
> `docs/guides/testing/`。本机交接、构建产物、测试媒体和凭据配置继续排除在公开仓库之外。

> Dock 优先级状态（2026-08-14）：`MainWindow` 已将四个角落归属左右操作 Dock，设备控制无论停靠
> 左侧还是右侧都保持全高；事件消息只占中央视频列的上方或下方，不再截断摇杆、停车按钮或安全
> 提示。事件消息默认隐藏、菜单开关、自由调高和监控墙行为保持不变。新增 1280×720、日志调高、
> 左右换边及隐藏回收几何回归；Windows Debug 全目标构建和 CTest 27/27（120.83 秒）通过，
> 1280×720 实际截图复核通过。该 R1 修改未改变 Dock 所有权、公共接口、依赖或生命周期。

> 统一 UI 状态（2026-08-14）：已实施“深石墨监控舱”，应用标题更新为“RtmpMonitor 监控台”。
> `StyleLoader` 在主题成功读取后统一应用 Fusion、深色 Palette 和限定作用域 QSS，读取失败不污染
> 原样式；主工具栏、空状态、视频边界、设备控制、日志和对话框使用同一语义色板。设备控制内部为
> `QDockWidget → QScrollArea → DeviceControlPanel`，1280×720 实际截图确认原生深色标题栏、空状态与
> Dock 统一且内容滚动可达。新增确定性 SVG/PNG/ICO 应用图标及 Windows DWM 外观辅助器；Windows
> Release EXE 图标可提取且无 Qt6Svg 运行时依赖。Windows Debug 全目标构建和 CTest 27/27、Release、
> ARM64 RASTER/GLES3 全目标构建及动态依赖复核通过，RASTER 未新增 GL 依赖。100%/125%/150% 完整
> 人工视觉矩阵仍待在对应系统缩放会话复验。

> 设备控制桌面化状态（2026-08-14）：原方向按钮已替换为 Windows 风格双模式控制台；新增
> `VirtualJoystickWidget`（20% 死区、四向量化、约 10° 迟滞、立即停车后 120 ms 回中）和
> `DeviceControlInputRouter`（显式解锁、WASD/方向键、Space、Esc、多键最后按下优先及文本/模态
> 作用域）。输入状态留在 Qt UI 线程，`DeviceControlController` 继续唯一持有车辆运动真值和停车
> 去重，MQTT/RTMP/schema/CLI 不变。Windows Debug 独立目录 217/217 构建步骤（全目标）和 CTest 27/27
>（最终 122.34 秒）、常用目录 CTest 27/27（120.20 秒）、Windows Release、ARM64 RASTER/GLES3
> 全目标交叉构建、ELF/动态依赖与 QEMU 纯逻辑门禁通过。旧 Debug 进程最初锁定常用输出；在其
> 自然退出后常用 Debug 主程序已成功更新，全程未强制终止。实车手感、Windows 100%/125%/150%
> 人工截图和固件失联停车仍待现场验证。

> MQTT 现场联调状态（2026-08-14）：用户已通过 EMQX 后台和 MQTTX 验证当前 Broker 的连接、
> 订阅、发布与消息观察，七类控制指令在消息层的返回符合既定 Topic/JSON 契约。该结果证明
> PC/Broker/MQTTX 通信链路正常，不作为小车实际动作或设备业务回执证明；安全台架实车验收仍待执行。

> 单车 MQTT 与保存推流基线（2026-08-13，已由上方 2026-08-15 双 Topic 状态扩展）：新增独立
> profiles/device_control 边界、保存列表 schema v1、全局 MQTT 设置、Paho MQTT C 1.3.16
> 异步发布/订阅客户端、方向控制 Dock、
> 有界 Topic 消息观察和 Fake Broker
> 集成测试。Windows Debug 全构建及 CTest 25/25、Windows Release 构建、ARM64 RASTER/GLES3
> 交叉构建和 ELF 依赖检查均通过。公网 Broker 仅确认 CONNECT/CONNACK 0；未在缺少现场安全
> 确认时发布远端命令或执行实车动作。本次无 PUBLISH 的远端 SUBACK 探测在执行前被平台审批
> 服务以 403 拒绝，未产生新的网络结果，已按约束暂停公网测试。Windows 无哈希测试包审计通过。
> 对应的实施级设计、界面操作、本地自动/手工测试、公网分级验证和排障入口为
> `docs/architecture/saved_stream_and_mqtt_device_control.md`；本次同 Topic 订阅/观察改动后新增模块
> 定向 CTest 5/5、Windows Debug 全量 CTest 25/25（最终 122.60 秒）复核通过。

> 架构开发门禁（2026-08-13）：已在当前 `CODEX_HOME` 安装并校验全局 `$architect-code-changes` Skill，仓库根 `AGENTS.md` 对代码、CMake、线程、持久化、schema、CLI 和测试架构变更强制使用 R0～R3 风险分级。项目专属边界仍由架构文档、实际 CMake target 和依赖检查脚本定义；本次只改变 AI 开发流程，未修改产品运行时行为。

> 架构解耦状态（2026-08-13）：按 media/render/ui/diagnostics/app 边界完成渐进拆分，新增依赖方向 CTest 门禁；Windows Debug 全目标构建及 CTest 21/21、Windows Release、ARM64 RASTER/GLES3 交叉构建和 ELF 依赖检查通过。CLI、schema v4、稳定 StreamId、容量 1 邮箱、重连和 CPU/OpenGL fallback 保持兼容。ARM 真机和 Windows 1/4/8 路 600 秒资格仍不在本次结论内。
> 解耦文档状态（2026-08-13）：`docs/architecture/progressive_decoupling_architecture.md` 已补齐解耦前耦合、优先级依据、P0～P3 迁移过程、设计原因、收益、兼容边界、CMake/组件/线程架构图、遗留耦合和评审清单；文档明确区分运行时数据流、编译依赖与目录职责。

> Windows 摄像头资格状态（2026-08-12）：schema v4、双行机器标记、统一显示帧率策略、摄像头源、视频分析器和总控脚本已实现；解耦后的 Windows Debug 基线为 CTest 21/21。第二轮单路 120 秒快速运行通过：采集/发布/解码/显示 30.000/29.967/30.038/29.963 FPS，零序号缺口，源延迟 P95 104 ms。Windows `auto` 产品目标现为 30 FPS；真实 1/4/8 路 720p30 的 600 秒矩阵尚未执行，见 ISSUE-012。

> 朋友测试包状态（2026-08-12）：从当前 Windows Release 工作树生成独立 Development Preview 便携 ZIP，打包审计通过；全新目录解压后的 31 个文件、SHA-256、版本 CLI 和 5 秒 GUI 启动均通过。该临时包位于被忽略的 `out/friend-package-20260812/`，不包含 SRS 和开发用摄像头资格工具，也不构成正式发布候选。

> 最近审查日期：2026-08-15
>
> 发布分支：`v0.1.0-alpha.1`
>
> Phase 0 冻结基线：`v0.1.0-alpha.1-rc1`（`80ce526`）
>
> 说明：本快照依据源代码、CMake、Git 状态和本轮实际构建/CTest 结果整理。

> 发布状态：`RtmpMonitor v0.1.0-alpha.1`；Windows 为 **Development Preview**，
> ARM Linux 为 **Engineering Preview**。Windows+WSL2 最小 SRS 链路已独立复验；
> 4/16 路与 600 秒结果属于历史证据；ARM 真机、真实摄像头和网页现场结果仍为
> `[需要验证]`。

> Week 6 状态：四宫格首帧黑屏已修复；主网格已改为居中的标准 16:9 监控网格，默认与全屏均使用 `Contain`，标准 16:9 流因此可以完整、铺满且不变形；异常比例完整留边，每格仍可手动选择 `Cover`。四组 600 秒 CPU/OpenGL 与 Quality 的历史正式门禁全部通过，CLI 默认 Renderer 为 `auto`，显式 CPU 回滚保留。

## 项目目标

RtmpMonitor 是一个使用同一套 C++17、Qt 6 Widgets 和 FFmpeg 代码，在 Windows x86_64 与 Linux ARM64 上接收、解码并多宫格显示 H.264/RTMP 视频的桌面/嵌入式客户端。

RTMP Server 采用独立 SRS 服务，不在 Qt 进程内实现 RTMP 协议。当前仓库已完成
Windows+WSL2 侧的最小配置、生命周期脚本、URL/配置生成和只读健康监控；ARM
真机、真实摄像头和最终分发仍按 ISSUE-008 保持 `[需要验证]`。

## 当前技术栈

- C++17、CMake 3.21+。
- Qt 6 Widgets；生产级单画布 OpenGL YUV renderer，并保留 CPU/QPainter 回退。
- FFmpeg 8.1.2 LGPL：avformat、avcodec、avutil、swscale。
- Windows x86_64：MSVC、Qt 6.6.1、Ninja。
- Linux ARM64：WSL2 Ubuntu 22.04、AArch64 GCC 11、Qt 6.2.4 目标库和 ARM64 sysroot。
- 外部 RTMP 基础设施：SRS 或 nginx-rtmp，默认 RTMP 端口 1935。

## 目标平台与本机环境

| 项目 | 已验证事实 |
| --- | --- |
| Windows | Windows 11；仓库个人绝对路径不写入版本控制。 |
| WSL2 | `Ubuntu-22.04-New`，版本 2；发行版存储使用本机配置，个人位置不入库。 |
| WSL 网络 | mirrored networking、Windows 防火墙开启。 |
| WSL swap | 使用本机独立配置，个人位置不入库。 |
| Codex Home | 使用本机 `CODEX_HOME`；用户目录下 `.codex` 是指向该目录的 Junction，个人绝对路径不入库。 |
| ARM64 sysroot | 位于 WSL VHDX 内；本任务不移动或修改其现有工具链边界。 |
| OpenViking Server | 0.4.11 已从预编译 wheel 安装到 `/opt/openviking/venv-0.4.11`；MCP 固定为 1.29.0；VLM 为 `openai-codex/gpt-5.6-luna`（OAuth，Responses，`reasoning.effort=none`）；配置和数据分别位于 `/etc/openviking`、`/var/lib/openviking`。`openviking.service.d/proxy.conf` 只为该服务设置本机 `127.0.0.1:7890` 代理和 localhost 例外，不修改 Clash 配置。 |
| OpenViking Windows 客户端 | `ovcli.conf`、插件状态和日志位于本机专用配置目录，该个人绝对路径不入库；Marketplace 插件 0.7.4 已安装并启用一次。`ovcli.conf` 不再固定 `actor_peer_id`，由插件按工作目录派生 peer；当前 CLI 的通用 `hooks` 功能为稳定且默认启用，已移除失效的旧配置项 `plugin_hooks`。 |
| Kimi Code MCP 客户端 | 项目级 `.kimi-code/mcp.json`（规则 `/.kimi-code/mcp.json` 已在 `.gitignore`）连接 `http://127.0.0.1:1933/mcp`，peer 固定 `E--rtmpProject`。2026-08-08 起 `OPENVIKING_API_KEY` 持久化为 Windows 用户级环境变量（HKCU，值与受限 `ovcli.conf` 一致），根治了此前“绕开安全启动器直接启动即 401”的问题；任意方式启动的新会话均可连接。仅主动工具调用，无自动 Hook 链路。 |
| WSL 登录保活 | `OpenViking-rtmpProject-WSL-KeepAlive` 登录后延迟 30 秒、禁止重复实例并隐藏维持 WSL 生命周期；无交互式 WSL 终端的五分钟空闲、四个 Windows HTTP 入口及任务停止/重启恢复已通过。 |

## 当前主要模块

- 应用组合：`ApplicationOptions`、`ApplicationBootstrap`、`StreamConnectionController`、`ConnectionBindingRegistry`、`ConnectionEventReporter`、`StyleLoader`。
- 播放与并发：`FFmpegPlayer` façade、`FfmpegInputSession`、私有 `StreamDecodeSession`、`MultiStreamPlaybackManager`、`DecodeWorkerPool`。
- 诊断：`RuntimeMetricsReporter` 组合媒体/渲染指标并原子输出 schema v4。
- UI：`MainWindow`、`DeviceControlPanel`、`VirtualJoystickWidget`、`DeviceControlInputRouter`、`VideoGridWidget`、`MonitoringGridLayout`、`GridTransitionAnimator`、`VideoGridSceneBuilder`、`VideoWidget`、`FullscreenVideoWindow`、`FullscreenChromeController`、`FullscreenScreenshotService`、`VideoCanvasHost`、`CpuVideoCanvas`、`VideoOpenGLCanvas`（GL 可选编译）、`LogPanel`。
- 视频帧与渲染：`VideoFrame`、`LatestFrameMailbox`、`VideoRenderController`、`OpenGLGridRenderer`（仅 `RTMP_MONITOR_HAS_OPENGL=1` 时编译）、`EmbeddedGlCapabilities`；旧 `VideoRenderWidget` 仅保留为历史 RGB 原型冒烟。
- Linux 平台：`LinuxApplicationBootstrap`、`LinuxRenderingPolicy`、`LinuxRendererFactory`（`src/platform/linux/`，仅 Linux 编译）；EGLFS 下全屏复用主画布。
- 日志与用户消息：`LogManager`、`SensitiveDataSanitizer`、`UserMessageService`。
- 构建与验证：Windows/ARM64 Preset、AArch64 toolchain、Linux `RASTER/GLES3/AUTO` 渲染模式、CTest、SRS 验证入口和 `scripts/qualify_embedded_device.sh` 板级资格脚本。

## 2026-08-13 单车 MQTT 与保存推流验收

- profiles 与 device_control 保持独立边界；依赖门禁通过。PC 在 CONNACK 后订阅同一个发布 Topic，
  只有 SUBACK 成功后进入 Connected。接收 payload 最多保留 4096 字节，网络 inbox 最多 64 条，
  UI 最多显示 20 条且来源统一为未知；收到消息只观察，不执行本地动作。
- Fake Broker 覆盖订阅成功/拒绝/5 秒超时、QoS 0 一发多收、外部发布观察、重连重订阅、截断、
  有界队列、初始连接失败后的重试和离线发布失败；协议测试固定七类动作。
- Windows Debug 最终完整 CTest 25/25（122.60 秒）通过；session generation 已使用原子状态隔离
  Paho 工作线程与 Qt owner 线程，定向 MQTT/面板测试 2/2（15.21 秒）通过。Windows Release 增量全构建、ARM64 RASTER/GLES3
  增量交叉构建和 ELF NEEDED 复核也已通过；RASTER 仍无 Qt OpenGL/EGL/GLES 依赖，两个模式均
  依赖预期的 `libpaho-mqtt3a.so.1`。代表性的 Release
  测试可执行文件在收敛 PATH 后直接运行正常，但本机 CTest 子进程仍受加载环境影响，部分在启动前
  返回 `0xc0000139`，不将其记为 Release CTest 通过。
- ARM64 RASTER/GLES3 均完成增量交叉构建；两者需要 `libpaho-mqtt3a.so.1`。RASTER 仍无
  Qt OpenGL/EGL/GLES 动态依赖，GLES3 保留预期 Qt OpenGL 依赖。
- Windows 测试包包含 `paho-mqtt3a.dll` 与 EPL-2.0 许可证，未携带 OpenSSL。按当前交付要求，
  打包脚本不计算或比对哈希，也不生成 SHA-256 文件；包内 33 个文件可完整解压并运行版本命令。

## 当前已验证进度

- 2026-08-13 渐进式解耦计划已落地：媒体管理器不再反向获取渲染指标或写文件；
  FFmpeg 单次输入会话与解码 session 分离；连接 Registry/Reporter、全屏 Chrome/截图、
  网格几何/动画/场景构建以及 CLI/Bootstrap 均拆为独立职责。新增 schema v4 空样本
  默认值测试和 `media !-> render/ui`、`render !-> ui` CTest 门禁。Windows Debug
  CTest 21/21（最终复跑 94.34 秒）通过，Windows Release 全目标构建通过；ARM64 RASTER 与
  GLES3 均完成交叉构建，前者无 Qt OpenGL/EGL/GLES 动态依赖，后者保留预期 Qt
  OpenGL 依赖。正式性能资格和 ARM 真机认证未执行，既有 ISSUE 状态不据此关闭。

- 2026-08-12 Phase 4 源码交接策略修订为 GitHub 分支和精确 commit：根 README
  现在提供 clone 后的 Windows/ARM64 环境、构建、CTest、运行和脚本索引；旧长版
  README 已整理归档。源码 ZIP 链路与维护者本机专用的性能/测试编排脚本已退役，
  C++ `tests/` 和 19 个 CTest 目标保留；SRS WSL 发行版改由参数或环境变量注入。
  仓库继续不提交第三方 `.lib/.dll/.so`。2026-08-11 的 GitHub clean-room Windows
  Release 143/143、CTest 19/19，以及 Linux RASTER 124/124、GLES3 138/138、
  FFmpeg/ELF/依赖/QEMU 33 项证据仍有效。本轮精简后的 F 盘 clean-room Windows
  Debug 又完成 143/143 构建和 CTest 19/19（103.10 秒），个人 Preset 拒绝覆盖
  负向测试通过；G 盘 WSL 独立 `/tmp` 又完成 ARM64 RASTER 124/124、GLES3
  138/138 与 QEMU 33 项断言。接收方独立 Windows/SRS/F5 复现仍 `[需要验证]`，
  Phase 4 总体未关闭。

- 2026-08-11 Phase 3 的 Codex 实现与静态门禁已完成：Windows 安装规则改为包根
  EXE，新增 `scripts/package_windows.ps1`、保守的 Development Preview 私有授权
  声明、`THIRD_PARTY_NOTICES` 和包内说明。脚本会拒绝覆盖非空目录，固定执行
  install/windeployqt/FFmpeg/授权与版本清单/审计/ZIP/SHA-256，并核对 MSVC Qt
  6.6.1、vcpkg `x64-windows` FFmpeg 8.1.2、FFmpeg GPL/nonfree 标志、PE 依赖和
  Microsoft VC++ Runtime 签名/最低版本。最终候选包 31 个文件、76,605,714 bytes，
  禁止文件、凭据、鉴权 RTMP URL、个人绝对路径和未解析 PE 依赖均为 0；解压文件数
  一致，包内版本 CLI 退出码 0。SHA-256 为
  `64e4879c9e527590a107087b3f654681c5f669fe7fe5e2de6cd2b7fd1b0464a9`。
  独立目录重复打包除 UTC 版本时间外逐文件一致；Release CTest 在显式 MSVC Qt/
  vcpkg 环境下 19/19（78.96 秒）通过。项目负责人授权复核及干净 Windows/VM 的
  VC++ 前置安装、双击、真实流、截图、退出和删除目录仍 `[需要验证]`，故 Phase 3
  总体仍未完成；候选制品只在被忽略的 `out/phase3/`。

- 2026-08-11 Phase 2 的 Agent 主导 Windows 门禁已完成，人工桌面门禁尚未完成：
  使用 VS CMake 3.29.5、Ninja、MSVC 19.41、Qt 6.6.1 和 vcpkg FFmpeg 8.1.2
  全新生成 Release，143/143 构建、完整 CTest 19/19（77.30 秒）通过；SRS
  6.0.184 与 1080p30 H.264 High 4.1/约 6 Mbps 流通过 Windows ffprobe；真实流
  FFmpeg 生命周期以及 CPU/OpenGL UI 正常关闭目标通过，verbose 结果为 4 passed、
  0 failed、0 skipped。Qt-Debug 同样 fresh configure、143/143 构建，空闲重跑
  完整 CTest 19/19（99.55 秒）通过。门禁期间发现 Windows GUI 子系统的
  `--version` 会显示模态框并残留进程，现改为 stdout 输出后立即退出，并新增 5 秒
  TIMEOUT 回归作为第 19 项；Release/Debug 定向回归均通过且无残留进程。修复前首轮
  Debug 在持续软件编码和刚完成全量编译时出现一次截图异步保存 2 秒等待超时，
  单项重跑通过；该结果已被修复后的全新 19/19 覆盖。原始证据只保存在被忽略的
  `out/phase2/`。Visual
  Studio Release/Debug 的中文、画面、全屏、截图、恢复及 `auto`/`cpu` 各 30 次
  正常关闭仍由用户人工验证，故 Phase 2 和 ISSUE-009/010 继续保持未完成。

- 2026-08-10 Phase 1 文档交接已完成：SRS 教程的 Bash/PowerShell 命令已分离，
  增加真实 1080p H.264 `-c:v copy`、libx264 回退和扩展 `ffprobe` 检查；新增
  `embedded_developer_handoff.md`，统一收集板卡、系统、ABI、Qt/QPA/EGL/GLES、
  显示输入和摄像头参数。未取得 ARM 真机、真实摄像头或网页现场证据的字段保持
  `[需要验证]`。本 Phase 仅修改文档，未重跑 C++ 构建或 CTest。
- 2026-08-10 Phase 0 审计已完成：169 个受跟踪文件、0 个未跟踪文件；发布排除项
  均保持忽略，凭据扫描命中只包含合成脱敏测试夹具/安全说明，跨平台指南中的个人
  盘符已改为占位符和环境变量。历史合并提交不重写，后续按 Phase 独立提交；
  `v0.1.0-alpha.1-rc1` 仅冻结 Phase 0 基线，不代表后续发布门禁完成。
- 2026-08-10 发布清单 Phase 0 的版本号与功能冻结项已完成：CMake 数字核心保持
  `0.1.0`，预发布版本集中定义为 `0.1.0-alpha.1`，程序 `--version` 使用同一生成
  配置；Windows Debug 主程序重建成功，完整 CTest 18/18 通过（91.77 秒）。本轮未
  设置 `RTMP_MONITOR_TEST_URL`，不构成新的真实流复验。后续仅接受发布清单内的
  必需改动；Phase 1 已单独完成，Phase 2～5 未因此自动完成。
- 2026-08-10 已创建发布候选分支 `v0.1.0-alpha.1`；当前 Windows Debug 工作树
  增量构建无待编译目标，完整 CTest 18/18 通过（92.76 秒）。本轮未设置
  `RTMP_MONITOR_TEST_URL`，因此真实流 UI 用例为跳过路径，真实流结论仍使用下方
  已记录的独立 Verifier/压力测试证据，不把本轮 18/18 冒充新的真实流验收。
- 2026-08-08 Linux 双路径渲染架构（`embedded_device_rendering_strategy.md` §16～§22）已由 Kimi 落地：`CpuVideoCanvas`/`VideoOpenGLCanvas` 拆为独立编译单元，`RtmpMonitorBuildConfig.h` 暴露 `RTMP_MONITOR_HAS_OPENGL`；新增 `src/platform/linux/`（bootstrap/policy/factory）与 `EmbeddedGlCapabilities` 纯判定；EGLFS 自动改为画布内单路 Snapshot 全屏；GLES3 P1 小优化（静态 sampler、批次 unpack、每 RenderItem 一次错误检查、纹理保留策略）完成。Windows Debug CTest 14/14 通过（新增 capabilities/policy 两个纯逻辑目标与 7 个动态网格用例）。
- 2026-08-08 WSL2 ARM64 双构建完成：RASTER 产物为 ELF64/AArch64 且 NEEDED 无 Qt6OpenGL/Qt6OpenGLWidgets/libEGL/libGLES；GLES3 产物含 Qt6 OpenGL/OpenGLWidgets 依赖且全部来自 sysroot。QEMU 用户态下纯逻辑测试：capabilities 10/10、policy 9/9、render core 9/9、user message 5/5 通过；logging 2 个计时敏感用例在 QEMU 下失败（QEMU 时序问题，Windows 端通过，与本次改动无关）。板级资格脚本 `scripts/qualify_embedded_device.sh` 已交付但未经真实板运行。
- Week 1～3：外部 RTMP 链路、动态 1～16 路 UI、单路 FFmpeg 拉流与安全退出已完成。
- Week 4：源码已支持 0～16 路动态连接、阻塞网络与共享解码池解耦、指标和自动化验收脚本。
- Week 5：设备状态、日志分层、脱敏和可配置重连相关模块已存在。
- Week 6 后续产品化：解码输出已切换为不可变 YUV `VideoFrame` 和容量 1 邮箱；主网格使用单 OpenGL 画布，全屏使用不共享 GLuint 的临时画布，OpenGL 失败自动回退 CPU。
- Week 6 指标已升级为 schema v3；生产 framebuffer 8 个质量用例、Renderer A/B 总控、独立测试流前缀、每秒采样、脱敏汇总和三篇教学/实战文档已完成。四组 600 秒正式门禁全部通过：OpenGL 16 路平均 CPU 相对降低 69.08%、显示 14.91 FPS，双屏最差流 P95 196 ms、最大 317 ms。
- 2026-08-04 已修复四路 RTMP 连接后主网格黑屏、进入一次全屏才显示的问题：Qt 6 拒绝 lambda 形式的 `Qt::UniqueConnection`，现改为成员槽；动态网格 29/29、完整 CTest 12/12 与四路无双击实机验证通过。
- 2026-08-08 此前将主网格默认设为 `Cover` 只是对超宽格子的临时补救，现已被 16:9 网格几何修复取代：`MonitoringGridGeometry` 为当前行列数选择整数像素中面积最大的近似 16:9 视频区、居中的动态 layout margins 和一致单格尺寸。普通窗口使用 4px 外边距/4px 格间距，F11 监控墙使用 0px/0px。新格默认 `Contain`，标准 16:9 流完整铺满；4:3、竖屏等异常比例完整留边；每格仍可手动选择 `Cover`，单路全屏固定 `Contain`，不提供 Stretch。
- `VideoWidget` 的设备名已移入 `videoSurface` 左上角半透明覆盖标签，不再占用独立标题行；`deviceName_`、tooltip 和 Snapshot 保留完整名称，省略标签不拦截鼠标。视频区下限为 144×81；窗口缩放、字体/样式变化、日志 Dock 显隐和动态布局均会重新计算。底部事件日志默认隐藏，但仍持续收集并可由“视图”菜单显示。
- `MainWindow` 提供“视图 → 监控墙模式”、F11 和 Esc 往返：进入时保存并隐藏窗口栏、菜单、工具栏、状态栏和日志 Dock，退出时原样恢复。监控墙临时强制各格有效模式为 `Contain`，退出后恢复原先逐格选择；从监控墙进入单路全屏再退出不会丢失监控墙状态。
- 2026-08-08 撤回一组基于非 VS 启动窗口误判而产生的临时 Canvas/overlay 层级修改后，使用 VS 的 MSVC 工具链完成 Windows Debug 构建，完整 CTest 12/12 全部通过（74.57 秒）。最终回归包含 64 组 1～16 路/四种窗口尺寸的纯几何用例、真实 16 格 viewport/覆盖标题、4:3/竖屏完整显示、日志 Dock、F11/Esc，以及 CPU/OpenGL 对 16:9/4:3/竖屏四边标记的 framebuffer 验证；原有 8 个 YUV/颜色/stride 质量用例保留。删除临时方向诊断日志后又在 `vcvars64.bat` 环境重新编译并执行生产 OpenGL framebuffer 目标，1/1 通过。
- 2026-08-08 第一轮居中 16:9 网格的 120 秒快速对照全部门禁通过：CPU 3.425%→0.203%，display FPS 11.047→14.914，frame age P95 46→34 ms；但它早于最终标题覆盖和监控墙实现。
- 最终监控墙版本的 120 秒快速对照实际命中 RTX 3060 Desktop GL 3.3、无 fallback：CPU 2.362%→0.197%（降低 91.66%），display FPS 8.496→14.914，internal latency P95 43→40 ms，最大 UI gap 149→174 ms，纹理稳定为 22,118,400 bytes；latest frame age P95 47→52 ms，超过 51.7 ms 门槛 0.3 ms，因此总控如实判定失败。两次短测均不替代四组 600 秒正式门禁。
- 视觉证据边界：用户确认 Visual Studio 生成并启动的程序显示正常。Codex 自动化账户启动的窗口与用户 VS 运行环境不同，不作为可比产品证据，也不再据此修改生产渲染代码。当前可重复的定量证据来自纯几何、CPU/OpenGL framebuffer 回读和用户对 VS 产物的人工确认；尚未固化 VS 窗口外围留白的像素测量截图。按状态文件精确停止后，1935 监听器及匹配客户端/FFmpeg/nginx 进程均为 0。
- 2026-08-09 Windows 单路全屏后续修复已完成代码与自动化门禁：无帧控制栏固定可见；有帧后经 1200ms 以 180ms `OutCubic` 收起，由底部 96px 热区滑入，离开防抖 250ms；清帧立即恢复。截图按钮与 `Ctrl+Shift+S` 抓取全屏画布 framebuffer、立即显示缩略图，并在线程池用 `QSaveFile` 异步原子保存 PNG。退出以最后 framebuffer 的 raster 冻结层遮挡切换，主网格 `surfacePresented()` 后才揭开，750ms 超时兜底。Visual Studio MSVC 环境 `Qt-Debug --fresh` 配置成功、136/136 构建成功、完整 CTest 17/17（92.74 秒）通过。真实 SRS 流下的工具栏、截图方向/颜色和 NVIDIA Overlay 录屏仍 `[需要验证]`，见 ISSUE-009。
- 2026-08-09 用户第二轮录像发现无帧顶部旋转控制栏、有帧截图后底栏不收起以及光标难以恢复。控制栏现改为底部 96px 热区内的受裁剪子控件，不再把表面动画到顶层窗口边界外；重复鼠标移动不再重启动画。全屏临时画布的鼠标事件穿透到统一处理器，底栏 250ms 收起与光标 2000ms 空闲隐藏使用独立计时，任何全屏鼠标活动立即恢复光标。新建独立 Windows Debug 目录完成 136/136 构建和 CTest 17/17（98.01 秒）；用户退出旧 F5 实例后，Visual Studio 当前构建目录的 `rtmp_monitor.exe` 已重新链接 1/1 成功。重新 F5 后的录像复验仍 `[需要验证]`。
- 2026-08-09 Windows Debug 退出堆损坏已定位到构建依赖追踪，而非 FFmpeg/OpenGL 运行时：Application Verifier Full Heaps 捕获 `FullscreenVideoWindow` 分配块末尾首次越界，Ninja 同期记录旧 `MainWindow.cpp.obj` 为 `#deps 0`。根因是 CMake 3.29 按 GBK 错误解码 `cl.exe` 的 UTF-8 中文 `/showIncludes` 前缀，头文件变化后调用方未重编译，产生类布局 ABI 不一致。CMake 现以原始字节探测前缀且探测失败即拒绝配置；全新 `Qt-Debug` 后 `MainWindow.cpp.obj` 恢复 347 条依赖并包含 `FullscreenVideoWindow.h`。143/143 构建、CTest 18/18、Verifier 真实流 UI 复测、CPU/OpenGL UI 关闭 30/30、单路真实流 30/30、16 路真实流 10/10，以及独立 ASan RelWithDebInfo 54/54 构建与真实流 UI 退出均通过；Verifier 已关闭。用户 Visual Studio F5 两种 renderer 各 30 次人工退出仍 `[需要验证]`，见 ISSUE-010。
- 2026-08-03 Windows Debug 全工程构建通过，完整 CTest 12/12 通过（65.87 秒），包含生产 YUV Shader framebuffer 像素验证和渲染核心并发/所有权/最终渲染延迟测试。
- 2026-08-03 Linux ARM64 Debug 全量交叉构建 94/94 目标步骤通过，最终改动又通过 45 步增量构建；主程序、ES3 EGL 冒烟和渲染核心测试均为 ELF64/AArch64，主程序依赖目标 Qt6 OpenGL/OpenGLWidgets 与 FFmpeg `.so`。
- 历史 GLES2 冒烟基线已提升为 GLES3；ARM64 主程序、测试、EGL/ES3 与 Qt OpenGL 目标均生成 AArch64 ELF。
- 上述 ARM64 结论仅证明交叉编译和链接，不证明真实盒子上的 QPA、GPU 或视频播放。

## 当前阻塞与未完成项

- Linux ARM64 真实硬件盒子的 QPA、GPU、FFmpeg 播放、输入和长期稳定性尚未验收。
- Windows 16 路正式性能资格测试已完成；更换 GPU/驱动、分辨率或部署环境后需重新执行。
- 2026-08-04 的四组 600 秒正式结果早于当前紧凑网格和 F11 监控墙；当前布局只完成两次 120 秒预录 Video 快速对照，最终版本还因 latest frame age P95 超出相对门槛 0.3 ms 而未全过。若要把当前布局作为新的完整发布负载认证，需要重新执行 Video、LiveLatency 与 Quality 的正式套件。
- RTMP Server 接入已于 2026-08-09 完成独立修复复验：Visual Studio `Qt-Debug` 现通过公共 `Windows-MSVC-vcpkg` 与本机 `VCPKG_ROOT` 使用正确 FFmpeg；无 toolchain 时 CMake 会给出可操作错误。全新配置、136/136 构建、CTest 17/17（85.29 秒）通过。SRS 6.0.184 的 1935/回环 1985、WSL/Windows 双侧 H.264 拉流、停推/同 URL 恢复、SIGQUIT 停止和未知 1935 占用拒绝已重跑通过；停止后端口与状态文件均清理。Kimi 的 4/16 路与 600 秒结果保留为历史证据，本次未重跑；Visual Studio F5 中文界面需用户在本机窗口最终确认。ARM 实机、真实摄像头、Docker 仍 `[需要验证]`，见 ISSUE-008。
- Windows 单路全屏的历史控制栏、完全黑屏、成功推流后工具栏不可唤出和退出白闪均已完成代码修复与自动化回归，但 Visual Studio F5、默认 OpenGL、NVIDIA Overlay 开启的真实 SRS 流录像尚未由用户确认；首帧、截图方向/颜色、停推与恢复推流保持 `[需要验证]`，见 ISSUE-009。
- Windows Debug 退出堆损坏的首次非法写入已由 Application Verifier 捕获并修复，自动化与真实流压力回归均通过；Visual Studio F5 下 `auto`/`cpu` 各 30 次人工关闭仍 `[需要验证]`，见 ISSUE-010。
- OpenViking 0.4.11 已启用为 systemd 服务并绑定 `127.0.0.1:1933`；MCP 已通过 wheel-only 固定为 1.29.0。doctor、`/health`、`/ready`、Windows 回环访问、重启恢复、user/root key 边界和临时 Session CRUD/清理均已验证。
- OpenViking Marketplace 与 `openviking-memory` 0.7.4 已各安装一次；插件声明四个官方 Hook 和一个内置 MCP。真实 MCP 握手及 18 个工具枚举已通过。2026-07-30 已在共享 `CODEX_HOME` 的 Codex CLI 中完成持久审批，四个 Hook 均显示 `Installed=1`、`Active=1`。真实 CLI `/compact` 已触发 `PreCompact(trigger=manual)`、真实 transcript 捕获、工作区 peer 和 Session commit accepted 均有日志证据；当时后台抽取任务因网络超时失败。
- 2026-08-03 当前 Desktop 已真实暴露并成功调用 `search_experience`、`read_experience`；常规 `search`、`recall`、`read` 也已从项目 peer 命中历史摘要。精确 MCP 工具与直接召回已通过，但仍需在自然新建的 Desktop 任务中证明 `SessionStart`/`UserPromptSubmit` 自动注入和跨任务唯一标记完全一致，故 ISSUE-003 仍未完全关闭。
- 2026-08-03 已从 9 个顶层 rtmpProject rollout 中排除当前任务并合并重复/续接记录，形成 6 个 `rtmp-history-<date>-<source-id>` 脱敏 Session；27 个子代理会话、原始 transcript 和工具输出未导入。六个 commit 均为 `completed`，每份都有 `archive_001`、`memory_diff.json` 和 `.done`，只抽取到 `E--rtmpProject` 的 `events/entities/preferences`。
- 固定 `actor_peer_id=E--rtmpProject` 曾导致其他工作区内容误写入项目 peer；完整 peer 已备份后，精确删除 7 个相关事件、2 个偏好和 1 个完全被其覆盖的软件实体。`ovcli.conf` 现改为工作区派生 peer：本机仓库仍派生 `E--rtmpProject`，另一工作区会派生独立 peer。全局其他项目记忆和现有模型调研 Resource 未删除。
- 2026-07-31 已确认 OpenViking 的 `openai-codex/gpt-5.4` 超时底层为 `httpcore.ConnectTimeout`，原因是 systemd 服务未加载代理环境而直接连接上游。现已增加仅作用于 `openviking.service` 的代理 drop-in 并重启；精确 endpoint 经 `127.0.0.1:7890` 连续 20 次均在 0.83 秒内得到预期 HTTP 405。真实 9,373 字节（5,973 字符）Resource 在 46 秒内完成摘要、overview、向量和关联 Session，journal 无新增 timeout；255 字符 abstract 和 1,920 字符 overview 已复核存在。未修改 Clash for Windows 的节点、规则、订阅或端口。
- 2026-08-03 最新只读复测显示现有代理链路再次间歇不稳：10 次目标 endpoint 探测仅 3 次得到预期 405，7 次为 TLS EOF。第一轮历史回填因此失败并完整回滚，第二轮在 Session 级精确回滚/重试保护下六份全部完成。本轮未修改 Clash、代理规则、systemd drop-in、端口或防火墙；残余风险见 ISSUE-007。
- 2026-08-03 已将 OpenViking VLM 从 `gpt-5.4` 切换为 `gpt-5.6-luna`。0.4.11 的 Codex Responses 适配器已做最小本机兼容修正：只对 `gpt-5.6-*` 传递 `reasoning.effort=none`；`ov.conf` 不写入其 schema 不接受的 `vlm.reasoning_effort`。`doctor` 显示 `openai-codex/gpt-5.6-luna` PASS；真实 `store=false` 探测返回 Luna，使用 33 输入、7 输出、共 40 Token。专用 Session commit 在约 21 秒内 `completed`，总计 14,480 Token、reasoning Token 为 0，`memory_diff` 无增删改；测试 Session、完成任务索引均已删除，唯一标记零命中且清理错误为 0。该验收只证明 Server VLM 与抽取链路，不关闭 ISSUE-003 的 Desktop 跨任务召回缺口。
- 2026-08-03 一次配置语法检查错误地输出完整 `ov.conf`，导致当时的 Server root key 被工具输出回显。该 key 已立即随机轮换；当前配置和回滚备份已同步新值并保持 `600`、`fklightdog:fklightdog`。Windows user/admin key 未改变，旧 root key 不得再次使用或记录。
- 一次验收工具输出意外回显了 Windows admin user key；该 key 已立即轮换并验证旧值返回 401、新值返回 200，`ovcli.conf` ACL 仍仅允许 ASUS 与 SYSTEM。Studio 和已运行的客户端进程必须改用新 key。
- Windows 登录保活已在没有交互式 WSL 终端的条件下连续运行超过五分钟；任务保持 Running，`/health`、`/ready`、`/studio` 持续为 200，`/docs` 最终为 200。停止并重启任务后保活链和 health 均恢复；ISSUE-004 已解决。
- 已验证 OpenViking 0.4.11 在 1933 同源提供 Web Studio `/studio` 和 OpenAPI `/docs`；mirrored networking 工作时不需要 `netsh portproxy` 或局域网绑定。
- 本轮失败验收产生的专用 OpenViking Session、Codex 测试任务、插件状态和 transcript 已删除；长期记忆精确 grep 未发现本轮唯一标记。

## 下一步优先任务

1. 维护者按发布清单完成 Phase 2 的 Visual Studio F5/视觉/退出压力人工门禁，并在
   干净 Windows 11 实机或 VM 完成 Phase 3 的 VC++ 前置、双击、真实流、截图、退出
   和目录删除验收；项目负责人单独复核 `LICENSE`，不得用当前开发机代替干净机。
   另由独立开发者从 Phase 4 GitHub 分支 clean clone 后完成 Windows 构建、CTest、SRS 和 F5 复现。
2. 自然重启 Desktop，让长期运行的插件/MCP 子进程读取移除固定 `actor_peer_id` 后的配置；分别在 rtmpProject 和另一工作区核对派生 peer。
3. 在新的自然 Desktop 任务中完成唯一标记自动注入验收；只有后台任务 `completed`、`SessionStart`/`UserPromptSubmit` 注入日志和完全一致召回同时成立才关闭 ISSUE-003，随后清理专用标记。
4. 在真实 Linux ARM64 盒子完成发布级验收：使用 `scripts/qualify_embedded_device.sh` 按用户指定的路数阶梯/码流/门槛执行，生成设备档案与 `recommendedMaxStreams`；EGLFS 全屏、linuxfb、温度和长稳只能在真机验证。
5. 按 `cross_platform_build.md` §9 在真实 ARM 目标板完成 SRS 本机构建、systemd 与 LAN 推拉流验收；真实摄像头到货后按方案 Phase 3 复测编码兼容性（ISSUE-008）。

## 关键文档

- `docs/guides/build-and-testing/embedded_developer_handoff.md`：嵌入式接收方环境、
  ABI、显示与摄像头参数填写模板和真机资格边界。

- [跨会话记忆说明](README.md)
- [重要设计决策](decisions.md)
- [已知问题](known_issues.md)
- [项目路线](../roadmap/project_plan.md)
- 维护者本地会话交接（`docs/project_handoff.md`，不进入源码交接包）
- [跨平台构建](../guides/build-and-testing/cross_platform_build.md)
- [RTMP 链路验证](../guides/build-and-testing/rtmp_chain_verification.md)
- [SRS Server 接入实施方案](../architecture/srs_server_integration_plan.md)
- [OpenViking 使用与测试](../guides/development/openviking_usage_and_testing.md)
- [Week 6 OpenGL 验证](../weeks/week6/week6_opengl_environment_and_validation.md)
