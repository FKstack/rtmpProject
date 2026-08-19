# Week 7：SRS Server 接入实施与逐 Phase 验收记录

> 发布标识：`RtmpMonitor v0.1.0-alpha.1`；Windows 为 **Development Preview**，
> ARM Linux 为 **Engineering Preview**。Windows+WSL2 最小 SRS 链路已独立复验；
> 本文的 4/16 路与 600 秒结果属于历史证据；ARM 真机、真实摄像头和网页现场结果
> 仍为 `[需要验证]`。

> 对应方案：`docs/versions/rtmp-v1/architecture/srs_server_integration_plan.md`（Phase 1～7）。
>
> 实施日期：2026-08-08 ～ 2026-08-09。执行者：Kimi。
>
> 平台：Windows 11 + WSL2 `Ubuntu-22.04-New`（mirrored networking）、
> MSVC/Qt 6.6.1 Windows Debug、AArch64 GCC 11 交叉构建环境。
>
> 状态结论：**Phase 1～7 的 Windows+WSL2 侧全部完成**；ARM 实机、真实
> 摄像头、Docker 备选路径保持 `[需要验证]`（见第 8 节）。不得表述为
> "Server 产品化完成"。

> 独立复验补记（2026-08-09，Codex）：Kimi 的原始状态不能直接作为最终验收。
> 复验首先发现 Visual Studio `Qt-Debug` 未传入 vcpkg toolchain、生命周期脚本
> 使用快速退出信号且 PID 升级信号缺少二次身份校验，因此初验不通过。修复后使用
> `Qt-Debug --fresh` 完成 136/136 构建、CTest 17/17（85.29 秒）；重新运行 SRS
> 6.0.184、WSL/Windows 双侧 ffprobe、停推/同 URL 恢复、SIGQUIT 停止和未知
> Python 1935 占用拒绝，全部通过。4/16 路和 600 秒结果仍属于 Kimi 的历史证据，
> 本次未重跑；Visual Studio F5 的最终中文界面由用户在本机窗口确认。

## 1. 固定基线（已核对）

```text
Git tag:       v6.0-r0
SRS version:   6.0.184
Release commit:9f8670b2a832aea04abe644af47261838838c49a   (实测一致)
Install:       WSL $HOME/opt/srs-6.0.184（源码构建）
Config:        deploy/srs/conf/srs-minimal.conf（入库）
```

最小配置字段已逐一对照固定 tag 的 `trunk/conf/full.conf`：`listen`、
`max_connections`、`daemon off`、`srs_log_tank console`、`srs_log_level_v2`、
`http_api { enabled / listen [ip:]port / crossdomain / raw_api { enabled,
allow_reload } }`、`vhost __defaultVhost__ { enabled }` 均存在；`http_api.listen`
的 `127.0.0.1:1985` 形式实测解析正常，API 只绑定回环。

## 2. 交付物清单

### 新增（入库）

```text
deploy/srs/conf/srs-minimal.conf
deploy/srs/systemd/rtmp-monitor-srs.service
deploy/srs/media-server.example.ini
scripts/srs/srs_dev_wsl.ps1          Check/Start/Status/Test/Stop，所有权状态文件
scripts/srs/verify_srs_chain.ps1     Windows+WSL 双侧端到端验收（含 soak）
scripts/srs/build_srs_arm64.sh       --source-dir/--prefix/--config/--mode native|cross/--cross-prefix
scripts/srs/verify_srs_chain.sh      Linux/ARM 设备端链验收
include/common/server/MediaServerTypes.h
include/common/server/RtmpUrlBuilder.h          + src
include/common/server/MediaServerConfiguration.h + src
include/common/server/MediaServerMonitor.h       + src
tests/RtmpUrlBuilderTest.cpp          (6 用例)
tests/MediaServerConfigurationTest.cpp (12 用例)
tests/MediaServerMonitorTest.cpp      (5 用例，含假 Server 防抖)
docs/versions/rtmp-v1/guides/build-and-testing/srs_failure_recovery.md
docs/versions/rtmp-v1/weeks/week7/week7_srs_server_integration.md（本文）
```

