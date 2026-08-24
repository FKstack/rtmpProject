# WebRTC V2 Week 8 测试指南：自动化脚本与手动验证

> 适用范围：Week 8 正式客户端“一次性 WebRTC 接收”
>
> 建议阅读时间：20～30 分钟
> 本文分为两个完全独立的版本：A 是自动化脚本测试，B 是人工可视测试。自动通过不能代替人工观感或真实公网结论。

## 1. 这次测试的对象是什么

Week 8 不是再测一次“两个 PeerConnection 能不能连上”。它要验证正式 `rtmp_monitor` 产品组合是否满足
以下完整链路：用户显式启动一次性请求，ReceiveOnly endpoint 经受管文件完成 Offer/Answer，发布端的真实
H.264 经 RTP、depacketizer、既有 FFmpeg 解码、`LatestFrameMailbox` 和正式视频画布呈现，产品只有在
selected pair 非 relay 且最近真实呈现不超过 1,000 ms 时显示 `Direct`。取消后 endpoint、worker、媒体流和
视频格全部归零；整个过程中不保存 peer，不修改 autoConnect，不绑定设备控制，也不自动启动 RTMP。

测试因此分为四层：

1. 静态门禁：脚本可解析、架构关键字存在、请求没有身份/持久化字段、图文完整；
2. C++ 产品集成：两个真实 PeerConnection、两个接收端信令角色、真实 H.264 出画和取消；
3. fresh 构建/全量回归：WebRTC OFF、Debug ON、Release ON 三组；
4. 人工观察：真实主程序菜单、对话框、文件操作、画面、状态遮罩、取消和故障提示。

![Week 8 验证矩阵](assets/07_test_matrix.svg)

## 2. 测试前必须知道的结论边界

自动脚本在一台 Windows 机器上运行，它能证明代码和本机真实媒体链闭环，不能证明两台物理电脑的防火墙、
NAT 或公网路径。手动版本如果也在同机运行，只能写“同机人工通过”；两台 LAN 主机才能写“物理 LAN 通过”；
公司网络与移动网络组合完成后，才能记录相应公网结果。任何场景都不能根据超时猜测 NeedsRelay；正式程序
只有拿到明确的 ICE Failed+srflx 证据才显示该分类。

测试资源不得使用客户或现场真实地址作为默认值。本文示例使用 Host，或用户本次明确输入
`stun:<stun-host>:3478`。不要把真实 URL、Offer/Answer、candidate、IP、端口或凭据复制进测试报告和 Git。

## 3. 版本 A：自动化脚本测试

### 3.1 入口与三种 Action

脚本位置为：

```powershell
scripts\webrtc\qualify_week8.ps1
```

它提供三个 action：

| Action | 是否构建 | 是否运行 CTest | 用途 |
| --- | --- | --- | --- |
| `Check` | 否 | 否 | 检查 Qt、vcpkg、VS/CMake、FFmpeg 前置环境 |
| `SelfTest` | 否 | 否 | 解析脚本，扫描 Week 8 契约，校验文档长度、关键内容、SVG XML 和图片链接 |
| `Run` | 是 | 是 | 生成固定媒体夹具，fresh 构建三组矩阵，跑全量 CTest，输出 JSON 结果 |

建议顺序始终是 Check → SelfTest → Run。不要直接从 Run 开始后再猜环境错误。

### 3.2 环境要求

自动矩阵的实测主线是 Windows 11、Visual Studio 2026 C++ 工具链、Qt 6.6.1 MSVC kit、vcpkg
`x64-windows` 和 libdatachannel 0.24.5。还需要命令行可找到 `ffmpeg.exe`、`ffprobe.exe` 和
`vswhere.exe`。脚本会通过 VS 安装目录选择其自带 CMake/CTest，避免系统 PATH 中较旧 CMake 不认识
`Visual Studio 18 2026` generator。

先在当前 PowerShell 会话提供非敏感工具根：

```powershell
$env:QTDIR = '<qt-root>'
$env:VCPKG_ROOT = '<vcpkg-root>'
```

