# RtmpMonitor SRS Server 接入实施方案

> 文档状态：架构与实施基线；Phase 1～7 已落地，未完成项以项目快照和已知问题为准
>
> 调研日期：2026-08-08
>
> 目标执行者：Kimi K3
>
> 范围：SRS 部署、RTMP 推拉链路、Qt 客户端最小接入、双平台部署与异常恢复
>
> 非目标：修改 SRS 核心、重写 `FFmpegPlayer`、重写 OpenGL、WebRTC/HLS/集群/公网/复杂鉴权

## 1. 执行摘要与不可变决策

本阶段只建立以下主链路：

```text
Camera / FFmpeg Publisher
    -> RTMP Push
SRS 6.x stable
    -> RTMP Pull
现有 MultiStreamPlaybackManager / FFmpegPlayer
    -> 现有 FFmpeg Decoder
    -> 现有 LatestFrameMailbox / OpenGL Renderer
    -> Qt VideoWidget
```

必须遵守以下决策：

1. 使用官方稳定版 SRS 6.0，实施时固定到 `v6.0-r0`（内部版本
   `6.0.184`），不得直接跟随 `develop`、SRS 7 unstable 或 SRS 8 开发线。
2. Windows 开发机的首选方案是 **WSL2 Ubuntu 内源码构建和运行 SRS**；
   Docker Desktop 是快速烟测与可复现环境的备选。不得假设 SRS 可以由 MSVC
   原生编译。
3. ARM Linux 首选在目标设备本机源码编译；只有目标设备资源不足、工具链明确且
   ABI 已冻结时才交叉编译。
4. SRS 是独立基础设施。开发期由脚本/WSL/Docker 管理，ARM 产品期由 systemd
   管理；Qt 客户端第一版只观察健康状态，不拥有或强制终止 SRS 进程。
5. `FFmpegPlayer` 只感知合法 RTMP URL，不得包含 SRS 特有逻辑。现有播放器、解码、
   邮箱、OpenGL、网格和全屏链路保持不变。
6. 不新增与现有 `StreamConnectionController`、`MultiStreamPlaybackManager` 重叠的
   `StreamManager`。第一版仅在确有需要时新增 `MediaServerEndpoint`、
   `RtmpUrlBuilder` 和只读的 `MediaServerMonitor`。
7. 第一版启用 HTTP API 仅用于本机健康检查；HTTP Callback、`on_publish`、
   `on_unpublish` 暂不启用。
8. 1935 被未知进程占用时必须失败并报告，不得按进程名批量杀进程，也不得接管不属于
   当前脚本/应用的 SRS、nginx、Docker 或 FFmpeg 进程。

## 2. 已核对的仓库现状

以下结论来自当前源码、CMake 和项目文档，不是根据历史描述推测：

- `StreamConnectionController` 已负责设备名称、URL、稳定 `StreamId`、视频格、用户操作、
  日志与审计的协调。
- `MultiStreamPlaybackManager` 已负责 0～16 路连接、每路 `FFmpegPlayer`、共享解码池、
  启停、重启、指标和邮箱。
- `FFmpegPlayer::start()` 已使用严格 `QUrl` 校验，仅接受含主机和路径的
  `rtmp://` URL；每路具有独立网络线程、可中断阻塞 I/O、3 秒默认重连和可选无限重试。
- `MainWindow::updateDeviceStatus()` 在 Connecting、Reconnecting、Error、Disconnected
  时清除旧帧，因此 SRS 崩溃后不会长期显示伪实时静止画面。
- 解码输出已经进入 `LatestFrameMailbox`，OpenGL/CPU renderer 与本次 Server 接入无关。
- 当前 UI 与脚本的历史默认流名多为 `camera001`；需求示例使用 `camera01`。SRS 对二者
  都支持。本计划不批量改名：新配置允许显式 `streamKey=camera01`，现有
  `camera001` URL 继续兼容。
- 当前工作区已有 nginx-rtmp 验证脚本。迁移到 SRS 时应增量新增 SRS 脚本，不能直接
  删除 nginx 脚本；确认 SRS 全链路通过后再单独决定历史脚本的退役。

## 3. 官方事实、版本选择与证据

### 3.1 版本基线

截至 2026-08-08，SRS 官方发布页将 `v6.0-r0` 标为 Latest、Stable，内部版本为
`6.0.184`，并提供源码包及 `ossrs/srs:6.0.184` Docker 镜像。因此本项目固定：

```text
Git tag:       v6.0-r0
SRS version:   6.0.184
Docker image:  ossrs/srs:6.0.184
```

实施时记录实际 commit 和镜像 ID；升级必须另开任务，重新执行本文全部门禁。

### 3.2 官方资料已确认的事实

- 官方构建流程是在 Linux/Ubuntu 中进入 `trunk` 后执行 `./configure && make`，并以
  `./objs/srs -c <config>` 启动；官方同时明确总体上高度推荐 Docker。
- SRS 的稳定 Windows 路线不是 MSVC 原生编译。官方 Windows 文档描述的是
  Cygwin64 + GCC/G++，并列出 SRT、线程和原生 IOCP 等已知问题。
- 官方稳定发布的 `v6.0-r0` 资产未提供对应的稳定 Windows 安装包，只提供源码和
  CentOS x86_64 二进制；较早的 6.0 预发布曾提供 Windows 安装包，不能据此推断稳定版
  原生 Windows 交付已经成熟。
- 官方说明 SRS 支持 Linux AArch64；ARM Server 推荐在设备本机直接
  `./configure && make`，同时提供 `--cross-build --cross-prefix=...` 交叉编译方式。
- RTMP 地址格式为 `rtmp://host:port/app/stream`；发布和播放使用同一个 URL，由 RTMP
  命令区分 publish/play。
- HTTP API 可提供 `/api/v1/versions`、`/api/v1/summaries`、
  `/api/v1/streams` 等 JSON 接口。RAW API 默认应关闭。
- HTTP Callback 中 `on_publish`/`on_play` 是同步判定点：非 HTTP 200 或非成功返回值
  会拒绝连接。因此不能把它直接指向可能未启动的桌面 Qt 客户端。

### 3.3 主要官方来源