### 修改（入库）

```text
CMakeLists.txt                       Qt6 Network、rtmp_monitor_server 库、3 个测试目标
src/main.cpp                         --media-server-config、--no-camera-autostart、
                                     监控组合与 server 模块日志
include/common/app/StreamConnectionController.h/.cpp
                                     setMediaServerEndpoint（仅默认 URL 生成）与
                                     addConnection(profile, endpoint) 重载
include/common/logging/UserMessageTypes.h、src/common/logging/UserMessageService.cpp
                                     ServerHealthy/ServerUnavailable 用户消息
README.md                            SRS 使用章节与状态行
docs/versions/rtmp-v1/guides/build-and-testing/rtmp_chain_verification.md   §15 SRS 入口（nginx 历史保留）
docs/versions/rtmp-v1/guides/build-and-testing/cross_platform_build.md      §9 SRS ARM 部署边界
docs/memory/known_issues.md          ISSUE-008
```

### 未改动（按约束）

`FFmpegPlayer`、`MultiStreamPlaybackManager`、邮箱、OpenGL/渲染、网格与
全屏链路、SRS 核心源码；未新增 `SrsManager`/第二套 `StreamManager`/runtime
类层次；未启用 HTTP Callback、HLS/WebRTC/SRT/集群；历史 nginx 脚本未删。

## 3. 逐 Phase 结果（按方案汇报模板）

### Phase 1 结果（WSL2 SRS 环境）

- Git 基线：5b38009；实施前是否干净：no（用户 docs 改动，全程保留）。
- SRS：6.0.184 / v6.0-r0 / 9f8670b。
- 平台：Windows 11 + WSL2 Ubuntu-22.04-New。
- 修改文件：`rtmp_chain_verification.md`（§15）。
- 新增文件：`deploy/srs/conf/srs-minimal.conf`、`scripts/srs/srs_dev_wsl.ps1`、
  `scripts/srs/verify_srs_chain.ps1`。
- Build：`./configure --prefix=$HOME/opt/srs-6.0.184 && make -j16 && make install`
  -> pass（两处环境修正，见第 4 节 T1/T2）。
- Run：`srs_dev_wsl.ps1 -Action Start` -> pass（setsid 后台持有，见 T3）。
- Test：Check/Status/Test 与 `verify_srs_chain.ps1 -Action Verify` -> 全 pass。
- 端口/API：1935=0.0.0.0 LISTEN；1985=127.0.0.1 LISTEN；
  `/api/v1/versions` code=0、version=6.0.184（WSL 与 Windows 双侧）。
- 推流：SRS 自带 `doc/source.flv` copy 编码 -> `live/camera01`
  （H.264 768x320 + AAC）。
- 拉流：WSL/Windows ffprobe 均读到 h264；Windows ffmpeg 连续解码 45 帧成功。
- 异常注入：未知 python3 占用 1935 -> Start 拒绝、退出码 3、进程未被杀。
- 未解决：`[需要验证]` LAN 入站摄像头推流；ffplay 可视化为人工项
  （自动化以解码替代）。
- 日志与产物：`out/srs/`（被 Git 忽略）、`~/srs-run/`（WSL）。
- 是否满足完成标准：yes（LAN 入站除外，属方案既有 `[需要验证]` 第 1 项）。

### Phase 2 结果（ARM Linux 环境）

- 修改文件：`cross_platform_build.md`（§9）。
- 新增文件：`scripts/srs/build_srs_arm64.sh`、`scripts/srs/verify_srs_chain.sh`、
  `deploy/srs/systemd/rtmp-monitor-srs.service`。
- Build（交叉验证）：`build_srs_arm64.sh --mode cross
  --cross-prefix aarch64-linux-gnu-` -> pass；
  产物 `objs/srs` 为 `ELF 64-bit ARM aarch64`，NEEDED 仅
  libstdc++/libm/libgcc_s/libc/ld-linux-aarch64。
- Run/Test：`verify_srs_chain.sh` 在 WSL x86_64 对真实 SRS 全 pass
  （脚本平台无关，供目标板复用）。