`<qt-root>` 应直接包含 `bin`、`plugins`、`lib\cmake\Qt6`；`<vcpkg-root>` 应包含
`scripts\buildsystems\vcpkg.cmake`。尖括号表示必须替换的本机路径，不要原样执行。路径只传给本次脚本，
不应写入仓库文档或测试结果。

### 3.3 第一步：Check

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  scripts\webrtc\qualify_week8.ps1 -Action Check
```

成功输出应包含：

```text
Week 8 prerequisites passed.
```

Check 实际确认 Qt CMake package、Qt platform plugin 目录、vcpkg toolchain、VS CMake/CTest 和 FFmpeg
工具可用。它不下载依赖，不修改源码，不联网，也不代表任何 C++ 测试已经通过。

常见 Check 失败解释：

- `Please replace ... placeholder`：环境变量为空或仍含尖括号；
- `Qt6Config.cmake` 缺失：传入的不是具体 MSVC Qt kit；
- `plugins\platforms` 缺失：Qt 安装不完整，后续 QApplication 会启动失败或挂起；
- `vcpkg.cmake` 缺失：VCPKG_ROOT 层级错误；
- `vswhere.exe was not found`：Visual Studio Installer 组件不可用；
- `ffmpeg.exe`/`ffprobe.exe` 找不到：将工具加入本次 PATH 后重试。

### 3.4 第二步：SelfTest

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  scripts\webrtc\qualify_week8.ps1 -Action SelfTest
```

成功输出：

```text
Week 8 qualification self-test passed.
```

SelfTest 不编译 C++，主要防止交付物自己失效。它检查：

- `qualify_week8.ps1` 能被 PowerShell 解析；
- product/runtime 公共头含 controller、session、diagnostics、NeedsRelay 和关键负向事实；
- 一次性公共契约没有 autoConnect、SavedStream、deviceId、peerId、rtmpUrl；
- CMake 中存在 runtime/product/product-test 和生成 feature macro；
- 总结文章总长度至少 16,000 个字符且至少含 5,000 个中文字符，测试文章总长度至少 11,000 个字符且至少含 4,000 个中文字符；
- 两篇文章包含类职责、自动/手动、状态、1,000 ms、RTMP 和控制边界；
- 至少七张 SVG 能作为 XML 解析，Markdown 图片链接指向真实文件。

SelfTest 失败时应修正文档或契约，不应调低阈值绕过。它也不替代编译，因为字符串存在不代表 C++ 类型正确。

### 3.5 第三步：Run

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  scripts\webrtc\qualify_week8.ps1 -Action Run
```

Run 会在忽略的 `out` 下生成以下三组 fresh 构建：

```text
out/build-windows-x64/week8/debug-off
out/build-windows-x64/week8/debug-on
out/build-windows-x64/week8/release-on
```

每组先 `--fresh` 配置，再全目标构建，再运行该配置的完整 CTest。脚本不会删除源码或用户文档；若你通过
`-BuildRoot` 改目录，仍建议使用仓库 `out` 下的明确子目录。不要把 BuildRoot 指向仓库根或个人资料目录。

矩阵分别回答：

| 构建 | 要回答的问题 |
| --- | --- |
| Debug OFF | 默认产品是否仍无 WebRTC product target/action/test，旧 RTMP 回归是否通过 |
| Debug ON | 新入口、真实产品链和全部调试断言是否通过 |
| Release ON | 优化构建、主程序部署、datachannel 运行时和完整测试是否通过 |

脚本生成 6 秒、1280×720、30 FPS、H.264 Constrained Baseline、Level 3.1、无 B-frame 的固定样本，同时
生成 audio-only、非 H.264 和带 B-frame 的负向夹具供既有测试使用。文件只进入忽略目录，不提交 Git。

CTest 环境有一个重要细节：常规 UI/OpenGL 测试使用真实 `windows` Qt platform；脚本同时明确
`QT_PLUGIN_PATH` 与 `QT_QPA_PLATFORM_PLUGIN_PATH`，避免只找到 Qt DLL 却找不到 platform plugin 时
QApplication 停住。新增 `rtmp_monitor_webrtc_product_test` 在 CTest 属性里单独覆盖为 `offscreen`，
因此产品媒体闭环不弹出窗口。不能把全局平台改为 offscreen，否则 Windows OpenGL 和窗口边框测试会产生
与业务无关的假失败。

成功后结果写入：

```text
out/webrtc-week8/qualification-result.json
```

也可用 `-ResultPath` 指定仓库 `out` 下的其他文件。JSON 只记录 schema、周次、源提交、时间、三组 CTest
数量、两个角色和能力边界；不记录 SDP、candidate、地址、端口或完整日志。`publicNetworkClaimed=false`
必须保持，除非未来有独立真实公网资格流程，不能因为本机 Run 通过而手改为 true。

### 3.6 只重跑新增产品测试

开发时不必每次跑三组矩阵。已有 Debug ON 构建后，可以先运行：

```powershell
$env:QT_PLUGIN_PATH = Join-Path $env:QTDIR 'plugins'
$env:QT_QPA_PLATFORM_PLUGIN_PATH = `
  Join-Path $env:QTDIR 'plugins\platforms'

ctest --test-dir out\build-windows-x64\week8\debug-on `
  -C Debug -R '^rtmp_monitor_webrtc_product_test$' `
  --output-on-failure
```

