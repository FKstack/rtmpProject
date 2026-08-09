# SRS 新手完全指南：概念、使用、配置与项目联动

> 文档分类：构建与验证。面向第一次接触 SRS 的读者。
>
> 核心 RTMP 1935、HTTP API 1985、推拉流和故障恢复命令已在本机
> （Windows 11 + WSL2 Ubuntu 22.04）实际执行，对应记录见
> `docs/weeks/week7/week7_srs_server_integration.md`。第 2.6 节网页预览是按
> SRS 6.0.184 官方配置与固定 tag 源码核对后新增的可选教程；当前仓库默认
> 未启用 8080，现场运行结果明确标为 `[需要验证]`。

## 目录

1. [SRS 是什么](#1-srs-是什么)
2. [我要如何使用 SRS](#2-我要如何使用-srs)
3. [我要如何配置 SRS](#3-我要如何配置-srs)
4. [我要如何让当前项目跟 SRS 联动](#4-我要如何让当前项目跟-srs-联动)
5. [常见问题 FAQ](#5-常见问题-faq)

---

## 1. SRS 是什么

### 1.1 一句话解释

SRS（Simple Realtime Server）是一个**开源的流媒体服务器**。可以把它想象成
一个"视频快递中转站"：

```text
摄像头/推流端  ──把视频“寄”到中转站──▶  SRS  ──从中转站“取”视频──▶  播放器/客户端
   (publish 推流)                        (收流+转发)                    (play 拉流)
```

没有中转站时，播放器必须直接连到摄像头，一个摄像头通常只能被少数客户端
连接；有了 SRS，摄像头只需要推一路流到 SRS，任意多个播放器都可以从 SRS
拉取同一路画面。

### 1.2 本项目为什么需要 SRS

RtmpMonitor 是一个多路监控客户端：它本身**不产生视频，只负责拉流、解码
和显示**。摄像头（或测试用的 FFmpeg）把 H.264 视频通过 RTMP 协议推给
SRS，RtmpMonitor 再从 SRS 把流拉回来：

```text
Camera / FFmpeg publisher
    -> RTMP Push（推流）
SRS 6.0.184（本项目固定版本）
    -> RTMP Pull（拉流）
RtmpMonitor（FFmpegPlayer 解码 -> 邮箱 -> OpenGL 渲染 -> 多宫格显示）
```

### 1.3 关键概念（先记住这 5 个词）

| 概念 | 含义 | 本项目的值 |
|---|---|---|
| **RTMP** | 推拉流用的协议（Adobe 系，成熟稳定）。推和拉用**同一个地址**，由协议命令区分 | 全程使用 |
| **application（app）** | 流的"分类目录"，一层名字 | 固定为 `live` |
| **streamKey** | 每一路流的名字（在 app 下唯一） | `camera01`、`camera02`…… |
| **RTMP URL** | 流的完整地址：`rtmp://<主机>:<端口>/<app>/<streamKey>` | `rtmp://127.0.0.1:1935/live/camera01` |
| **HTTP API** | SRS 自带的只读查询接口（JSON），用来确认"服务器活着没、是 SRS 吗、有几路流" | `http://127.0.0.1:1985/api/v1/...` |

两个端口不要混淆：

- **1935**：RTMP 推拉流端口（干活的端口，摄像头和播放器都连它）。
- **1985**：HTTP API 端口（体检端口，只绑定本机回环 `127.0.0.1`，
  不对局域网开放）。

### 1.4 版本与形态（本项目已经定好的事）

- 版本固定为 **SRS 6.0.184**，Git tag `v6.0-r0`，这是 SRS 官方 6.x 稳定版。
  不跟随 develop 分支，不用 SRS 7/8 开发线。
- SRS 官方历史上提供过 Cygwin/Windows 路径，但它不是本项目可依赖的原生 MSVC
  正式基线。本项目的工程选择是：Windows 开发机在 WSL2 Ubuntu 中源码编译运行
  SRS，不修改 SRS 核心，也不维护 Cygwin 分支。
- ARM Linux 产品环境：在目标设备上本机编译，由 systemd 管理（脚本已备好，
  实机尚未验证，见第 2.8 节）。
- Docker 是备选方案（快速烟测/CI），本指南以 WSL2 为主线。
- 历史上的 nginx-rtmp 脚本保留未删（`scripts/verify_rtmp_chain.ps1`），
  新工作一律用 SRS。

### 1.5 SRS 在本项目中的边界（重要）

- SRS 是**独立基础设施**，不是 Qt 客户端的一部分。
- 开发期由脚本管理（本文第 2 节），产品期由 systemd 管理。
- **Qt 客户端只"观察" SRS 的健康状态，不负责启动或停止它**；即使 SRS
  挂了，客户端也只是自动重连，不会去杀进程或重启服务器。

---

## 2. 我要如何使用 SRS

### 2.1 前置条件自查（30 秒）

在仓库根目录执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\srs\srs_dev_wsl.ps1 -Action Check
```

它会检查并逐行报告：

- WSL2 发行版 `Ubuntu-22.04-New` 是否存在；
- 仓库内的 SRS 配置文件 `deploy/srs/conf/srs-minimal.conf` 是否存在；
- WSL 里有没有已编译的 SRS 二进制（期望显示 `SRS binary: 6.0.184`）；
- WSL 里 ffmpeg/ffprobe/curl 是否可用；
- 测试素材 `source.flv` 是否存在；
- 1935/1985 端口当前是否被占用、有没有脚本管理的运行状态。

如果显示 `SRS binary: MISSING`，说明还没编译，先看第 2.2 节；如果一切
正常，直接跳到第 2.3 节日常使用。

### 2.2 第一次：在 WSL2 里编译 SRS（约 10～20 分钟）

> 已在 2026-08-09 完成的机器可跳过本节。以下为完整复现步骤。

进入 WSL（开始菜单搜 "Ubuntu" 或在终端执行 `wsl -d Ubuntu-22.04-New`）：

```bash
# 1. 安装编译依赖（有 sudo 时）
sudo apt-get update
sudo apt-get install -y ca-certificates git gcc g++ make patch unzip perl curl jq ffmpeg tclsh

# 2. 拉取固定版本的源码（tag v6.0-r0）
mkdir -p "$HOME/src" && cd "$HOME/src"
git clone --branch v6.0-r0 --depth 1 https://github.com/ossrs/srs.git srs-6.0.184
cd srs-6.0.184
git rev-parse HEAD        # 必须输出 9f8670b2a832aea04abe644af47261838838c49a

# 3. 编译并安装到用户目录
cd "$HOME/src/srs-6.0.184/trunk"
./configure --prefix="$HOME/opt/srs-6.0.184"
make -j"$(nproc)"
make install

# 4. 验证
"$HOME/opt/srs-6.0.184/objs/srs" -v     # 输出 6.0.184
```

**没有 sudo 怎么办（本机实际情况）**：

- `apt` 里找不到 tclsh 且没有密码 sudo 时，可以用户态解包（本机就是这么
  做的）：

```bash
mkdir -p "$HOME/local/debs" && cd "$HOME/local/debs"
curl -sfO https://archive.ubuntu.com/ubuntu/pool/main/t/tcl8.6/tcl8.6_8.6.12+dfsg-1build1_amd64.deb
curl -sfO https://archive.ubuntu.com/ubuntu/pool/main/t/tcl8.6/libtcl8.6_8.6.12+dfsg-1build1_amd64.deb
dpkg -x libtcl8.6_8.6.12+dfsg-1build1_amd64.deb "$HOME/local"
dpkg -x tcl8.6_8.6.12+dfsg-1build1_amd64.deb "$HOME/local"
mkdir -p "$HOME/local/bin"
ln -sf "$HOME/local/usr/bin/tclsh8.6" "$HOME/local/bin/tclsh"
```

- SRS 源码里 vendored 的 SRT 编译脚本写的是 `#!/usr/bin/tclsh`（绝对路径）。
  用户态 tclsh 不在那个位置，需要把 `~/src/srs-6.0.184/trunk/3rdparty/
  srt-1-fit/` 下这几个脚本的第一行改成 `#!/usr/bin/env tclsh`：

```bash
grep -rl '#!/usr/bin/tclsh' "$HOME/src/srs-6.0.184/trunk/3rdparty/srt-1-fit/" \
  | while read -r f; do sed -i '1s|^#!/usr/bin/tclsh$|#!/usr/bin/env tclsh|' "$f"; done
```

  注意：这只改 **WSL 里 `~/src` 下的 SRS 源树**（不属于本仓库、不影响
  SRS 功能、不构成私有 fork），仓库里不保存任何修改过的 SRS 文件。

- 编译时带上用户态工具：

```bash
export PATH="$HOME/local/bin:$PATH"
export LD_LIBRARY_PATH="$HOME/local/usr/lib/x86_64-linux-gnu"
export TCL_LIBRARY="$HOME/local/usr/share/tcltk/tcl8.6"
./configure --prefix="$HOME/opt/srs-6.0.184" && make -j"$(nproc)" && make install
```

### 2.3 日常使用：五个动作

所有动作都由同一个脚本完成（在仓库根目录、PowerShell 中执行，
每个示例都写成可以直接复制的完整命令）：

| 动作 | 命令 | 干什么 |
|---|---|---|
| 检查 | `... -Action Check` | 只读检查环境，不启动任何东西 |
| 启动 | `... -Action Start` | 安装仓库配置并在 WSL 后台启动 SRS |
| 状态 | `... -Action Status` | 查看运行状态、端口、API、流列表 |
| 自测 | `... -Action Test` | 自动推一路流→验证→停推（约 20 秒） |
| 停止 | `... -Action Stop` | 只停脚本自己启动的 SRS |

完整命令示例（启动）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\srs\srs_dev_wsl.ps1 -Action Start
```

启动成功的输出类似：

```text
[srs-dev-wsl] Installing config from /mnt/e/rtmpProject/deploy/srs/conf/srs-minimal.conf
[srs-dev-wsl] Starting SRS (RunId=1a180701) ...
[srs-dev-wsl] SRS ready. PID=48017 API={"code":0,...,"version":"6.0.184"}
```

> 如果启动时报 `TCP 1935 is already listening and is NOT owned by this
> script`（退出码 3）：说明 1935 被别的程序占用。脚本**故意拒绝启动、也
> 绝不帮你杀进程**。请按输出里的 PID/进程名人工确认占用者（可能是旧
> SRS、nginx、Docker），处理好再来。这是安全设计，不是故障。

### 2.4 验证 SRS 真的活着（三种方式，任选）

方式一：脚本自测（最省事）——自动推流、拉流、停流一条龙：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\srs\srs_dev_wsl.ps1 -Action Test
# 看到 [srs-dev-wsl] TEST PASS 即通过
```

方式二：浏览器/ curl 访问健康 API：

```powershell
Invoke-RestMethod http://127.0.0.1:1985/api/v1/versions
# 期望：code=0，data.version = 6.0.184
```

方式三：端到端验收脚本（WSL 和 Windows 两侧都验证，含停推/恢复）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\srs\verify_srs_chain.ps1 -Action Verify
# 全部 [verify-srs] PASS，最后 VERIFY PASS；报告写入 out\srs\

# 加码：10 分钟推流保活（正式验收门槛）
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\srs\verify_srs_chain.ps1 -Action Verify -SoakSeconds 600
```

### 2.5 自己动手推流、拉流（理解原理）

**链路烟测**（SRS 自带素材只用于确认推拉流是否跑通）：

```bash
# 在 WSL/Bash 终端执行。Bash 的续行符是反斜杠，且后面不能有空格。
ffmpeg -re -stream_loop -1 \
  -i "$HOME/src/srs-6.0.184/trunk/doc/source.flv" \
  -c copy -f flv rtmp://127.0.0.1:1935/live/camera01
```

参数白话解释：`-re` 按正常速度读、`-stream_loop -1` 无限循环、`-c copy`
不解码直接转发、`-f flv` 用 FLV 封装推到后面的 RTMP 地址。

> 该 `source.flv` 实测只有 768×320、25 FPS、视频码率约 212 kbps，并且素材
> 本身存在较长的重复/静止画面。它适合做链路烟测，不适合评价 RtmpMonitor 的
> 清晰度和流畅度；`-c copy` 只保留源质量，不会把低清素材变清晰。

**高清清晰度/流畅度测试**（WSL/Bash）：

```bash
ffmpeg -re \
  -f lavfi -i "testsrc2=size=1920x1080:rate=30" \
  -an \
  -c:v libx264 \
  -preset veryfast \
  -tune zerolatency \
  -pix_fmt yuv420p \
  -b:v 6M \
  -maxrate 6M \
  -bufsize 12M \
  -g 60 \
  -keyint_min 60 \
  -sc_threshold 0 \
  -f flv \
  rtmp://127.0.0.1:1935/live/camera01
```

Windows PowerShell 执行同一测试时，续行符必须改为反引号：

```powershell
ffmpeg -re `
  -f lavfi -i "testsrc2=size=1920x1080:rate=30" `
  -an `
  -c:v libx264 `
  -preset veryfast `
  -tune zerolatency `
  -pix_fmt yuv420p `
  -b:v 6M `
  -maxrate 6M `
  -bufsize 12M `
  -g 60 `
  -keyint_min 60 `
  -sc_threshold 0 `
  -f flv `
  rtmp://127.0.0.1:1935/live/camera01
```

用自己的 mp4（仓库里 `testdata/test.mp4` 或任意 H.264 视频）：

```powershell
ffmpeg -re -stream_loop -1 -i test.mp4 `
  -map 0:v:0 -an -c:v libx264 -preset veryfast -tune zerolatency `
  -pix_fmt yuv420p -g 50 -keyint_min 50 -sc_threshold 0 `
  -f flv rtmp://127.0.0.1:1935/live/camera01
```

**拉流验证**：

```powershell
# 看流信息（应出现 h264 视频流）
ffprobe -v error -show_entries stream=index,codec_type,codec_name,width,height,avg_frame_rate,bit_rate `
  -of json rtmp://127.0.0.1:1935/live/camera01

# 看画面（会弹出播放器窗口）
ffplay -fflags nobuffer -flags low_delay -framedrop rtmp://127.0.0.1:1935/live/camera01

# 不弹窗、只验证能否连续解码 45 帧（自动化里用的方法）
ffmpeg -i rtmp://127.0.0.1:1935/live/camera01 -frames:v 45 -f null -
```

画质或卡顿问题按同一地址做 A/B：

| ffplay | RtmpMonitor | 初步判断 |
| --- | --- | --- |
| 清晰、流畅 | 清晰、流畅 | 链路和客户端基本正常 |
| 清晰、流畅 | 模糊或卡顿 | 优先检查客户端解码、帧邮箱和 Renderer |
| 同样模糊或卡顿 | 同样模糊或卡顿 | 优先检查源素材、推流参数、SRS 或网络 |

不要用 ScreenSketch 录屏码率代替直播源参数。先用 `ffprobe` 确认拉到的实际
分辨率、帧率和编码，再比较 ffplay 与客户端。

**查看 SRS 当前有几路流**：

```powershell
Invoke-RestMethod "http://127.0.0.1:1985/api/v1/streams/?start=0&count=100"
```

> 注意两个坑（都踩过）：`/api/v1/streams` 不带尾斜杠会返回 301 跳转；
> 接口**默认只返回 10 条**（分页参数 start/count），多流时必须显式
> `?start=0&count=100`，否则会误以为"丢了 6 路流"。

### 2.6 可选：用 SRS 自带网页查看推流画面

#### 2.6.1 先理解：网页不是直接播放 RTMP

SRS 有网页工具，但需要把两件事分开：

- `http://127.0.0.1:8080/console/` 是 **SRS Console**，用于查看服务器、
  Vhost、流和客户端等运行信息；它不是本项目的正式监控客户端。当前配置的
  1985 API 禁止跨域，所以只开启 8080 时 Console 静态页面能打开，但数据请求
  可能被浏览器拦截，处理方法见 2.6.3。
- `http://127.0.0.1:8080/players/srs_player.html` 是 SRS 随源码提供的
  **HTTP-FLV 示例播放器**，可以在 Edge/Chrome 中查看一路实时画面。

现代浏览器不能直接打开 `rtmp://...`。这里实际走的是：

```text
FFmpeg/摄像头 --RTMP--> SRS --HTTP-FLV--> SRS Player 网页
                              |
                              +--> RtmpMonitor 仍然通过 RTMP 拉流
```

也就是说，开启网页预览不会改变摄像头的推流 URL，也不会改变当前 Qt 客户端
的播放链路；SRS 只是为同一路流额外挂载一个 `.flv` HTTP 地址。HTTP-FLV
通常比 HLS 延迟低，SRS 官方文档给出的常见延迟约为 3～5 秒，但实际结果仍受
GOP、浏览器、网络和缓存影响。

> 当前仓库默认**没有**启用网页服务。这是有意的：第一阶段只需要 RTMP
> 1935 和回环 HTTP API 1985。下面的 8080/HTTP-FLV 仅用于本机开发观察，
> 不是 Qt 客户端的依赖，也不是生产管理平台。配置字段、官方页面路径和参数
> 已按 SRS 6.0.184 核对；尚未在当前默认配置上实际启用，运行结果
> `[需要验证]`。

#### 2.6.2 开启本机网页预览

编辑仓库权威配置 `deploy/srs/conf/srs-minimal.conf`。在 `http_api` 块之后、
`vhost` 块之前加入：

```conf
# 可选的本机开发网页：静态 Console、示例 Player 和 HTTP-FLV。
http_server {
    enabled             on;
    listen              127.0.0.1:8080;
    dir                 ./objs/nginx/html;
    crossdomain         off;
}
```

然后在现有 `vhost __defaultVhost__` 内加入 `http_remux`：

```conf
vhost __defaultVhost__ {
    enabled             on;

    http_remux {
        enabled         on;
        mount           [vhost]/[app]/[stream].flv;
    }
}
```

完整含义：

- `listen 127.0.0.1:8080`：只允许本机访问网页，避免把无鉴权的示例页面
  暴露到局域网；这与 RTMP 的 1935 全网卡监听不是一回事。
- `dir ./objs/nginx/html`：SRS 安装目录里的官方 Console、Player 等静态文件。
  项目脚本会先 `cd` 到 SRS 安装目录再启动，因此该相对路径有效。
- `http_remux`：收到 `rtmp://host/live/camera01` 后，按 mount 规则生成
  `http://host:8080/live/camera01.flv`，不重新编码视频。
- `crossdomain off`：官方 Player 与 FLV 同源，当前用不到跨域。如果以后从
  另一个网站加载 FLV，需要单独评审 CORS、HTTPS、鉴权和来源限制。

配置改完必须重启 SRS，不能只刷新浏览器：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\srs\srs_dev_wsl.ps1 -Action Stop

powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\srs\srs_dev_wsl.ps1 -Action Start
```

确认 8080 已监听：

```powershell
Test-NetConnection 127.0.0.1 -Port 8080
# 期望：TcpTestSucceeded = True

Invoke-WebRequest http://127.0.0.1:8080/players/srs_player.html `
  -UseBasicParsing | Select-Object StatusCode
# 期望：StatusCode = 200
```

如果 8080 已被未知程序占用，应先只读确认占用者；不要结束身份不明的进程，
也不要为了省事改成随机端口后忘记同步 Player URL。

#### 2.6.3 推流并在网页中播放

先按 2.5 推送测试流：

```powershell
ffmpeg -re -stream_loop -1 -i test.mp4 `
  -map 0:v:0 -an -c:v libx264 -preset veryfast -tune zerolatency `
  -pix_fmt yuv420p -g 50 -keyint_min 50 -sc_threshold 0 `
  -f flv rtmp://127.0.0.1:1935/live/camera01
```

先用 FFprobe 验证 HTTP-FLV 本身，避免把“浏览器问题”和“SRS 没有流”混在
一起：

```powershell
ffprobe -v error `
  -show_entries stream=index,codec_type,codec_name,width,height `
  -of json http://127.0.0.1:8080/live/camera01.flv
```

然后在 Windows Edge 或 Chrome 打开以下完整地址：

```text
http://127.0.0.1:8080/players/srs_player.html?schema=http&server=127.0.0.1&port=8080&app=live&stream=camera01.flv&autostart=true
```

参数逐个解释：

| 参数 | 当前值 | 含义 |
|---|---|---|
| `schema` | `http` | 网页拉流协议，不是摄像头推流协议 |
| `server` | `127.0.0.1` | 浏览器看到的 SRS 地址 |
| `port` | `8080` | `http_server` 端口，不是 RTMP 1935 或 API 1985 |
| `app` | `live` | 与 RTMP URL 的 `/live/` 一致 |
| `stream` | `camera01.flv` | 流名加 HTTP-FLV 的 `.flv` 后缀 |
| `autostart` | `true` | 页面加载后尝试自动播放 |

浏览器可能因自动播放策略不允许立即播放有声视频；这时点击页面里的播放按钮
即可，不能据此判断推流失败。当前教程使用 `-an` 只推视频，不受有声自动播放
限制。

还可以打开：

```text
http://127.0.0.1:8080/console/
```

如果 Console 页面能打开但没有服务器/流数据，而浏览器开发者工具显示访问
`http://127.0.0.1:1985` 的 CORS 错误，可在**仅绑定回环地址的本机开发环境**
中，把现有 `http_api` 块的这一行临时改为：

```conf
http_api {
    enabled             on;
    listen              127.0.0.1:1985;
    crossdomain         on;   # 仅供本机 8080 Console 调用 1985
    # raw_api 仍保持 off
}
```

改后执行 Stop→Start。`listen 127.0.0.1:1985` 和 `raw_api off` 必须保持不变；
不使用 Console 时把 `crossdomain` 恢复为 `off`。**只看视频的 SRS Player 不
需要修改 1985 的 CORS**，因为 Player 和 `.flv` 都由同一个 8080 服务提供。

Console 适合确认“流是否存在、是否有客户端连接”，最终画面、方向、颜色、
截图和全屏交互仍必须在本项目的 `RtmpMonitor` 中验收。不要把 SRS Console
当成产品监控 UI，也不要让 Qt 播放链依赖 Console 是否可用。

#### 2.6.4 常见故障

| 现象 | 优先检查 |
|---|---|
| 8080 连不上 | 是否保存了仓库配置并执行 Stop→Start；WSL 中执行 `ss -Hltn '( sport = :8080 )'`；检查 SRS 日志是否端口冲突 |
| Player 页面 404 | `http_server.dir` 是否为 `./objs/nginx/html`；SRS 安装目录中是否存在 `objs/nginx/html/players/srs_player.html` |
| 页面能开但一直黑屏 | API 流列表是否存在 camera01；FFprobe 能否读取 `.flv`；`app/stream` 是否完全一致；等待下一个 H.264 关键帧 |
| RTMP 能播、HTTP-FLV 不能读 | 是否漏加 `http_remux`；URL 是否包含 `.flv`；查看 SRS 日志中的 HTTP stream/remux 错误 |
| 页面提示不支持 | 优先使用最新 Edge/Chrome；HTTP-FLV 依赖浏览器 MSE/JavaScript，并不是原生 RTMP；iOS Safari 不属于本教程验收范围 |
| 视频有、音频行为异常 | 本阶段建议先 `-an` 排除音频；只有确认所有流都无音频时才评估 `http_remux.has_audio off`，不要对未来带音频摄像头盲目全局关闭 |

#### 2.6.5 安全边界与关闭方法

- 本机开发保持 `127.0.0.1:8080`，不要直接改成 `8080`/`0.0.0.0:8080`
  暴露给局域网。SRS 官方说明，HTTP API 的 Basic Auth 并不会自动保护普通
  HTTP 静态服务和所有媒体接口。
- 确需让另一台电脑看网页时，应另外评审防火墙来源白名单、反向代理、HTTPS
  与鉴权；这不属于当前第一版。
- 网页预览只用于诊断，不要增加 HLS、WebRTC、Callback 或集群。
- 不再需要网页时，从 `srs-minimal.conf` 删除 `http_server` 和
  `http_remux` 两段，再执行 Stop→Start；RTMP 1935 和 API 1985 会继续工作。

官方依据：

- [SRS 6 HTTP-FLV](https://ossrs.io/lts/en-us/docs/v6/doc/flv)
- [SRS v6.0-r0 官方 HTTP-FLV 配置](https://github.com/ossrs/srs/blob/v6.0-r0/trunk/conf/http.flv.live.conf)
- [SRS Console 与 HTTP API 安全边界](https://ossrs.io/lts/en-us/blog/secure-your-http-api)

### 2.7 看日志、停止与清理

- SRS 运行日志在 WSL 的 `~/srs-run/srs-<RunId>.log`（`Status` 会显示当前
  RunId）。WSL 里查看：`tail -f ~/srs-run/srs-*.log`。
- 停止：`srs_dev_wsl.ps1 -Action Stop`。脚本在每次发信号前于同一个 WSL
  操作中核对 `/proc/<pid>/exe` 和命令行，先发 SIGQUIT；只有等待超时且身份
  再次匹配才发 SIGKILL，**绝不会停别人的进程**。停完确认 1935/1985 已释放。
- 所有权记录在 `out\srs\srs-dev-wsl.state.json`（被 Git 忽略）：RunId、
  `starting/ready` 状态、PID、可执行路径、配置路径、启动时间。

### 2.8 ARM Linux 设备上使用（脚本已备好，实机未验证）

在目标 ARM 板上（需要板子能访问）：

```bash
# 本机编译（官方推荐方式）
bash scripts/srs/build_srs_arm64.sh \
    --source-dir <srs-6.0.184-源码目录> \
    --prefix /opt/rtmp-monitor/srs-6.0.184 \
    --config deploy/srs/conf/srs-minimal.conf \
    --mode native

# 安装 systemd 服务（仓库已提供 unit 文件）
sudo install -m 0644 deploy/srs/systemd/rtmp-monitor-srs.service \
    /etc/systemd/system/rtmp-monitor-srs.service
sudo systemctl daemon-reload
sudo systemctl enable --now rtmp-monitor-srs.service

# 板子上做链验收
bash scripts/srs/verify_srs_chain.sh \
    --srs-home /opt/rtmp-monitor/srs-6.0.184 \
    --srs-source <srs-6.0.184-源码目录>
```

交叉编译（仅设备资源不足时，`--mode cross --cross-prefix aarch64-linux-gnu-`）
只证明能产出 AArch64 程序，**必须在真机上重新跑一遍验证**。
当前状态：交叉构建已通过，实机 `[需要验证]`（ISSUE-008）。

---

## 3. 我要如何配置 SRS

### 3.1 配置文件在哪里、怎么生效

- 仓库里的**唯一权威配置**：`deploy/srs/conf/srs-minimal.conf`（进 Git）。
- `srs_dev_wsl.ps1 -Action Start` 每次都会把它复制到 WSL 的
  `$HOME/opt/srs-6.0.184/conf/rtmp-monitor.conf` 再启动。
- 所以正确流程是：**改仓库里的文件 → 重新 `-Action Start`**。不要直接改
  WSL 里的副本（下次 Start 会被覆盖）。

### 3.2 逐行看懂当前配置

```conf
listen                  1935;        # RTMP 推拉流端口，对所有网卡监听
                                     # （本机、局域网摄像头/客户端都能连）
max_connections         100;         # 最大连接数上限（推+拉合计）

daemon                  off;         # 前台运行，便于脚本/日志管理
srs_log_tank            console;     # 日志输出到控制台（脚本再落盘到 ~/srs-run/）
srs_log_level_v2        info;        # 日志级别：info（排查时可改 trace）

http_api {                           # 只读健康查询接口
    enabled             on;
    listen              127.0.0.1:1985;   # 只绑定本机回环！局域网访问不到
    crossdomain         off;              # 不允许跨域（无浏览器直连需求）
    raw_api {
        enabled         off;         # RAW API 能改服务器状态，必须关
        allow_reload    off;         # 禁止通过 API 热重载配置
    }
}

vhost __defaultVhost__ {             # 默认虚拟主机（RTMP 必需）
    enabled             on;
}
```

### 3.3 设计意图（为什么不能乱改）

- **1985 只绑定回环**：API 能暴露服务器内部状态，永远不要把 1985 绑到
  `0.0.0.0` 或局域网地址。将来确需远程查询时，要同时加 Basic Auth 和
  防火墙来源限制，且密码不进仓库。
- **RAW API 保持关闭**：开了它，任何能访问 1985 的程序都能改 SRS 配置，
  本项目的客户端只做只读健康检查，用不到。
- **不启用多余功能**：默认配置里故意没有 `http_server`（HTTP 文件服务）和
  `http_remux`（HTTP-FLV）；2.6 只把它们作为本机开发时可开启、可关闭的
  网页诊断能力。默认同样没有
  `hls`、`rtc_server`（WebRTC）、`srt_server`、`dvr`（录制）、`transcode`
  （转码）、`cluster`（集群）、`http_hooks`（回调）。需要哪个，单独评审
  再加。
- `daemon off + console`：让 WSL 前台、脚本日志文件、systemd journal
  都能正确接管日志。

### 3.4 常见调整场景

| 想做的事 | 改法 | 注意 |
|---|---|---|
| 换 RTMP 端口 | `listen 1936;` | 只能用于隔离测试；摄像头、客户端、media-server.ini 的 `rtmpPort` 必须同步改，产品默认仍是 1935 |
| 更详细日志 | `srs_log_level_v2 trace;` | 日志量暴涨，排查完改回 info |
| 改 application 名 | 不用改 SRS 配置（vhost 通配）；推/拉 URL 的 app 段一起换即可，如 `rtmp://.../monitor/camera01` | 客户端 media-server.ini 的 `application` 同步改 |
| 让摄像头从局域网推流 | SRS 侧不用改（1935 已全网卡监听） | 需要在 Windows 防火墙为"专用网络"加 TCP 1935 入站规则；WSL mirrored 下 LAN 入站路径目前 `[需要验证]` |

改完配置的验证顺序：`-Action Stop` → `-Action Start` → `-Action Test`。

---

## 4. 我要如何让当前项目跟 SRS 联动

客户端联动有**三种方式**，从简单到自动化，按需选择。

### 4.1 方式一：`--url` 直接拉流（最快，30 秒跑通）

第 1 步：启动 SRS 并推一路流（见 2.3、2.5）。

第 2 步：用现有客户端直接拉：

```powershell
.\out\build-windows-x64\debug\rtmp_monitor.exe `
  --url rtmp://127.0.0.1:1935/live/camera01
```

多路就重复 `--url`（最多 16 次）：

```powershell
.\out\build-windows-x64\debug\rtmp_monitor.exe `
  --url rtmp://127.0.0.1:1935/live/camera01 `
  --url rtmp://127.0.0.1:1935/live/camera02
```

这条路不改任何配置，适合随手验证。**已实测**：状态进入 playing、OpenGL
渲染、停推自动重连、恢复后 15 秒内重回 playing、退出无残留。

### 4.2 方式二：连接对话框手工输入（交互式）

直接启动客户端（不带参数），点"添加新的连接"，填：

- 设备名称：任意（如 `摄像头 01`，会话内唯一）；
- RTMP URL：完整地址 `rtmp://127.0.0.1:1935/live/camera01`。

对话框的默认 URL 已经由 media-server 配置生成（见 4.3），手工输入的
完整 URL 永远优先，互不冲突。

### 4.3 方式三：配置文件驱动（推荐的多路方式）

这是 Phase 5/6 落地的正式方式：**一个 INI 文件同时描述"服务器在哪"和
"有哪些摄像头"**，客户端启动时自动生成各路的 RTMP URL 并连接。

#### 4.3.1 配置文件长什么样

复制示例 `deploy/srs/media-server.example.ini` 为 `media-server.ini`：

```ini
[server]
host = 127.0.0.1            ; SRS 主机：本机开发就是 127.0.0.1
                            ; 客户端在别的机器时，填 SRS 主机的 LAN IP
rtmpPort = 1935             ; 对应 srs-minimal.conf 的 listen
application = live          ; 单层 app 名，只允许字母数字 _ -
apiBaseUrl = http://127.0.0.1:1985   ; 健康 API 地址（回环）
apiHealthEnabled = true     ; 是否启用 API 健康探测

[camera01]                  ; 段名以 camera 开头
cameraId = camera01         ; 稳定 ID（规则同 app），缺省用段名
displayName = 摄像头 01      ; 界面上显示的名字，随便改，不影响连接
streamKey = camera01        ; SRS 流名 —— 推流端 URL 的最后一段必须也是它
autoStart = true            ; 启动客户端时自动连接

[camera02]
cameraId = camera02
displayName = 摄像头 02
streamKey = camera02
autoStart = true
```

#### 4.3.2 URL 是怎么生成的（记住这个公式）

```text
rtmp://<host>:<rtmpPort>/<application>/<streamKey>
```

例：上面配置生成 `rtmp://127.0.0.1:1935/live/camera01`。
**推流端和拉流端用同一个公式**：摄像头/FFmpeg 推到哪，客户端就拉哪。

约束（不满足会被跳过并记 warning 日志）：

- `application`、`streamKey`、`cameraId` 只允许 `[A-Za-z0-9_-]+`（一层，
  不能有 `/`、空格、中文）；
- `cameraId` 和 `streamKey` 在全配置里分别唯一；
- 最多 16 个 camera 段（与客户端上限一致），第 17 个会被跳过；
- 老的 `camera001` 命名和新 `camera01` 命名都合法，SRS 不挑剔名字。

#### 4.3.3 启动方式

开发期以 **Visual Studio F5** 为唯一首选入口：

1. 选择 `Qt-Debug` CMake Preset；
2. 首次使用或修改 vcpkg 后，执行“删除缓存并重新配置”；
3. 确认 CMake 缓存中的 `CMAKE_TOOLCHAIN_FILE` 指向本机 vcpkg，且
   `FFMPEG_INCLUDE_DIRS` 非空；
4. 将 `rtmp_monitor.exe` 设为启动项；
5. 在“调试和启动设置”中为该目标添加
   `--media-server-config <media-server.ini 的本机路径>`，按 F5 启动。

当前构建目录只自动复制 FFmpeg DLL，尚未执行 `windeployqt`。因此不要从资源管理器
直接双击 EXE：它可能找不到 MSVC Qt DLL，或从 PATH 误加载 MinGW Qt。Developer
PowerShell 中的命令行启动仅作为诊断后备，不作为中文界面视觉验收证据。

可选开关：

- `--no-camera-autostart`：只解析校验配置，不自动连接（检查配置对不对
  时用）；
- `--url` 与配置文件可以同时用：`--url` 优先；配置里的流若与 `--url`
  重复会被跳过并记 warning；总数仍不得超过 16。

#### 4.3.4 健康监控在干什么（不用管，但要知道）

只要 endpoint 配好（含默认值），客户端每 2 秒对 SRS 做一次"体检"：

- **healthy**：1935 能连 + API 返回正常（能看到版本号，如 6.0.184）；
- **degraded**：1935 能连但 API 异常。**不影响播放**，只是体检打折；
- **unavailable**：两个都连不上（SRS 挂了/停了）。

状态变化会写进系统日志（`system.jsonl` 的 `server` 模块），并在界面的
"事件消息"面板提示一次（不刷屏）。**重点**：播放和健康检查是两条独立的
路——就算 API 全挂，视频照播；SRS 恢复后，客户端播放器靠既有的 3 秒
自动重连自己回来，不需要重启客户端。

#### 4.3.5 完整联动演示（照抄可跑）

```powershell
# 1. 启动 SRS
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\srs\srs_dev_wsl.ps1 -Action Start

# 2. 推 4 路测试流（WSL 终端执行，或参考 verify 脚本）
#    ffmpeg -re -stream_loop -1 -i "$HOME/src/srs-6.0.184/trunk/doc/source.flv" \
#      -c copy -f flv rtmp://127.0.0.1:1935/live/camera01   (camera02/03/04 同理)

# 3. 准备 4 路配置（复制示例并加 camera01~04 段）

# 4. 启动客户端
.\out\build-windows-x64\debug\rtmp_monitor.exe --media-server-config .\media-server.ini
```

**已实测结果**：4/4、16/16 路全部 playing；停掉 camera03 的推流，其余
15 路不受影响；恢复推流后 camera03 在 20 秒内自动回到 playing；停掉
SRS 再启动，客户端自动恢复，不用重启。

### 4.4 接真实摄像头（当前唯一未验证的环节）

摄像头端（按其厂商文档）把 RTMP 推流地址设为：

```text
rtmp://<运行SRS的主机的LAN-IP>:1935/live/<streamKey>
```

注意：

- **不能写 127.0.0.1**（那是摄像头自己）；
- Windows 防火墙：只为"专用网络"加 TCP 1935 入站规则，不要整体关防火墙；
- WSL2 mirrored networking 下，LAN 入站能否直达 WSL 里的 SRS 当前标记为
  `[需要验证]`（ISSUE-008），需要真实摄像头或第二台设备实测；
- 摄像头编码要求：H.264 视频（客户端现有解码链路），建议 GOP 1～2 秒；
  音频可先关闭（`-an` 等价设置）排除变量；
- streamKey 与 media-server.ini 里某 camera 段的 `streamKey` 一致。

---

## 5. 常见问题 FAQ

**Q1：启动脚本报"1935 已被占用且不是本脚本所有"？**
这是安全拦截（退出码 3）。按输出里的 PID/进程名人工确认占用者（旧
SRS/nginx/Docker/其他），处理完再启动。脚本永远不会替你杀进程。

**Q2：推流成功但播放器黑屏/一直 connecting？**
按顺序排查：`-Action Status` 看 API 里有没有这路流（没有→推流端问题）；
ffprobe 拉一下能不能读到 h264；流名两边是否完全一致（`camera01` vs
`camera1`）；是否等了第一个关键帧（GOP 太大时多等几秒）。

**Q3：API 里只有 10 路流，明明推了 16 路？**
接口默认分页 count=10。用 `?start=0&count=100`。这不是丢流。

**Q4：`/api/v1/streams` 返回 "Redirect to /api/v1/streams/"？**
加尾斜杠：`/api/v1/streams/`。

**Q5：WSL 里后台启动的 SRS/ffmpeg 一转眼就没了？**
必须用 `setsid nohup ... </dev/null &` 加 `disown` 加 `sleep 1`（本项目
脚本已内置）。直接 `nohup ... &` 在 wsl.exe 退出时会被会话回收杀掉。

**Q6：配置文件里中文 displayName 显示乱码？**
INI 文件请保存为 UTF-8。客户端读配置按 UTF-8 处理；若用 PowerShell 读
metrics/日志排查，注意 PS 5.1 默认按 ANSI 读文件，中文需显式指定 UTF-8。

**Q7：SRS 停了，客户端会怎样？**
各路进入重连（UI 清旧帧显示重连状态），监控约 10 秒后报 unavailable；
SRS 恢复后各路在几秒～20 秒内自动回到 playing，不用重启客户端。如果
你设了非零 `--max-reconnect-failures` 且已达上限，则需要手动"重新连接"。

**Q8：我能从 Qt 客户端里启动/停止 SRS 吗？**
不能，这是设计决定。开发期用脚本、产品期用 systemd；客户端只观察健康。
批量管理进程的规则在 `srs_dev_wsl.ps1` 里：只碰自己启动且身份匹配的。

**Q9：想确认 SRS 版本对不对？**
WSL：`"$HOME/opt/srs-6.0.184/objs/srs" -v`（应 6.0.184）；
源码：`cd ~/src/srs-6.0.184 && git rev-parse HEAD`（应 9f8670b2...）；
API：`Invoke-RestMethod http://127.0.0.1:1985/api/v1/versions`。

**Q10：换台电脑跑客户端？**
客户端机器不需要 SRS；把 media-server.ini 的 `host` 和 `apiBaseUrl` 改成
SRS 主机的 LAN IP（API 只对 SRS 本机回环开放，远程机器上的健康 API 探测
会失败，监控显示 degraded，播放不受影响——这是预期行为）。

**Q11：SRS 网页能否代替 RtmpMonitor？**
不能。SRS Console/Player 是服务器观察和单路协议验证工具，适合判断“流有没有
到 SRS、HTTP-FLV 能不能播放”。本项目的 1～16 路管理、FFmpeg 解码、OpenGL
渲染、状态、重连、全屏和截图仍由 RtmpMonitor 负责。网页正常只能证明辅助链路
正常，不能代替 Visual Studio F5 下的客户端验收。

---

## 6. 延伸阅读

- 实施与逐 Phase 验收记录：[../../weeks/week7/week7_srs_server_integration.md](../../weeks/week7/week7_srs_server_integration.md)
- 异常恢复矩阵与发布门禁：[srs_failure_recovery.md](srs_failure_recovery.md)
- RTMP 链路验证（含 nginx 历史）：[rtmp_chain_verification.md](rtmp_chain_verification.md)
- ARM 部署边界：[cross_platform_build.md](cross_platform_build.md) §9
- 总体方案与 `[需要验证]` 清单：`docs/srs_server_integration_plan.md`
- SRS 官方文档：<https://ossrs.io/lts/en-us/docs/v6/doc/getting-started-build>
