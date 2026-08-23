# WebRTC V2 Week 6 实际测试结果

> 本页只记录实际执行事实，不把计划目标写成已完成结果。Week 6 的代码、Release 便携包和同机
> 黑盒门禁已经完成；由于当前没有第二台获准 Windows 电脑，真实双机 `W6-LAN-01/02/03`、
> `W6-LIF-01` 与最终 `W6-GATE` 仍等待用户执行。

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

## 6. 双机门禁的实际状态

空结果目录执行：

```powershell
& scripts/webrtc/qualify_week6.ps1 -Action VerifyLan `
  -ResultRoot out/webrtc-week6/empty-lan-results
```

实际按预期返回非零，并稳定提示：

```text
W6-GATE is blocked: four valid dual-PC reports were not found.
```

| 门禁 | 当前真实状态 | 缺少的证据 |
| --- | --- | --- |
| `W6-LAN-01` | 等待用户 | PC A publisher/Offerer ↔ PC B viewer/Answerer 连续 10 轮 |
| `W6-LAN-02` | 等待用户 | PC A viewer/Offerer ↔ PC B publisher/Answerer 连续 10 轮 |
| `W6-LAN-03` | 等待用户 | 两台电脑 package ID 一致、真实 `host/host + udp`、收发播放证据 |
| `W6-LIF-01` | 等待用户 | viewer 先关、publisher 先关、窗口响应与重复运行 |
| `W6-GATE` | 阻塞于用户双机执行 | 四份有效脱敏报告与两台电脑环境/生命周期记录 |

## 7. 未覆盖范围

没有第二台获准 Windows 电脑，因此当前不能证明真实双机网卡、防火墙、跨电脑文件搬运、两端窗口
观感或实际 `host/host + udp`。没有执行公网、STUN/TURN、WSS、摄像头、多路、正式产品 UI、ARM
真机、鉴权、TLS 或 RBAC 测试；这些均不属于 Week 6 技术实现与本机自动资格的完成口径。