- 未解决：`[需要验证]` 目标 ARM 板本机构建、systemd、LAN 推拉流、重启
  自恢复、最小权限服务用户。交叉构建不替代实机结果。
- 是否满足完成标准：脚本与交叉 ELF 门禁 yes；实机部分待硬件。

### Phase 3 结果（FFmpeg 推流）

- 修改文件：无新增 C++；验证脚本已参数化 `-StreamKey/-Application/
  -SoakSeconds/-SourceFile`。
- Run：`verify_srs_chain.ps1 -Action Verify -SoakSeconds 600`。
- Test：publisher 连续 600 秒推流保持（API 周期性采样无掉线）；
  停推后 API 流消失；同 URL 恢复再次 active -> 全 pass。
- 未解决：`[需要验证]` 真实摄像头 H.264 profile/level/GOP/时间戳兼容性；
  摄像头到 Windows 主机的 LAN 推流路径。
- 是否满足完成标准：FFmpeg 合成源部分 yes；摄像头部分待硬件。

### Phase 4 结果（现有客户端拉流）

- Build：`cmake --build out\build-windows-x64\debug` + 完整 CTest 14/14
  （103.95 秒）-> pass。
- Run：`rtmp_monitor --renderer=auto --url rtmp://127.0.0.1:1935/live/camera02`
  （`--metrics-file` 采样）。
- Test：状态 `playing`、decodedFrames 持续增长（11->83）；renderer
  requested=auto、active=opengl（RTX 3060 Desktop GL 3.3、无 fallback）；
  停推后转 `reconnecting`，恢复后 15 秒内重回 `playing`（reconnects=1）；
  WM_CLOSE 优雅退出 exitCode=0、无进程/线程残留 -> 全 pass。
- 未修改 `FFmpegPlayer`/OpenGL/邮箱/网格：确认。
- 是否满足完成标准：yes。

### Phase 5 结果（Server 配置与健康接入）

- 修改文件：`CMakeLists.txt`、`src/main.cpp`、
  `StreamConnectionController.h/.cpp`（仅默认 URL 生成）；未改 MainWindow。
- 新增文件：见第 2 节 server/ 与 tests/ 清单、`media-server.example.ini`。
- 核心接口：方案 9.2 全部落地；无 `startSrs()/killSrs()`/Docker/systemctl。
- Build：Windows Debug 17/17（85.11 秒）-> pass；ARM64 交叉 115/115 -> pass。
- Test：
  - 纯逻辑：默认值、非法 host/port/app/streamKey、IPv4/IPv6、配置覆盖
    （23 个新用例）。
  - 假 Server：TCP-only -> Degraded；200+code 0 -> Healthy（version 正确）；
    非 JSON/code!=0 -> Degraded；无监听 -> Unavailable；flap 防抖不抖动。
  - 集成：真实 SRS + `--media-server-config deploy/srs/media-server.example.ini`，
    system.jsonl 记录 `config_loaded` -> `health_changed checking -> healthy
    （rtmpPortReachable=true, apiReachable=true, serverVersion=6.0.184）`。
- 监控 2 秒轮询不刷日志（只在状态变化时记录/提示）；API 探测不阻塞 GUI；
  Server 故障不停止播放器，播放器不依赖 API。
- 是否满足完成标准：yes。

### Phase 6 结果（多摄像头配置）

- 修改文件：`MediaServerConfiguration.*`（profile 解析校验）、
  `StreamConnectionController.*`（profile 重载 + cameraId 会话内查重）、
  `src/main.cpp`（profile 自动接入，`--url` 优先，重复跳过）。
- 核心接口：`loadCameraProfiles(...)`、
  `addConnection(profile, endpoint, startImmediately)`；现有
  `addConnection(displayName, rtmpUrl, ...)` 保留。
- Test：
  - 纯逻辑：重复 cameraId、重复 streamKey、非法 key、17 路拒绝（并入
    controller/config 测试目标，CTest 17/17 全绿，85.22 秒）。
  - 真实 SRS 4 路：profile -> URL builder -> 4/4 playing；停 camera03 其余
    3 路继续；恢复后 camera03 自动 Playing -> pass。
  - 真实 SRS 16 路：16 profile 16/16 playing；停 camera03 其余 15 路继续；
    恢复后 20 秒内 camera03 Playing -> pass。