预期是一项通过，通常约 3～5 秒。它包含两个数据行，不是只测一个角色，并覆盖呈现中断与恢复。若想看
QtTest 每条断言，可直接运行
测试 exe 并传 `-v2`，但要确保 Qt 和 vcpkg Debug DLL 在 PATH；更推荐由 CTest 运行，因为 CTest 已设置
offscreen 和超时。

### 3.7 新增产品测试逐步做了什么

`requestAndStatePolicyAreBounded()` 是纯策略测试。重点检查 1,000 ms 边界而非模糊“小于一秒左右”；检查
TURN 输入不被当前 STUN-only UI 接受；检查普通连接超时不被误写为 NeedsRelay。

`productReceivePath_data()` 产生接收端 Answerer 和接收端 Offerer 两行。`productReceivePath()` 每行执行：

1. 创建 `QTemporaryDir`，确保测试不碰真实 AppLocalData；
2. 创建 manager、CPU MainWindow、LogManager 和 product controller；
3. 找到 ON-only actions，检查开始可用、取消不可用；
4. 调用 controller `start()`，检查一个视频格和一个媒体流已创建；
5. 创建真实 SendOnly `WebRtcEndpointSession`；
6. 根据数据行生成或等待 Offer/Answer，所有包经 `SessionPackageCodec` 校验；
7. 双方连接后创建 send port，发送固定真实 H.264 IDR；
8. 等待 controller 进入 Direct；
9. 检查 decoded/presented、非 relay pair、帧年龄、无控制和无 RTMP 回退；
10. 关闭 sender，取消 controller，检查 Idle、0 视频格、0 流、action 恢复、交换目录清空；
11. 测试进程最后有界执行 `rtc::Cleanup()`。

这条测试不是 mock 网络计数。它实际经过 PeerConnection 和正式媒体/画布对象，只把窗口后端设为
offscreen。它也不等于人工观感测试，因为自动断言无法评价窗口尺寸、文字可读性、菜单手感和真实操作顺序。

![Week 5 媒体链在产品测试中的路径](assets/05_media_embedding.svg)

### 3.8 自动失败如何定位

按失败阶段定位，不要一上来清空仓库：

- **配置失败**：先看 generator、Qt/vcpkg 路径和 libdatachannel 精确版本；
- **编译 product target 失败**：检查公共头前向声明、Qt MOC 可见类型和 target link 方向；
- **product test 等待 10 秒后 Error**：先看是否让 render item 在 Connecting 时工作，再看 presented age；
- **Offer/Answer 等待 20～30 秒**：检查交换根是否唯一有效包、sessionId 是否匹配、角色是否相反；
- **普通 UI 测试无输出且进程有响应**：检查 Qt plugin path，不要把它当作业务死锁；
- **OpenGL/窗口测试在 offscreen 失败**：恢复全局 windows platform，仅对产品测试用 CTest offscreen 属性；
- **OFF 构建出现 product test**：检查 CMake if 边界和 feature macro；
- **取消后有流或文件**：检查 controller 释放顺序和 runtime 本地/远端包删除，不要只在测试里强制删除。