- [SRS 6.0 stable release](https://github.com/ossrs/srs/releases/tag/v6.0-r0)
- [SRS 官方源码构建](https://ossrs.io/lts/en-us/docs/v6/doc/getting-started-build)
- [SRS 官方 Docker 快速入门](https://ossrs.io/lts/en-us/docs/v6/doc/getting-started)
- [SRS RTMP](https://ossrs.io/lts/en-us/docs/v5/doc/rtmp)
- [SRS HTTP API](https://ossrs.io/lts/en-us/docs/v5/doc/http-api)
- [SRS HTTP Callback](https://ossrs.io/lts/en-us/docs/v5/doc/http-callback)
- [SRS ARM 与交叉编译](https://ossrs.io/lts/en-us/docs/v5/doc/arm)
- [SRS Windows/Cygwin](https://ossrs.net/lts/en-us/docs/v6/doc/windows)
- [Microsoft WSL 网络](https://learn.microsoft.com/windows/wsl/networking)
- [Qt QProcess](https://doc.qt.io/qt-6/qprocess.html)
- [Qt QNetworkAccessManager](https://doc.qt.io/qt-6/qnetworkaccessmanager.html)
- [FFmpeg CLI](https://ffmpeg.org/ffmpeg.html)
- [ffplay CLI](https://ffmpeg.org/ffplay.html)

> 注：SRS 6 网站的部分 OpenAPI/ARM 页面会跳转或复用 5.0 文档。本文只采用在
> `v6.0-r0` 配置文件/发布说明中仍存在的字段；Kimi 在实施时仍须以固定 tag 内的
> `trunk/conf/full.conf` 和实际启动结果复核。若字段不一致，标记 `[需要验证]`，不得猜测。

## 4. Windows x64 部署方案比较与结论

| 方案 | 可行性 | 优点 | 缺点/风险 | 本项目结论 |
|---|---|---|---|---|
| MSVC 原生 | 不成立 | 理论上与 Qt 进程同平台 | 官方没有 MSVC 原生构建链；不能把 Cygwin 误写成 MSVC | 禁止作为方案 |
| Cygwin64 `srs.exe` | 可实验 | Windows 可执行文件，官方曾支持 | POSIX 兼容层；稳定发布资产不完整；官方列有线程/SRT/IOCP 已知问题 | 仅实验，不做正式基线 |
| WSL2 Ubuntu 源码构建 | 可行 | 与官方 Linux 构建一致；易查看源码/日志；接近 ARM Linux；当前机器已有 Ubuntu 22.04 WSL2 和 mirrored networking | WSL 生命周期、Windows 防火墙和 LAN 入站需验证 | **当前开发机首选** |
| Docker Desktop/WSL2 backend | 可行且官方推荐 | 最快、隔离、可复现、官方镜像、多架构 | 增加 Docker daemon/镜像依赖；挂载配置和进程归属需管理 | 快速烟测/CI/回退首选 |
| 独立 Linux VM/远端 Linux | 可行 | 最接近生产 | 部署和网络成本更高 | 后续集成环境，不是第一步 |

最终选择：

```text
日常开发与配置调试：WSL2 Ubuntu + 固定 tag 源码构建
快速复现/CI：Docker Desktop + 固定镜像 tag
正式 Windows 原生 Qt 应用：只连接 WSL2/Docker/外部 SRS，不承载 MSVC SRS
```

## 5. SRS 最小配置

Kimi 在 Phase 1 新增 `deploy/srs/conf/srs-minimal.conf`，内容以固定 tag 的
`trunk/conf/full.conf` 复核后采用：

```conf
# RtmpMonitor Phase 1: RTMP push/pull + local health API only.
listen                  1935;
max_connections         100;

daemon                  off;
srs_log_tank            console;
srs_log_level_v2        info;

http_api {
    enabled             on;
    listen              127.0.0.1:1985;
    crossdomain         off;
    raw_api {
        enabled         off;
        allow_reload    off;
    }
}

vhost __defaultVhost__ {
    enabled             on;
}
```

配置含义：

- 1935 对所有接口监听，以便本机、局域网摄像头和客户端推拉 RTMP。
- 1985 只绑定 Server 本机回环，用于进程健康、版本和流状态查询。
- `daemon off + console` 让 WSL 前台、Docker logs 和 systemd journal 正确接管日志。
- 不出现 `http_server`、`hls`、`rtc_server`、`srt_server`、`dvr`、`transcode`、
  `ingest`、`forward`、`cluster` 或 `http_hooks`，即不启用当前不需要的能力。
- RAW API 显式关闭，客户端不得通过 API 动态改写 SRS 配置。

`http_api.listen` 的 `[ip:]port` 形式由官方文档支持。若固定 tag 实际解析失败，使用
`listen 1985;` 并通过主机防火墙只允许本机，记录为 `[需要验证]`，不得悄悄暴露到 LAN。

当未来确需从另一台机器访问 API 时，才允许把 API 绑定到管理网地址，并同时启用
Basic Auth、限制防火墙来源。密码不得进入仓库、命令行报告或 RTMP URL。

## 6. Windows 开发机：WSL2 首选方案完整步骤

### 6.1 前置检查

在 Windows PowerShell：

```powershell
wsl.exe --list --verbose
wsl.exe --status
Get-NetTCPConnection -LocalPort 1935 -State Listen -ErrorAction SilentlyContinue
Get-NetTCPConnection -LocalPort 1985 -State Listen -ErrorAction SilentlyContinue
```

若 1935 已监听：

1. 记录 PID、进程路径、所属环境；
2. 若 `/api/v1/versions` 能确认是本项目预期的 SRS，可选择复用；
3. 若是 nginx、其他 SRS、Docker 或未知进程，停止实施并报告；
4. 不得自动 `Stop-Process`、`taskkill`、`killall` 或结束全部同名进程。

当前机器已验证使用 WSL2 mirrored networking。Microsoft 文档说明该模式下 Windows
与 WSL 可双向使用 `127.0.0.1`。摄像头从 LAN 访问 Windows 主机 1935 的路径仍必须
在 Phase 3 实机验证，不能用本机 localhost 结果替代。

### 6.2 获取稳定源码

进入 WSL2 Ubuntu：

```bash
sudo apt-get update
sudo apt-get install -y \
  ca-certificates git gcc g++ make patch unzip perl curl jq ffmpeg

mkdir -p "$HOME/src"
cd "$HOME/src"
git clone --branch v6.0-r0 --depth 1 \
  https://github.com/ossrs/srs.git srs-6.0.184
cd srs-6.0.184
git rev-parse HEAD
git describe --tags --always
```

预期 tag 为 `v6.0-r0`。官方 release commit 为
`9f8670b2a832aea04abe644af47261838838c49a`；若实际不同，停止并调查。

### 6.3 编译与用户目录安装

```bash
cd "$HOME/src/srs-6.0.184/trunk"
./configure --prefix="$HOME/opt/srs-6.0.184"
make -j"$(nproc)"
make install

"$HOME/opt/srs-6.0.184/objs/srs" -v
file "$HOME/opt/srs-6.0.184/objs/srs"
ldd "$HOME/opt/srs-6.0.184/objs/srs"
```

若 `./configure` 报缺包，只能根据错误和固定 tag 的 Dockerfile/README 增补依赖；不得
直接安装不相关的全套音视频组件。官方稳定 Dockerfile 的基础构建依赖包含
`gcc make g++ patch unzip perl git`。

### 6.4 安装最小配置并启动

Phase 1 实施后，从仓库复制配置：

```bash
install -m 0644 \
  <repo-in-wsl>/deploy/srs/conf/srs-minimal.conf \
  "$HOME/opt/srs-6.0.184/conf/rtmp-monitor.conf"

cd "$HOME/opt/srs-6.0.184"
./objs/srs -c conf/rtmp-monitor.conf
```

保持此终端前台运行。开发首测不使用 `nohup`，便于直接观察配置解析和连接日志。

### 6.5 检查端口与 HTTP API

在第二个 WSL 终端：

```bash
ss -ltnp 'sport = :1935'
ss -ltnp 'sport = :1985'
curl --fail --silent --show-error \
  http://127.0.0.1:1985/api/v1/versions | jq .
```

在 Windows PowerShell：

```powershell
Test-NetConnection 127.0.0.1 -Port 1935
Test-NetConnection 127.0.0.1 -Port 1985
Invoke-RestMethod http://127.0.0.1:1985/api/v1/versions
```

通过条件：1935、1985 均可达，API 为 HTTP 200，JSON 顶层 `code` 为 0，并能读取
6.0.184 版本信息。

### 6.6 FFmpeg 测试推流

优先使用 SRS 仓库自带 H.264/AAC FLV，避免本机 FFmpeg 缺少 `libx264`：

```bash
ffmpeg -re -stream_loop -1 \
  -i "$HOME/src/srs-6.0.184/trunk/doc/source.flv" \
  -c copy -f flv \
  rtmp://127.0.0.1:1935/live/camera01
```

若要从任意测试视频编码，先确认 `ffmpeg -encoders` 包含 `libx264`，再用：

```bash
ffmpeg -re -stream_loop -1 -i <test-video> \
  -map 0:v:0 -an \
  -c:v libx264 -preset veryfast -tune zerolatency \
  -pix_fmt yuv420p -g 50 -keyint_min 50 -sc_threshold 0 \
  -f flv rtmp://127.0.0.1:1935/live/camera01
```

真实摄像头推流时，目标不能写 `127.0.0.1`，必须写 Windows 开发机在摄像头所在 LAN
中的地址：

```text
rtmp://<WINDOWS-LAN-IP>:1935/live/camera01
```

只允许为 Windows “专用网络”增加 TCP 1935 入站规则；不得关闭整个防火墙。WSL
mirrored networking 下 LAN 入站能否直接转入发行版，必须由第二台设备或真实摄像头
验证并记录 `[需要验证]`。

### 6.7 FFmpeg/ffplay 测试拉流

```bash
ffprobe -v error \
  -show_entries stream=index,codec_type,codec_name,width,height \
  -of json rtmp://127.0.0.1:1935/live/camera01

ffplay -fflags nobuffer -flags low_delay -framedrop \
  rtmp://127.0.0.1:1935/live/camera01
```

同时检查 SRS 流 API：

```bash
curl --fail --silent --show-error \
  http://127.0.0.1:1985/api/v1/streams | jq .
```

通过条件：`ffprobe` 能读到 H.264 视频流，`ffplay` 连续显示运动画面，API 中存在
`live/camera01` 且 publish active。具体 JSON 字段路径以 6.0.184 实际输出为准；若与
旧文档不同，标记 `[需要验证]`，不要硬编码旧字段。

### 6.8 停止与清理

前台 SRS 使用 `Ctrl+C`；前台 FFmpeg 同样使用 `q` 或 `Ctrl+C`。随后确认：

```powershell
Get-NetTCPConnection -LocalPort 1935 -State Listen -ErrorAction SilentlyContinue
Get-NetTCPConnection -LocalPort 1985 -State Listen -ErrorAction SilentlyContinue
```

脚本只能停止自己启动且身份匹配的进程。状态记录至少包含 PID、启动时间、可执行路径、
配置路径和运行 ID。

## 7. Windows 开发机：Docker 备选完整步骤

### 7.1 拉取并确认镜像

```powershell
docker pull ossrs/srs:6.0.184
docker image inspect ossrs/srs:6.0.184
```

必须记录 RepoDigest 或 image ID，不能只记录浮动 `ossrs/srs:6`。

### 7.2 使用仓库最小配置启动

在仓库根目录 PowerShell：

```powershell
$configPath = (Resolve-Path .\deploy\srs\conf\srs-minimal.conf).Path

docker run --name rtmp-monitor-srs --detach `
  --publish 1935:1935 `
  --publish 127.0.0.1:1985:1985 `
  --mount "type=bind,source=$configPath,target=/usr/local/srs/conf/rtmp-monitor.conf,readonly" `
  ossrs/srs:6.0.184 `
  ./objs/srs -c conf/rtmp-monitor.conf
```

容器内命令路径必须使用 Linux 形式。Kimi 必须在脚本 SelfTest 中验证 PowerShell
bind mount 与参数传递，并固定经过实测的转义方式。

### 7.3 验证与停止

```powershell
docker ps --filter name=rtmp-monitor-srs
docker logs --tail 100 rtmp-monitor-srs
Test-NetConnection 127.0.0.1 -Port 1935
Invoke-RestMethod http://127.0.0.1:1985/api/v1/versions
```

推拉流命令与 WSL 方案相同。停止：

```powershell
docker stop --time 10 rtmp-monitor-srs
docker rm rtmp-monitor-srs
```

不得在没有确认容器名、ID 和 label 的情况下执行批量删除。正式脚本应给容器增加
`com.rtmp-monitor.owner` 与唯一 run-id label，并按 label 精确管理。

## 8. ARM Linux 部署

### 8.1 首选：目标设备本机编译

官方 ARM 文档建议 ARMv7/ARMv8 Server 尽量本机编译。目标设备执行：

```bash
uname -a
uname -m
getconf GNU_LIBC_VERSION || true

sudo apt-get update
sudo apt-get install -y \
  ca-certificates git gcc g++ make patch unzip perl curl jq ffmpeg

mkdir -p "$HOME/src"
cd "$HOME/src"
git clone --branch v6.0-r0 --depth 1 \
  https://github.com/ossrs/srs.git srs-6.0.184
cd srs-6.0.184/trunk

./configure --prefix=/opt/rtmp-monitor/srs-6.0.184
make -j"$(nproc)"
sudo make install

/opt/rtmp-monitor/srs-6.0.184/objs/srs -v
file /opt/rtmp-monitor/srs-6.0.184/objs/srs
ldd /opt/rtmp-monitor/srs-6.0.184/objs/srs
```

在内存很小的设备上，先用 `make -j1`。若架构未被自动识别，官方旧文档给出
`--extra-flags='-D__aarch64__'`，但只有实际失败并核对固定 tag 后才使用，标记
`[需要验证]`。

### 8.2 配置与 systemd

复制配置：

```bash
sudo install -d -m 0755 /etc/rtmp-monitor
sudo install -m 0644 \
  <repo>/deploy/srs/conf/srs-minimal.conf \
  /etc/rtmp-monitor/srs.conf
```

Phase 2 新增 `deploy/srs/systemd/rtmp-monitor-srs.service`：

```ini
[Unit]
Description=RtmpMonitor SRS Media Server
Wants=network-online.target
After=network-online.target

[Service]
Type=simple
WorkingDirectory=/opt/rtmp-monitor/srs-6.0.184
ExecStart=/opt/rtmp-monitor/srs-6.0.184/objs/srs -c /etc/rtmp-monitor/srs.conf
KillSignal=SIGQUIT
TimeoutStopSec=15
Restart=on-failure
RestartSec=2
NoNewPrivileges=true

[Install]
WantedBy=multi-user.target
```

第一版可先以系统服务默认用户运行以排除权限问题；正式产品化前必须创建最小权限服务
用户并复验端口、日志和升级路径 `[需要验证]`。SRS 以 console 输出，日志由 journald
接管。

```bash
sudo install -m 0644 \
  <repo>/deploy/srs/systemd/rtmp-monitor-srs.service \
  /etc/systemd/system/rtmp-monitor-srs.service
sudo systemctl daemon-reload
sudo systemctl enable --now rtmp-monitor-srs.service

systemctl status rtmp-monitor-srs.service --no-pager
journalctl -u rtmp-monitor-srs.service -n 100 --no-pager
ss -ltnp 'sport = :1935'
curl --fail --silent --show-error \
  http://127.0.0.1:1985/api/v1/versions | jq .
```

防火墙只向摄像头/客户端所在 LAN 网段开放 TCP 1935。1985 保持回环，不向 LAN 开放。

### 8.3 备选：交叉编译 AArch64

仅在本机编译不可接受时使用：

```bash
sudo apt-get install -y \
  gcc-aarch64-linux-gnu g++-aarch64-linux-gnu binutils-aarch64-linux-gnu

cd <srs-source>/trunk
./configure \
  --cross-build \
  --cross-prefix=aarch64-linux-gnu- \
  --prefix=/opt/rtmp-monitor/srs-6.0.184
make -j"$(nproc)"

file objs/srs
aarch64-linux-gnu-readelf -h objs/srs
aarch64-linux-gnu-readelf -d objs/srs
```

交叉构建只证明生成 AArch64 ELF，不证明目标板可运行。必须在目标板再次执行：

```bash
file /opt/rtmp-monitor/srs-6.0.184/objs/srs
ldd /opt/rtmp-monitor/srs-6.0.184/objs/srs
/opt/rtmp-monitor/srs-6.0.184/objs/srs -v
```

以下内容在硬件/SDK 未确定前全部标记 `[需要验证]`：目标 glibc/musl、动态加载器、
OpenSSL/其他第三方依赖、厂商 sysroot、CPU 指令集、文件系统只读边界和 systemd 可用性。
不得把当前 Qt/FFmpeg ARM64 sysroot 自动假定为 SRS 的正确运行 sysroot。

### 8.4 ARM Docker

SRS 官方项目声明支持 AArch64，历史 changelog 记录官方镜像已支持 aarch64。使用前仍要
确认固定镜像包含目标架构：

```bash
docker buildx imagetools inspect ossrs/srs:6.0.184
docker pull ossrs/srs:6.0.184
docker image inspect ossrs/srs:6.0.184
```

若目标板已有合格 Docker/Containerd，容器方案可作为部署备选；若没有，不要为了 SRS
单独引入完整容器运行时。

## 9. 客户端最小接入架构

### 9.1 不新增 `SrsManager` 或第二套 `StreamManager`

现有职责已足够：

```text
StreamConnectionController
  -> MultiStreamPlaybackManager
      -> one FFmpegPlayer per StreamId
          -> LatestFrameMailbox
              -> existing renderer/UI
```

新增一个既管理摄像头、又管理 SRS、又管理播放器的 `SrsManager` 会与上述三层重叠。
因此第一版只新增两个纯边界与一个观察器：

```text
MediaServerConfiguration / MediaServerEndpoint
  -> 保存和校验 Server host/port/app/API endpoint

RtmpUrlBuilder
  -> MediaServerEndpoint + streamKey -> QUrl

MediaServerMonitor
  -> 异步 TCP/API 健康检查；不启动、不停止、不重启 SRS
```

### 9.2 核心类型与接口

建议接口，名称可因现有命名规范微调，但职责不得扩大：

```cpp
struct MediaServerEndpoint
{
    QString host = QStringLiteral("127.0.0.1");
    quint16 rtmpPort = 1935;
    QString application = QStringLiteral("live");
    QUrl apiBaseUrl = QUrl(QStringLiteral("http://127.0.0.1:1985"));
    bool apiHealthEnabled = true;
};

struct CameraStreamProfile
{
    QString cameraId;      // 稳定配置 ID，例如 camera01
    QString displayName;   // 用户可见名称
    QString streamKey;     // SRS stream，例如 camera01
    bool autoStart = true;
};

std::optional<QUrl> buildRtmpUrl(
    const MediaServerEndpoint &endpoint,
    const QString &streamKey,
    QString *error = nullptr
);

enum class MediaServerState {
    Unknown,
    Checking,
    Healthy,
    Degraded,
    Unavailable
};

struct MediaServerHealth
{
    MediaServerState state = MediaServerState::Unknown;
    bool rtmpPortReachable = false;
    bool apiReachable = false;
    QString serverVersion;
    QString diagnostic;
};

class MediaServerMonitor final : public QObject
{
    Q_OBJECT
public:
    void startMonitoring();
    void stopMonitoring();
    void probeNow();
signals:
    void healthChanged(const MediaServerHealth &health);
};
```

实现约束：

- 使用 Qt Network 的异步 API；不得在 GUI 线程调用阻塞 `waitForConnected()`、
  `waitForFinished()` 或同步 curl 子进程。
- 每 2 秒探测一次；单次超时 1500 ms；连续 3 次失败转 Unavailable，连续 2 次成功转
  Healthy，避免一次丢包造成 UI 抖动。
- API 健康为 `/api/v1/versions` HTTP 200、有效 JSON、`code == 0`。若仅 1935 可达而
  API 不可达，状态为 Degraded，不阻止播放器继续工作。
- 监控器只在状态变化时写日志/用户消息，不能每 2 秒刷屏。
- API 返回和日志不得保存完整 URL 参数、Token 或密码。

### 9.3 地址由谁保存、URL 由谁生成

- 部署/运维配置 `media-server.ini` 保存 `host`、`rtmpPort`、`application`、
  `apiBaseUrl`。由 `MediaServerConfiguration` 读取；第一版不做 Server 设置 UI。
- 摄像头配置只保存 `cameraId`、`displayName`、`streamKey`、`autoStart`，不重复保存完整
  Server 地址。
- `RtmpUrlBuilder` 使用 `QUrl::setScheme/setHost/setPort/setPath` 生成 URL，处理 IPv6
  方括号和转义；不得用字符串拼接接受任意 `/`。
- `application` 与 `streamKey` 第一版只允许一层标识，建议字符集
  `[A-Za-z0-9_-]+`。这符合 SRS 官方对单层 app/stream 的建议。
- 现有 `--url` 和连接对话框完整 URL 继续保留，优先级高于生成式配置，保证兼容现有
  脚本和用户流程。
- `StreamConnectionController` 在会话内持有 camera/profile 到 `StreamId` 的绑定；
  `MultiStreamPlaybackManager` 仍只持有 `StreamConnection`；`FFmpegPlayer` 只收到最终
  URL。

### 9.4 Camera / Stream / Player 一一对应

```text
CameraStreamProfile.cameraId
    -> streamKey
    -> rtmp://host:1935/live/<streamKey>
    -> StreamConnectionController Binding
    -> runtime StreamId
    -> MultiStreamPlaybackManager Entry
    -> one FFmpegPlayer
```

规则：

- 同一配置中 `cameraId` 和 `streamKey` 必须分别唯一。
- 一个 camera profile 对应一个本地 player；SRS 本身可允许多个独立拉流客户端。
- `StreamId` 是进程内会话 ID，不写回摄像头配置，不从网格位置推导。
- 显示名称可修改，不能作为 URL 或持久 ID。
- 最多 16 个 profile，与现有 UI/manager 上限一致。

### 9.5 谁启动和停止 SRS

| 环境 | 启动/停止责任 | Qt 客户端行为 |
|---|---|---|
| Windows WSL2 开发 | 人工终端或仓库脚本；后续可用 WSL systemd | 只监控和连接 |
| Windows Docker 开发/CI | Docker/仓库脚本，按固定容器 ID/label 管理 | 只监控和连接 |
| ARM Linux 产品 | systemd | 只监控和连接 |
| 远端独立 SRS | 远端运维系统 | 只监控和连接 |

第一版不创建 `ISrsRuntime / WindowsSrsRuntime / LinuxSrsRuntime`。业务代码没有平台化
启动需求，真正的平台差异已经集中在脚本、Docker 和 systemd。只有未来产品明确要求
“Qt 必须启动随包分发的本地 Server 子进程”时，才新增 `IMediaServerRuntime`，并至少
提供 `ExternalRuntime` 与 `OwnedProcessRuntime`；不得提前实现。

### 9.6 SRS 崩溃和恢复

既有行为已经覆盖媒体层：

1. SRS 退出或网络断开；
2. 各 `FFmpegPlayer` 独立进入 Error/Reconnecting；
3. UI 清除旧帧并显示重连；
4. `MediaServerMonitor` 经过防抖后报告 Unavailable；
5. systemd/Docker/人工恢复 SRS；
6. 默认无限重试的播放器在下一次 3 秒重连周期自动恢复；
7. 第一帧解码后回到 Playing 和现有 renderer。

不得在 Server unavailable 时调用 `stopAll()`，否则会破坏已有自动恢复。若用户设置了
非零 `--max-reconnect-failures` 且已达到上限，恢复后由用户手动“重新连接”；是否在
Server Healthy 上升沿自动重启这类已停流，放到 Phase 7 单独验证。

### 9.7 1935 端口冲突

预检顺序：

```text
TCP 1935 未监听 -> 可以启动预期 SRS
TCP 1935 已监听 + API/版本/归属匹配 -> 明确选择复用
TCP 1935 已监听 + 归属不匹配/无法识别 -> 失败并报告
```

报告至少包含端口、PID/容器 ID、可执行路径或环境、建议命令；不得打印敏感命令行。
切换到 1936 只允许用于隔离测试，并必须同步摄像头与客户端配置；产品默认仍为 1935。

## 10. HTTP API 与 Callback 决策

### 10.1 第一版使用 HTTP API 的范围

第一版启用只读健康 API，因为单纯 TCP connect 只能证明端口有人监听，不能证明对方是
正确版本的 SRS。使用：

- `/api/v1/versions`：身份和版本健康；
- `/api/v1/summaries`：人工诊断，可选；
- `/api/v1/streams`：Phase 3/6 验收发布流，第一版 UI 不依赖它自动建 player。

不使用：

- RAW reload/update API；
- DELETE client；
- 通过 API 修改 SRS 配置；
- 把 1985 暴露到非管理网络。

### 10.2 第一版不使用 Callback

不启用 `on_publish/on_unpublish` 的原因：

1. 摄像头列表和 streamKey 第一版是预配置的；客户端可先创建 player，publisher 未上线时
   由现有自动重连等待，不需要事件驱动发现。
2. `on_publish` 是同步准入点。桌面客户端未运行、暂停、崩溃或被防火墙阻断时会影响
   摄像头推流，形成错误的反向依赖。
3. 当前项目没有 HTTP Server/control-plane 模块。为了一个事件在 Qt GUI 内增加监听端口、
   请求校验、线程、安全和生命周期，不符合最小接入目标。
4. `on_unpublish` 只能说明 publisher 会话结束，不等价于客户端已经无法播放；媒体层仍
   应以 FFmpeg 实际状态为准。

### 10.3 以后何时引入 Callback

只有满足以下任一需求才重新评估：

- 摄像头 streamKey 不预先登记，需要动态发现；
- 需要服务端发布鉴权/配额；
- 多个客户端需要共享可靠的在线目录；
- 需要审计 publisher 上下线而非仅观察播放器状态。

届时推荐架构：

```text
SRS HTTP Callback
  -> always-on control service / sidecar
      -> validated camera catalog/event stream
          -> Qt client polls/subscribes
```

不要把 `on_publish` 直接指向 GUI。若只是本机动态发现，优先先实现只读轮询
`/api/v1/streams`，确认确有价值后再上 Callback。

## 11. Windows 与 ARM Linux 的统一边界

```text
统一 C++ 业务代码
  MediaServerConfiguration
  RtmpUrlBuilder
  MediaServerMonitor
  StreamConnectionController
  MultiStreamPlaybackManager
  FFmpegPlayer
  Renderer/UI

平台部署层
  Windows: WSL2 script or Docker script
  ARM Linux: native build script + systemd unit
```

业务代码不得出现：

- `#ifdef Q_OS_WIN` 后调用 `wsl.exe` 或 `docker.exe`；
- `#ifdef Q_OS_LINUX` 后调用 `systemctl`；
- SRS 安装目录、Docker 容器名或 systemd unit 名；
- 通过播放器判断 Server 进程归属。

这样无需为设计模式创建 `WindowsSrsRuntime/LinuxSrsRuntime`，平台差异仍被完整隔离。

## 12. 分阶段实施计划

### Phase 1：Windows 开发机 WSL2 SRS 环境跑通

**要做什么**

- 固定并构建 SRS 6.0.184；落地最小配置。
- 提供 WSL2 Check/Start/Status/Stop 与单路推拉验证。
- 验证 Windows localhost、WSL localhost、API 和 1935。

**修改文件**

- `docs/guides/build-and-testing/rtmp_chain_verification.md`：增加 SRS 入口，不删除 nginx 历史。
- `README.md`：Phase 1 成功后才更新默认 Server 说明。

**新增文件**

- `deploy/srs/conf/srs-minimal.conf`
- `scripts/srs/srs_dev_wsl.ps1`
- `scripts/srs/verify_srs_chain.ps1`

**核心接口**

PowerShell 脚本动作固定为：

```text
-Action Check | Start | Status | Test | Stop
```

脚本保存自有运行状态；Stop 只处理身份匹配的自有进程。

**测试方法**

```text
build: ./configure --prefix=... && make && make install
run:   ./objs/srs -c conf/rtmp-monitor.conf
test:  1935 + /api/v1/versions + FFmpeg push + ffprobe/ffplay pull
```

**完成标准**

- SRS 版本、commit、配置、端口、API、推流和拉流证据齐全。
- Windows `rtmp://127.0.0.1:1935/live/camera01` 可推可拉。
- Stop 后只清理自有进程，1935/1985 无意外残留。
- 不改任何 C++ 源码。

### Phase 2：ARM Linux SRS 环境跑通

**要做什么**

- 先在目标 ARM Linux 本机构建；资源不足时再使用交叉编译。
- 安装同一份最小配置和 systemd unit。
- 验证重启、自启动、端口、API 和本机推拉流。

**修改文件**

- `docs/guides/build-and-testing/cross_platform_build.md`：增加 SRS 部署边界和
  “交叉构建不等于实机通过”。

**新增文件**

- `scripts/srs/build_srs_arm64.sh`
- `scripts/srs/verify_srs_chain.sh`
- `deploy/srs/systemd/rtmp-monitor-srs.service`

**核心接口**

Shell 脚本参数至少包含：

```text
--source-dir --prefix --config --mode native|cross --cross-prefix
```

**测试方法**

```text
build: native ./configure && make，或官方 cross-build 参数
run:   systemctl start rtmp-monitor-srs
test:  file/ldd + 1935 + API + FFmpeg push + ffprobe/ffplay pull + reboot
```

**完成标准**

- 真实目标板报告 `uname -m`、SRS version、ELF、动态依赖和运行结果。
- systemd 重启后自动恢复，journal 无循环崩溃。
- 摄像头/另一台客户端可通过目标板 LAN IP 访问 1935。
- 交叉编译结果不得替代实机结果。

### Phase 3：FFmpeg 与真实摄像头推流到 SRS

**要做什么**

- 先用官方 `source.flv` 验证；再用真实嵌入式摄像头推到固定 streamKey。
- 固定一层 app=`live`，每摄像头唯一 streamKey。
- 记录编码格式、分辨率、帧率、GOP；第一版以现有播放器支持的 H.264 为准。

**修改文件**

- `scripts/srs/verify_srs_chain.ps1`
- `scripts/srs/verify_srs_chain.sh`
- `docs/guides/build-and-testing/rtmp_chain_verification.md`

**新增文件**

- 无新的 C++ 文件。
- 若摄像头厂商需要专门说明，只新增脱敏的
  `docs/guides/build-and-testing/camera_to_srs.md`，不得保存鉴权 URL。

**核心接口**

```text
cameraId -> streamKey -> rtmp://<SRS-IP>:1935/live/<streamKey>
```

**测试方法**

- Build：重新构建当前 Qt 客户端基线，确认摄像头接入前无回归；脚本执行语法/SelfTest。
- Run：启动 SRS，再分别启动官方素材 FFmpeg publisher 与真实摄像头 publisher。
- Test：

- FFmpeg publisher 运行至少 10 分钟；ffprobe/ffplay 拉流。
- 停止 publisher，再恢复同一 URL；确认 SRS 流状态和播放恢复。
- 从真实摄像头所在网段测试，不用 localhost 代替。

**完成标准**

- `camera01` 在 API 中 publish active，ffplay 连续有画面。
- 停推后消失/变 inactive，恢复后同一 URL 再次可拉。
- H.264 profile/level 与现有 FFmpeg 解码器兼容；异常项标 `[需要验证]`。

### Phase 4：现有 Qt 客户端从 SRS 拉流

**要做什么**

- 不改播放器和 renderer，使用现有 `--url`/连接对话框直接拉 SRS。
- 验证状态、第一帧、OpenGL active backend、断推重连和退出。

**修改文件**

- 原则上无源文件修改。
- 验证通过后仅更新 `README.md` 与 RTMP 链路指南的实际命令/结果。

**新增文件**

- 无。

**核心接口**

```text
rtmp_monitor --url rtmp://127.0.0.1:1935/live/camera01
```

**测试方法**

```text
build: cmake --build <windows-debug-build>
run:   rtmp_monitor --renderer=auto --url <SRS URL>
test:  full CTest + 单路真实播放 + 停推/恢复 + 正常退出
```

**完成标准**

- 状态进入 Playing、画面真实更新、active renderer 与预期一致。
- 停推后清旧帧并重连；恢复后 15 秒内重新 Playing。
- 程序退出后无 Qt/FFmpeg 网络线程残留。
- 不修改 `FFmpegPlayer`、OpenGL、邮箱或网格架构。

### Phase 5：Server 配置与健康生命周期接入 Qt

**要做什么**

- 增加 Server endpoint 读取、URL builder 和异步健康监控。
- 让连接对话框默认 URL 使用 endpoint，但保留完整 URL 手工覆盖。
- 将 Server 状态写入现有系统日志；UI 只在状态变化时提示。
- 不让 Qt 启停 SRS。

**修改文件**

- `CMakeLists.txt`：增加 Qt6 Network 和 server 静态库/测试目标。
- `src/main.cpp`：加载配置、组合 monitor、连接日志/UI 信号。
- `include/common/app/StreamConnectionController.h`
- `src/common/app/StreamConnectionController.cpp`：只调整默认 URL 生成与 profile 绑定。
- `include/common/ui/MainWindow.h`、`src/common/ui/MainWindow.cpp`：如确有必要，增加低频
  Server 状态显示；不得影响视频状态机。

**新增文件**

- `include/common/server/MediaServerTypes.h`
- `include/common/server/MediaServerConfiguration.h`
- `src/common/server/MediaServerConfiguration.cpp`
- `include/common/server/RtmpUrlBuilder.h`
- `src/common/server/RtmpUrlBuilder.cpp`
- `include/common/server/MediaServerMonitor.h`
- `src/common/server/MediaServerMonitor.cpp`
- `deploy/srs/media-server.example.ini`
- `tests/MediaServerConfigurationTest.cpp`
- `tests/RtmpUrlBuilderTest.cpp`
- `tests/MediaServerMonitorTest.cpp`

**核心接口**

采用第 9.2 节接口。禁止增加 `startSrs()`、`killSrs()` 或 Docker/systemctl 调用。

**测试方法**

- Build：Windows Debug 构建新增 server library/test；ARM64 RASTER/GLES3 两套交叉构建。
- Run：用真实 SRS 和 `media-server.ini` 启动 Qt 客户端，保持现有 `--url` 对照组。
- Test：

- 纯测试：默认值、非法 host/port/app/streamKey、IPv4/IPv6 URL、配置覆盖、脱敏。
- 假 Server 测试：TCP open、API 200/code 0、API invalid JSON、超时、抖动防抖。
- 集成：真实 SRS 健康/停止/恢复，播放器独立重连。
- Windows 完整 CTest；ARM64 双构建与纯逻辑测试。

**完成标准**

- 同一套 C++ 代码在 Windows x64 和 ARM Linux 编译。
- API 探测不阻塞 GUI；2 秒轮询不刷日志。
- Server monitor 故障不停止现有播放器；播放器也不依赖 API 才能播放。
- 现有 `--url`、连接对话框和所有 renderer 回归通过。

### Phase 6：多摄像头配置接入

**要做什么**

- 使用 `CameraStreamProfile` 配置 1～16 路 cameraId/displayName/streamKey。
- 由 URL builder 生成各路 URL，交给现有 controller/manager。
- 检查重复、上限和单路故障隔离。

**修改文件**

- `MediaServerConfiguration.*`
- `StreamConnectionController.*`
- `src/main.cpp`
- 相关 controller/manager 测试，仅增加 profile 场景，不重写原测试。

**新增文件**

- 如 `MediaServerConfiguration` 过长，才拆分：
  `include/common/app/CameraProfileStore.h`、
  `src/common/app/CameraProfileStore.cpp`、
  `tests/CameraProfileStoreTest.cpp`。
- 不满足拆分条件时不要机械创建这些文件。

**核心接口**

```text
QList<CameraStreamProfile> loadCameraProfiles(...)
StreamId addConnection(profile, endpoint, startImmediately)
```

现有 `addConnection(displayName, rtmpUrl, ...)` 保留兼容。

**测试方法**

- Build：构建主程序及新增/扩展的 profile、controller、manager 测试目标。
- Run：启动同一 SRS，依次运行 1、4、16 路 profile 配置。
- Test：

- 1、4、16 路 profile；重复 cameraId、重复 streamKey、非法 key、17 路拒绝。
- 真实 SRS 4 路先验收，再执行 16 路既有自动测试。
- 停止 camera03，其他路继续；恢复后 camera03 自动 Playing。

**完成标准**

- profile 到 StreamId/Player 的映射稳定，不依赖网格位置。
- 16 路 URL 唯一且不泄漏 API 凭据。
- 现有多路性能、日志、渲染和退出门禁不退化。

### Phase 7：异常恢复与发布门禁

**要做什么**

- 覆盖 SRS 崩溃、重启、1935 冲突、API 故障、网络中断、publisher 中断和应用退出。
- 固化脚本所有权、错误报告和恢复时间。
- 评估非零重试上限下 Server 恢复是否需要自动 restartStream；默认无限重试无需新增逻辑。

**修改文件**

- SRS Windows/ARM 验证脚本。
- `MediaServerMonitor.*` 及测试。
- 仅当测试证明必要时，最小修改 `StreamConnectionController.*` 处理
  Healthy 上升沿；不得批量重建 manager/player。
- 更新 `docs/memory/known_issues.md` 和发布验收文档。

**新增文件**

- `docs/guides/build-and-testing/srs_failure_recovery.md`
- 故障恢复复验以现有 SRS 生命周期/链路脚本和
  `docs/guides/build-and-testing/srs_failure_recovery.md` 的步骤为准；如未来需要
  常驻故障注入入口，再单独设计并提交，当前不虚构脚本路径。

**核心接口**

不新增进程控制接口。恢复策略：

```text
default infinite retry -> rely on FFmpegPlayer
retry limit reached    -> manual reconnect by default
optional auto restart  -> only affected stopped streams, once per Healthy edge
```

**测试方法**

- Build：重新构建主程序、monitor/controller 相关测试和故障脚本 SelfTest。
- Run：由故障测试脚本启动其自有 SRS、publisher 和 Qt 客户端，并记录所有权状态。
- Test：

1. SRS 正常、1/4/16 路 Playing。
2. 精确停止自有 SRS；观察所有流清帧并独立 Reconnecting。
3. 恢复 SRS；默认配置下所有流 15 秒内 Playing。
4. 让未知 listener 占用 1935；启动脚本必须拒绝且不杀未知进程。
5. 仅关闭 1985；RTMP 继续播放，monitor 为 Degraded。
6. 关闭单个 publisher；其他流不受影响。
7. 应用退出；网络线程、monitor reply/timer 全部释放。

**完成标准**

- 所有故障均有结构化、脱敏、有限频率日志。
- 未误杀、不接管外部进程；端口冲突返回非零。
- SRS 恢复不要求重启 Qt 应用。
- Windows 完整 CTest、真实流测试、ARM64 构建和 ARM 真机恢复测试全部有结果。

## 13. Kimi K3 强制工作流与汇报模板

每个 Phase 必须单独完成，不得跨 Phase 批量改代码。执行顺序固定为：

```text
1. 检查 git status 和实际文件
2. 只实施当前 Phase
3. build
4. run
5. test
6. 检查日志、进程、端口和残留
7. 汇报结果
8. 等待确认
9. 确认成功后才进入下一 Phase
```

汇报必须使用：

```markdown
## Phase N 结果

- Git 基线：<commit>；实施前是否干净：<yes/no>
- SRS：<version/tag/commit or image digest>
- 平台：<Windows+WSL2 / Docker / ARM model+OS>
- 修改文件：<list>
- 新增文件：<list>
- Build：<exact command> -> <pass/fail>
- Run：<exact command> -> <pass/fail>
- Test：<exact command and counts> -> <pass/fail>
- 端口/API：1935=<...>, 1985=<...>, version=<...>
- 推流：<source/app/stream/codec; no secrets>
- 拉流：<ffprobe/ffplay/RtmpMonitor result>
- 异常注入：<what/result>
- 未解决：[需要验证] <list>
- 日志与产物：<relative paths only>
- 是否满足完成标准：<yes/no with reasons>
```

禁止用“应该可以”“理论支持”代替命令终态。超时、交叉编译和短时烟测必须如实标注，
不能扩大为真机、长期稳定或产品发布结论。

## 14. 第一版禁止事项

- 不修改 SRS 核心源码，不维护私有 SRS fork。
- 不魔改 RTMP 协议，不自研 RTMP Server。
- 不做 SRS 内核、协程、内存池、转发或协议优化。
- 不重写 `FFmpegPlayer`、`MultiStreamPlaybackManager`、邮箱或 OpenGL。
- 不引入 WebRTC、HLS、SRT、GB28181、DVR、转码、Kubernetes 或集群。
- 不做公网部署、复杂鉴权、Token 回调或多租户。
- 不在 GUI 内嵌 HTTP Callback Server。
- 不让 Qt 批量杀进程、管理未知容器或执行 `systemctl`。
- 不创建职责重复的 `SrsManager`、`StreamManager` 或无实际平台差异的 runtime 类层次。
- 不把真实摄像头鉴权 URL、Token、密码、私网拓扑、个人绝对路径或原始日志写入仓库。

## 15. 当前 `[需要验证]` 清单

1. WSL2 mirrored networking 下，真实摄像头从 LAN 访问 Windows 主机 TCP 1935 是否能
   直接进入当前发行版；必须由外部设备验证。
2. `v6.0-r0` 在当前 Ubuntu 22.04 与目标 ARM 镜像上的实际最小构建依赖。
3. 固定 tag 对 `http_api.listen 127.0.0.1:1985` 的实际解析和 Windows localhost 转发。
4. 目标 ARM 板 glibc/musl、动态加载器、sysroot、systemd、存储和服务用户权限。
5. `ossrs/srs:6.0.184` manifest 在实际 registry 中是否包含目标 `linux/arm64`。
6. 真实摄像头输出的 H.264 profile/level、GOP、时间戳和音频格式是否与现有播放器兼容。
7. SRS `/api/v1/streams` 在 6.0.184 的精确 JSON 字段路径。
8. Docker Desktop PowerShell bind mount 与容器命令的最终转义形式，必须由脚本 SelfTest
   固化。
9. 非零 `--max-reconnect-failures` 场景在 Server 恢复后是否值得自动 restart；默认
   0（无限重试）不存在该需求。

这些项目没有验证前不得写入 `docs/memory/project_snapshot.md` 作为已完成事实。

## 16. 最终验收定义

第一版只有同时满足以下条件才算完成：

```text
Windows WSL2 SRS stable build/run
AND ARM Linux real-device SRS build/run
AND FFmpeg synthetic publish/play
AND real camera publish
AND existing Qt client pull
AND existing decoder/OpenGL unchanged
AND 1/4/16 stream mapping verified
AND SRS stop/restart recovery verified
AND unknown 1935 listener is never killed
AND full Windows CTest passes
AND ARM64 build plus real-device checks pass
```

在此之前，项目状态只能写为“SRS 集成 Phase N 已完成，其余待验证”，不得写成
“Server 产品化完成”。
