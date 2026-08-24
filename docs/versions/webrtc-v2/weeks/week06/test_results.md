# WebRTC V2 Week 6 实际测试结果

> 本页只记录实际执行事实，不把本地双实例写成两台物理电脑。2026-08-24 用户确认：最终 ZIP
> 的两个独立本地副本、两种拓扑各十轮和完整自动门禁可作为 Week 6 的设计验收依据。因此
> `W6-DESIGN-GATE` 与本阶段 `W6-GATE` 标记为通过，Week 7/P2P 可以开始；真实双机
> `W6-LAN-01/02/03` 与物理窗口生命周期验证改为延期的环境资格，不再阻塞研发。

## 1. 环境与被测提交

| 项目 | 实际值 |
| --- | --- |
| 执行日期 | 2026-08-23 至 2026-08-24（Asia/Singapore） |
| 系统 | Windows 11 22631，x64 |
| 编译器 | Visual Studio 2026 18.9.1，MSVC 19.51.36256 |
| Windows SDK | 10.0.26100.0，目标 Windows 10.0.22631 |
| CMake | Visual Studio 2026 内置 CMake 4.3.1-msvc1 |
| Qt | 6.6.1 MSVC x64 |
| libdatachannel | 0.24.5，vcpkg `x64-windows` |
| FFmpeg | vcpkg FFmpeg 运行库；资格样本由本机 FFmpeg 工具生成 |
| 分支 | `Beta` |
| C++/全量资格提交 | `e6921e1b6102c0182782129cf7d7aa05667f7cfa` |
| 最终资格脚本与包提交 | `ada15ac80993d9241e1a314160d68f53f347ac89` |

`ada15ac` 只在已经全量通过的 `e6921e1` 上补充“从 ZIP 全新展开、包扫描、CLI 负向检查”和
`VerifyLan` 空结果提示；没有修改 C++、CMake target 或运行 DLL。最终 Release 包从干净的
`ada15ac` 生成，并再次跑满两种拓扑各十轮。

## 2. fresh 构建与完整 CTest

实际执行入口：

```powershell
& scripts/webrtc/qualify_week6.ps1 -Action Run `
  -QtRoot $env:QTDIR -VcpkgRoot $env:VCPKG_ROOT