## 4. 版本 B：手动可视测试

人工版本主要检查正式主程序的入口、对话框、状态文本、真实画面和取消手感。最简单的发布端可以使用 Week 7
便携 `rtmp_monitor_webrtc_client` 或当前 ON 构建的同名客户端。人工测试不要求修改源码，也不要直接编辑
会话 JSON。

### 4.1 手动测试准备

需要：

- 一份 WebRTC=ON 的 `rtmp_monitor.exe`；
- 一份同版本 `rtmp_monitor_webrtc_client.exe` 及其运行时；
- 客户端可用的固定 H.264 样本；
- 一个仅用于本次测试的受管交换目录；
- 若做 STUN 场景，一个由用户明确选择的测试 STUN 地址；Host 场景不需要网络服务。

先关闭以前遗留的产品程序和开发者客户端。检查交换目录没有旧 `.offer.json`/`.answer.json`；不要把它指向
下载目录、桌面或仓库。测试结束后会话包应被消费，若仍有不认识的包，先移到隔离的临时目录再调查，不要
在不确认来源时批量删除用户文件。

启动正式程序时不要添加自动连接参数。WebRTC=ON 构建应看到 `WebRTC` 菜单；OFF 构建不应看到。若 OFF
仍出现菜单，这是门禁失败，不要继续用 UI 观感掩盖构建开关错误。

![一次性接收 UI 流程](assets/02_one_shot_ui_flow.svg)

### 4.2 场景一：接收端作为 Answerer

这是 UI 默认角色。步骤如下：

1. 打开正式程序，确认现有 RTMP 保存流没有因为打开程序而新增连接；
2. 选择 `WebRTC → 一次性接收…`；
3. 输入容易辨认但不含设备身份的名称，例如“WebRTC 临时画面”；
4. 保持“等待发布端 Offer（接收端作为 Answerer）”；
5. 首轮保持 Host；确认 STUN 输入框禁用；
6. 记下对话框显示的受管交换目录，但不要复制目录内 JSON 内容到报告；
7. 点击“开始接收”。应立即出现一个视频格，状态是等待 Offer/Answer 或 Connecting，不应显示 Direct；
8. 在发布端客户端选择 media-role=publisher、signaling-role=offer、固定 sample；
9. 等发布端生成 Offer，把完整文件作为文件复制进正式程序的受管目录，不要手工复制 JSON 文本；
10. 等正式程序生成对应 Answer，把 Answer 文件复制给发布端的受管目录；
11. 发布端开始发送后，正式视频格应出现测试画面，状态转为 `Direct · 当前画面已呈现`；
12. 观察至少 30 秒，画面应持续更新，状态不应仅因单次网络计数而提前出现；
13. 点击 `WebRTC → 取消当前会话`；视频格应消失，开始 action 恢复，取消 action 禁用。

关键检查点：在第 7～10 步期间，连接遮罩可以显示，但不能在没有画面时宣布 Direct；第 11 步后
Direct 表示非 relay pair 与当前呈现同时成立，不代表设备控制授权。

### 4.3 场景二：接收端作为 Offerer

第二次测试前确认上一条视频格和会话文件已经归零。再打开一次性对话框，选择“由接收端生成 Offer”。

1. 点击开始后，正式程序应在受管目录生成 Offer；
2. 把 Offer 文件复制到发布端目录；
3. 发布端这次选择 media-role=publisher、signaling-role=answer；
4. 发布端生成 Answer 后，把文件复制回正式程序目录；
5. 等画面和 Direct；观察 30 秒；
6. 直接点击视频格的删除按钮，而不是菜单取消；
7. 删除请求应走同一 cancel 路径，最终仍是 0 视频格、0 会话，并允许再次启动。

要特别避免把 Offerer 与 publisher 混为一谈。正式程序虽然生成 Offer，媒体方向仍是 ReceiveOnly；发布端
虽然作为 Answerer，仍是 SendOnly。若因为“谁先生成 SDP”而改变媒体角色，测试的不是 Week 8 契约。