- profile -> StreamId/Player 映射稳定，不依赖网格位置；16 路 URL 唯一；
  不含 API 凭据。
- 是否满足完成标准：yes（既有 16 路性能门禁不退化的正式复测未在本阶段
  重跑，延续 ISSUE-002 的既有结论）。

### Phase 7 结果（异常恢复与发布门禁）

- 新增文件：`docs/versions/rtmp-v1/guides/build-and-testing/srs_failure_recovery.md`；
  一次性故障注入脚本 `out/srs/test_phase7_recovery.ps1`（忽略入库）。
- Test（真实 SRS + 真实客户端，结果矩阵见指南 §2）：
  1. 4 路 Playing 中精确停止自有 SRS -> 全部离开 Playing，监控防抖后
     `unavailable`。
  2. 重启 SRS（ffmpeg publisher 无客户端重试，同步重启）-> 4/4 在 20 秒
     内恢复 Playing，监控 `healthy`；不要求重启 Qt 应用。
  3. 未知占用 1935 -> 脚本拒绝、退出码 3、未杀。
  4. API 故障（RTMP 正常）-> 4/4 继续 Playing，监控 `degraded`。
  5. 单 publisher 中断 -> 其余路不受影响，恢复后自动 Playing（Phase 6）。
  6. 应用退出 -> exitCode=0、无残留、监控资源全部释放。
- Windows 完整 CTest 17/17；ARM64 交叉构建通过；ARM 真机恢复
  `[需要验证]`。
- 非零 `--max-reconnect-failures` 的 Healthy 上升沿自动 restart：未启用，
  保持方案 `[需要验证]` 第 9 项；默认无限重试已验证覆盖常规恢复。
- 是否满足完成标准：Windows 侧 yes；ARM 真机部分待硬件。

## 4. 实施中发现并固化的关键事实

- **T1 无 sudo 的 WSL 构建**：apt 无 tclsh candidate 且无密码 sudo。从
  Ubuntu 官方源 HTTPS 下载 `tcl8.6`/`libtcl8.6` 8.6.12+dfsg-1build1 deb 并
  `dpkg -x` 解包到 `$HOME/local`（`tclsh -> tclsh8.6` 软链，配
  `LD_LIBRARY_PATH`/`TCL_LIBRARY`）。固定 tag 的 `Dockerfile.builds` 依赖清单
  为 `gcc make g++ patch unzip perl git libasan5`，本机已有 libasan6。
- **T2 vendored SRT 的 tclsh shebang**：`3rdparty/srt-1-fit/configure` 等使用
  `#!/usr/bin/tclsh`（非 env），用户态 tclsh 不在该路径会导致
  `env: './configure': No such file or directory`。处理：仅改写 SRS **源树**
  （`~/src`，仓库外）这些脚本首行为 `#!/usr/bin/env tclsh`；属构建宿主
  bootstrap，不改 SRS 功能、不入库、不形成 fork。
- **T3 WSL 后台进程存活**：`wsl.exe -- bash -lc/cmd` 退出时会话回收会带走
  未脱离的子进程；`setsid nohup ... </dev/null &` 之后必须 `disown` 并
  `sleep 1`，否则 setsid 子进程在 exec 前被杀（日志为空、无端口）。
  全部脚本已按此固化。
- **T4 PowerShell 调用 wsl.exe**：`Start-Process -RedirectStandardInput` 会
  丢失 ExitCode；改用 `System.Diagnostics.ProcessStartInfo`
  （UseShellExecute=false）+ stdin 喂 `bash -s` + 异步抽取 stdout/stderr，
  规避所有 `wsl.exe -lc` 参数引用层（`$PATH` 会被中间 shell 提前展开，
  内含空格/括号的 Windows 路径段直接造成语法错误）。Git Bash 侧再调用时
  需 `MSYS_NO_PATHCONV=1` 防止 `/d /s /c` 与 `/mnt/...` 被转换。