```

| 配置 | 全目标构建 | 完整 CTest | CTest 实际时间 | 关键边界 |
| --- | --- | ---: | ---: | --- |
| fresh Debug，WebRTC OFF | 通过 | 39/39 | 120.86 秒 | 存在 `rtmp_monitor_webrtc_disabled_test`，没有 Week 6 客户端入口 |
| fresh Debug，WebRTC ON | 通过 | 45/45 | 192.14 秒 | endpoint、runtime paths、viewer pipeline 均存在并通过 |
| fresh Release，WebRTC ON | 通过 | 45/45 | 160.84 秒 | 作为便携包的实际二进制来源 |

三套 CTest 均输出 `100% tests passed, 0 tests failed`。ON 比 Week 5 的 44 项增加一项
`rtmp_monitor_webrtc_runtime_paths_test`；Week 5 资格脚本已取消对旧总数的硬编码，改为完整
CTest 成功并核对必需测试名。

关键 C++ 证据如下：

| 测试 | 实际证据 |
| --- | --- |
| `rtmp_monitor_webrtc_endpoint_test` | 两种信令拓扑均取得真实 selected pair；只暴露候选类型与 transport；真实 RTP 到达；任一端关闭后对端在有界时间进入 Failed；关闭后 sink 计数不再增长；重复关闭与队列清理通过 |
| `rtmp_monitor_webrtc_runtime_paths_test` | repository、portable marker 优先、无有效布局、重复解析及 package-local sample 路径全部通过 |
| `rtmp_monitor_webrtc_viewer_pipeline_test` | 官方 H.264 depacketizer、FFmpeg 解码、capacity-1 mailbox 与 CPU framebuffer 闭环通过；包含容量丢弃后的新 SPS/PPS/IDR 恢复证据 |
| `rtmp_monitor_layer_dependency_test` | transport、media、render/UI 依赖方向保持通过 |

同机 libdatachannel 可能把远端 peer-reflexive 候选脱敏归类为 `srflx`；资格逻辑没有把它伪装成
`host`。本轮二十轮便携运行实际均观察到 `host/host + udp`，但报告仍明确标记为同机，不据此
宣称双机 LAN 已通过。

## 3. Release 包与 ZIP 黑盒结果

最终制品位于 Git 忽略目录：

```text
out/packages/webrtc-week6/
├─ RtmpMonitor-WebRTC-ada15ac80993-windows-x64/
└─ RtmpMonitor-WebRTC-ada15ac80993-windows-x64.zip
```

| 项目 | 实际结果 |
| --- | --- |
| ZIP 大小 | 29,558,608 字节 |
| 阶段目录文件数 | 33 |
| manifest source commit | `ada15ac80993d9241e1a314160d68f53f347ac89` |
| 样本 | 6 秒、1280×720、30 fps、H.264 Constrained Baseline 3.1、无 B 帧；生成与 ffprobe 验证通过 |
| Qt platform plugins | `platforms/qwindows.dll` 与 `qoffscreen.dll` 均存在 |
| 许可 | Qt、FFmpeg、libdatachannel、libjuice、libsrtp、OpenSSL 等实际随包组件许可存在 |
| 新 ZIP 展开 | 通过；不是直接复用阶段目录 |
| `--help` | exit 0，稳定 WebRTC V2 publisher/viewer 描述存在 |
| viewer 携带 `--source sample` | exit 2，稳定 `invalid_arguments` 事件存在 |
| 包内 `Check` / `SelfTest` | 通过 / 通过 |
| 禁入扫描 | PDB、LIB、EXP、OBJ、C/C++ 源码、CMakeCache、状态、Offer/Answer、非空日志/结果/会话目录、开发机绝对路径均未发现 |

从最终 ZIP 全新展开后创建两个独立副本，由自动 broker 只复制完整 Offer/Answer JSON 文件：

| 拓扑 | 轮数 | 结果 | 每轮证据 |
| --- | ---: | --- | --- |
| publisher/Offerer ↔ viewer/Answerer | 10 | 10/10 通过 | portable layout、selected UDP pair、RTP、AU、decoded、presented、exit 0、交换目录清零 |
| viewer/Offerer ↔ publisher/Answerer | 10 | 10/10 通过 | portable layout、selected UDP pair、RTP、AU、decoded、presented、exit 0、交换目录清零 |

汇总文件 `out/webrtc-week6/final-zip-portable-results.json` 实际记录：
`packageFromFreshZip=true`、`sameMachinePortable=true`、`lanClaimed=false`、20/20 passed。
这项测试证明便携布局、DLL 闭合、完整文件搬运和播放闭环，不证明两台物理电脑之间的网卡与防火墙。

## 4. Week 4 / Week 5 回归

实际执行：

```powershell
& scripts/webrtc/qualify_week4.ps1 -Action SelfTest
& scripts/webrtc/qualify_week4.ps1 -Action Run -QtRoot $env:QTDIR -VcpkgRoot $env:VCPKG_ROOT
& scripts/webrtc/qualify_week5.ps1 -Action SelfTest
& scripts/webrtc/qualify_week5.ps1 -Action Run -QtRoot $env:QTDIR -VcpkgRoot $env:VCPKG_ROOT
```

| 回归项 | 实际结果 |
| --- | --- |
| Week 4 SelfTest | 通过 |
| Week 4 Run | 通过；OFF 39/39、ON 45/45、两种旧 publisher/peer 角色流程通过 |
| Week 5 SelfTest | 通过 |
| Week 5 Run | 通过；OFF 39/39、ON 45/45、两种 publisher/viewer 播放拓扑通过 |
| Week 6 repo SelfTest | 通过；文档字符量、6 张 SVG/XML、相对链接、类职责与敏感模式检查通过 |
| Week 6 package runner SelfTest | 通过 |

Week 4 首次在受限沙箱内启动 MSBuild 时，因为不能读取当前用户的 Windows SDK 注册目录而失败；
允许在沙箱外读取正常工具链后，同一完整 Run 通过。它是执行环境权限问题，不是源码或测试失败。

## 5. Qt 弹窗回归与修复证据

资格运行期间曾出现 `rtmp_monitor_event_center_service_test.exe` 找不到
`QTest::toPrettyUnicode(QStringView)` 入口的 Windows 弹窗。实际根因是宿主 `PATH` 中
MinGW Qt bin 排在 MSVC Qt 前方：Release 测试需要无后缀 `Qt6Test.dll`，因此误载 MinGW DLL；
Debug 使用 `Qt6Testd.dll`，所以此前没有暴露同一冲突。

`QualificationCommon.psm1` 现统一构造收敛运行时 PATH，只包含所选 MSVC Qt bin、与配置匹配的
vcpkg runtime bin 和 Windows 系统目录。Week 5/6 在 CTest 前设置、结束后恢复。修复后：

- Release `rtmp_monitor_event_center_service_test` 单项 1/1 通过；
- fresh Release ON 完整 45/45 通过；
- Week 4、Week 5 完整回归均未再次出现弹窗；
- 最终 ZIP 使用 exe 同级 DLL 与包内 platforms 插件，`--help` 和双拓扑运行通过。

## 6. 设计验收通过与物理双机延期

### 6.1 验收策略变更

原 Week 6 计划要求四份物理双机报告才能解除开发门禁。当前没有第二台电脑，而最终 ZIP 已经在
同一台电脑的两个独立展开目录中完成两种拓扑各十轮，且每轮均具备 selected UDP pair、RTP、AU、
decoded、presented、退出码和零残留证据。用户据此接受以下替代口径：

- 本地两个独立包副本不是“真实双机 LAN”证据，结果仍保持 `sameMachinePortable=true` 与
  `lanClaimed=false`；
- 这组结果足以证明 Week 6 的接口设计、组合关系、便携布局、信令搬运、媒体闭环和自动生命周期，
  因而 `W6-DESIGN-GATE` 通过；
- 本阶段 `W6-GATE` 按产品验收决定标记为“通过（本地双实例设计验收）”，允许进入 Week 7/P2P；
- 真实网卡、防火墙、两台 Windows 主机和现场窗口体验仍未验证，统一放入
  `W6-PHYSICAL-LAN` 延期项，未来具备设备时可补测，但不回退当前设计通过状态。

### 6.2 可选的物理 LAN 聚合器仍保持严格

空结果目录执行：

```powershell
& scripts/webrtc/qualify_week6.ps1 -Action VerifyLan `
  -ResultRoot out/webrtc-week6/empty-lan-results
```