### 4.4 场景三：取消发生在不同阶段

分别测试以下四个取消时机，每次都重新开始一条会话：

- 尚未出现任何 Offer/Answer 时取消；
- 已生成本地包、尚未收到远端包时取消；
- 已连接但画面尚未呈现时取消；
- Direct 稳定显示后取消。

每次取消的可见结果应一致：UI 不冻结，视频格移除，开始 action 可用，取消 action 不可用；发布端最终能退出；
本次包被消费或清理；再次启动不会收到上一代回调。可以在任务管理器观察进程，但不要以“CPU 归零”替代
资源计数和再次启动验证。

![取消与关闭时序](assets/06_shutdown_sequence.svg)

### 4.5 场景四：媒体中断和恢复

先达到 Direct，再让发布端暂停发送超过 1 秒，但暂时不关闭 PeerConnection。正式程序应在最近呈现年龄超过
1,000 ms 后离开 Direct，显示画面中断/Error 类提示；不能继续因为 ICE 仍 Connected 而保持 Direct。

若同一个连接恢复连续发送且当前代画面重新呈现，controller 可以回到 Direct，并记录 MediaRecovered。
这里的“恢复”只指视频呈现，控制仍未授权。若发布端直接关闭导致 endpoint Failed/Closed，则产品可能进入
连接中断 Error，用户应取消并重新建会话，不要求旧 session 自动复活。

人工中断测试要用“停止媒体但保持连接”和“关闭整个发布端”两种方式分别执行，因为它们验证不同分支。
不要通过拔掉未知网络或关闭系统防火墙制造不可恢复的环境变化。

### 4.6 场景五：错误和 NeedsRelay 文案

以下错误应为普通 Error，而不是 NeedsRelay：

- 没有放入 Offer/Answer 导致信令等待超时；
- sessionId 不匹配或目录存在多个有效输入；
- 发布端没有运行；
- 输入不支持的 TURN URL，应该在开始前直接校验失败；
- 已连接但 10 秒内没有任何真实呈现；
- 当前画面超过 1,000 ms 未呈现。

NeedsRelay 只能在一次明确的 STUN 场景中，由 endpoint 报告 ConnectionFailed、ICE Failed 且本轮看到 srflx
时出现。即使出现，它也只提示“当前网络可能需要 Relay”，本程序不会自动填 TURN、不会上传网络信息，也
不会回退 RTMP。若没有这组证据，报告写 Error/超时，不要人工把结果改成 NeedsRelay。

![状态证据分类](assets/04_state_evidence.svg)

### 4.7 场景六：确认没有 RTMP 与设备控制联动

这是人工测试里最容易漏掉的负向契约。达到 Direct 后检查：

- 视频格没有被标成设备控制目标；
- 操纵杆、键盘控制或 MQTT 面板不会因为点击该格而获得目标；
- 音频显示为不可用，而不是误选现有 RTMP 音频；
- 保存流列表没有新增 WebRTC 条目；
- 重启程序后不会自动恢复上一条 WebRTC 会话；
- 故意让 WebRTC 失败后，没有同名 RTMP 流在后台自动启动；
- 取消 WebRTC 不影响用户原本独立启动的 RTMP 流。

这些检查不要求对安全模块做过度扩展，只需证明本轮有限契约没有越界。发现联动时应修产品组合，不能在文档
里加一句“请勿使用控制”作为替代。

### 4.8 场景七：WebRTC OFF 兼容检查

启动 fresh Debug OFF 或 Release OFF 主程序，确认：

- 菜单栏没有 WebRTC 菜单；
- 程序启动不访问 STUN，不创建 session exchange；
- 程序目录无需 datachannel.dll 才能启动；
- 原有 RTMP 添加、保存流、autoConnect、音频、控制和日志仍按原行为；
- CTest 列表含 disabled dependency test，不含 product test。

OFF 兼容是 Week 8 的一等验收项，不是“编译不过再修”的备用模式。

## 5. 人工结果记录模板