- **T5 隐藏窗口进程的优雅退出**：`Start-Process -WindowStyle Hidden` 的
  `MainWindowHandle==0`，`CloseMainWindow()` 返回 False。改为 EnumWindows
  按 PID 找顶层窗口并 PostMessage(WM_CLOSE)，客户端优雅退出 exitCode=0。
- **T6 SRS API 分页**：`/api/v1/streams/` 默认只返回 10 条（`start/count`
  查询参数，默认 count=10）。多流验收必须显式
  `/api/v1/streams/?start=0&count=N`；无斜杠路径会 301 到带斜杠版本。
  曾因此误判"SRS 只注册 10 路流"（方案 `[需要验证]` 第 7 项已据此回答）。
- **T7 mirrored networking 的关闭端口行为**：连接本机已关闭端口不立即
  RST，探测按 1.5 秒超时耗尽；SRS 停止到监控 `unavailable` 事件约 10 秒
  （3 次失败防抖）。恢复为 `healthy` 约 2～4 秒（2 次成功防抖）。
- **T8 ffmpeg publisher 无重连**：SRS 停止后 ffmpeg publisher 直接退出；
  故障恢复测试在重启 SRS 后必须重启 publisher（真实摄像头自行重推）。
- **T9 metrics JSON 读取**：Qt 每秒原子替换 metrics 文件；PowerShell 读取
  需 `FileShare.ReadWrite` + UTF-8 + 重试，且中文 displayName 不能依赖
  控制台默认编码（PS 5.1 `Get-Content` 按 ANSI 读取会乱码）。

## 5. 架构落点（与方案第 9/11 节一致）

```text
deploy/srs/conf + scripts/srs + systemd unit   <- 平台部署层（拥有进程）
        |
MediaServerConfiguration -> MediaServerEndpoint/CameraStreamProfile
RtmpUrlBuilder           -> rtmp://host:port/app/streamKey
MediaServerMonitor       -> 只读异步健康（不启停 SRS）
        |
StreamConnectionController -> MultiStreamPlaybackManager -> FFmpegPlayer
        -> LatestFrameMailbox -> 既有 renderer/UI（未改动）
```

## 6. 当前 `[需要验证]` 清单（承接方案第 15 节）

1. LAN 入站：真实摄像头从 LAN 访问 Windows 主机 1935（须外部设备）。
2. ARM 目标板本机构建、systemd、LAN 推拉流、重启自恢复、服务用户权限。
3. 真实摄像头 H.264 profile/level/GOP/时间戳与播放器兼容性。
4. 非零 `--max-reconnect-failures` 达上限后的 Server 恢复策略（默认无限
   重试已覆盖常规场景）。
5. Docker 备选路径：`ossrs/srs:6.0.184` arm64 manifest、PowerShell bind
   mount 转义（本轮未启用 Docker）。
6. ~~SRS `/api/v1/streams` 精确 JSON 字段路径~~ -> 已回答：默认分页
   count=10，需 `?start=0&count=N`；流对象含
   `name/app/tcUrl/url/clients/frames/recv_bytes/kbps/publish.active/
   video.codec`（见 T6）。

板卡、系统、SDK/ABI、Qt/QPA、EGL/GLES、显示输入和摄像头编码参数由接收方填写
[嵌入式二次开发交接与环境填写模板](../../guides/build-and-testing/embedded_developer_handoff.md)；
不得用交叉构建结果代替真机字段。

## 7. 复现入口

```powershell
# SRS 生命周期（只管理脚本自有进程）
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\srs\srs_dev_wsl.ps1 -Action Check|Start|Status|Test|Stop

# 端到端验收（含 10 分钟保活）
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\srs\verify_srs_chain.ps1 -Action Verify -SoakSeconds 600

# 客户端基线
cmake --build out\build-windows-x64\debug --config Debug
ctest --test-dir out\build-windows-x64\debug -C Debug --output-on-failure
.\out\build-windows-x64\debug\rtmp_monitor.exe --media-server-config .\deploy\srs\media-server.example.ini
```

故障恢复矩阵与发布门禁：
[SRS 异常恢复与发布门禁验证指南](../../guides/build-and-testing/srs_failure_recovery.md)