实际按预期返回非零，并稳定提示旧物理 LAN 聚合器的固定错误：

```text
W6-GATE is blocked: four valid dual-PC reports were not found.
```

这条输出只说明 `VerifyLan` 没有收到四份物理双机报告。它不否定已经通过的本地双实例设计门禁，
也不再阻塞 Week 7；后续若调整脚本术语，可把它改名为 `W6-PHYSICAL-LAN`，但本次纯文档验收不
修改既有脚本行为。

| 门禁/资格项 | 当前真实状态 | 依据或缺少的证据 |
| --- | --- | --- |
| `W6-LOCAL-01` | 通过 | 最终 ZIP 两个独立副本，两种拓扑各 10/10，合计 20/20 |
| `W6-DESIGN-GATE` | 通过 | 构建、CTest、包、信令、媒体四层、角色反转、清理和回归证据齐全 |
| `W6-GATE` | 通过（设计验收口径） | 用户接受本地双实例作为 Week 6 开发门禁，Week 7/P2P 解锁 |
| `W6-LAN-01/02/03` | 延期，不阻塞 | 缺少两台物理 Windows 电脑；未来可补测两种拓扑与 host/host UDP |
| `W6-LIF-01` 物理场景 | 延期，不阻塞 | 缺少两台电脑上的 viewer/publisher 先关和现场窗口记录 |
| `W6-PHYSICAL-LAN` | 未验证 | 四份物理双机脱敏报告尚未收集，不影响设计通过结论 |

## 7. 未覆盖范围

没有第二台获准 Windows 电脑，因此当前不能证明真实双机网卡、防火墙、跨电脑文件搬运、两端窗口
观感或实际 `host/host + udp`；这些被明确标记为延期的环境资格，而不是已经通过的事实。没有执行
公网、STUN/TURN、WSS、摄像头、多路、正式产品 UI、ARM 真机、鉴权、TLS 或 RBAC 测试。Week 6
设计验收通过只解锁下一阶段开发，不把上述能力写成已验证。