建议在仓库外保存现场原始记录，在 `test_results.md` 只写脱敏摘要。每个场景可以使用下面模板：

```text
日期：YYYY-MM-DD
构建：Debug ON / Release ON / Debug OFF
场景：receiver-answerer / receiver-offerer / cancel / media-stale / OFF
主机关系：同机 / 物理 LAN / 公司网络与移动网络
ICE 模式：host / 用户明确 STUN（不记录具体 URL）
结果：通过 / 失败 / 未执行
观察：是否出画、是否 Direct、是否正确取消、是否无控制、是否无 RTMP 回退
清理：视频格 0、会话文件 0、相关进程退出
限制：不得记录 SDP、candidate、IP、端口、凭据和完整个人路径
```

如果物理 LAN 或公网未执行，就写“未执行”，不要用自动 product test 代替。若出现错误，记录稳定 reason、
发生阶段和可重复步骤，不粘贴包含网络标识的原始日志。

## 6. 通过标准

### 6.1 自动化版本通过标准

- Check 和 SelfTest 返回 0；
- fresh Debug OFF 全构建/CTest 通过，product test 不存在；
- fresh Debug ON 全构建/CTest 通过，product test 存在且两角色通过；
- fresh Release ON 全构建/CTest 通过，主程序与 datachannel runtime 存在；
- layer dependency test 通过；
- 生成配置宏 OFF=0、ON=1；
- `qualification-result.json` 产生且 `publicNetworkClaimed=false`；
- 测试结束没有相关残留进程或受管会话包。

### 6.2 手动版本通过标准

- ON 构建入口清晰且只有用户点击开始后才建会话；
- 两种接收端信令角色都能实际出画并达到 Direct；
- 没有画面时不提前 Direct，画面陈旧后离开 Direct；
- 四种阶段取消都不冻结并能再次开始；
- 错误分类不把普通超时误写为 NeedsRelay；
- 不新增保存档案、不自动连接、不绑定控制、不静默启动 RTMP；
- OFF 构建无菜单、无 WebRTC 自动联网和产品依赖；
- 结果按实际环境标记，不冒充真实 LAN/公网/ARM。

## 7. 失败后的恢复方法

如果自动 Run 中断，先确认没有 `ctest`、`rtmp_monitor` 或 `rtmp_monitor_webrtc_*` 测试进程，再重跑对应
CTest 或整个 Run。构建目录位于 `out`，可以新建另一个明确的 `-BuildRoot`，不需要 `git reset` 或删除源码。
若平台插件错误，修环境变量；若构建错误，修代码后增量编译；若受管目录存在旧包，只处理已确认属于本次测试
的文件。

如果人工程序卡在 Connecting，按顺序检查角色是否互补、文件是否复制到正确受管根、sessionId 是否匹配、
发布端是否真的开始发送、视频编码是否 H.264。不要先改 1,000 ms 阈值，也不要给状态机添加“只要 Connected
就 Direct”的捷径。

如果取消后无法再次启动，检查 controller 是否仍持有 input handle、session worker 是否 join、视频格是否从
MainWindow 移除。不要通过允许第二条并发 session 绕开；Week 8 的契约就是一次一条。

## 8. 最推荐的实际执行顺序

一次完整验收建议按以下顺序：

1. 在干净 Git 状态记录当前提交和分支；
2. 执行 Check；
3. 执行 SelfTest；
4. 执行完整 Run，保存脱敏 JSON 摘要；
5. 启动 Debug ON，做 Answerer 和 Offerer 两种人工出画；
6. 做四个取消时机；
7. 做媒体暂停/恢复与发布端关闭；
8. 检查无 RTMP/MQTT/保存流联动；
9. 启动 Debug OFF 做兼容检查；
10. 清理本次临时会话文件和进程；
11. 只把脱敏结论写入 `test_results.md`。

这套顺序先用廉价、确定性的门禁发现结构问题，再用全量回归证明兼容，最后把人的时间集中在脚本不能判断的
交互和观感上。它不要求为安全而新增庞大框架，也不牺牲 Week 8 最关键的事实边界。
