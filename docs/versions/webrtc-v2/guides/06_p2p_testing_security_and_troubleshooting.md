# 第 6 章：最终测试、隐私与 RtmpMonitor 映射

协议功能已经完成。最后一章把“我这里运行过”变成可重复门禁：CTest 运行单轮和十轮，120 秒停止挂住
进程，正则只认稳定 summary，PowerShell 同时验证 CLI、阶段计数和敏感模式零命中。

## 本章完成后你将得到什么

- 一个完整、独立、可从全新目录构建的 MiniLab；
- 单轮与十轮两项 CTest；
- 帮助、非法参数、隐私和退出码验收；
- 对 Week 2、Week 3 与 Week 4～5 的准确能力映射。

## 前置检查点

- 第 5 章 `main.cpp` 与最终参考文件完全一致；
- 无参数和 `--repeat=10` 都成功；
- `--repeat=0` 返回 2；
- 每轮 shutdown，一次全局 Cleanup。

## 预计时间

45～60 分钟。

## 本章在最终项目中的位置

本章只修改独立项目的 `CMakeLists.txt`。完成后学习目录的两个文件应分别与
[`tutorials/webrtc-minilab/CMakeLists.txt`](../../../../tutorials/webrtc-minilab/CMakeLists.txt) 和
[`main.cpp`](../../../../tutorials/webrtc-minilab/main.cpp) 一致。

## 本章知识点

- `include(CTest)` 与 `BUILD_TESTING`；
- `TIMEOUT`、`RUN_SERIAL`；
- `PASS_REGULAR_EXPRESSION` 为什么只匹配稳定汇总；
- 输出隐私扫描和测试结论边界。

## 当前项目目录

```text
out/learn-webrtc-minilab/
├─ CMakeLists.txt   # 尚无 CTest
└─ main.cpp        # 最终运行逻辑
```

## 步骤 6.1：把单轮和十轮写成 CTest 门禁

### 当前问题

手工看到一次成功不能防止以后回归。测试既要验证单轮，也要覆盖重复创建/关闭；同时必须给整个进程
设置上限，避免条件变量或 Cleanup 缺陷无限占用 CI。

### 修改文件

`out/learn-webrtc-minilab/CMakeLists.txt`

### 代码修改

第 1 章的 `CMakeLists.txt` 已经包含精确依赖诊断、目标级警告和 Windows DLL 部署，后续章节一直
原样保留。本章只追加 CTest，不重复或覆盖前面的构建边界。

<div style="background-color:#fff3cd;border-left:5px solid #d6a700;padding:10px 14px;margin:10px 0;">
<strong>新增（黄色）：测试开关、两个测试和三类属性</strong>
<pre><code>include(CTest)
if(BUILD_TESTING)
    add_test(NAME webrtc_minilab_single COMMAND webrtc_minilab)
    add_test(NAME webrtc_minilab_repeat_10
        COMMAND webrtc_minilab --repeat=10)
    # 再设置 RUN_SERIAL、TIMEOUT 和 PASS_REGULAR_EXPRESSION。
endif()</code></pre>
</div>

把下面的完整测试块追加到 `CMakeLists.txt` 末尾：

```cmake
include(CTest)
if(BUILD_TESTING)
    add_test(NAME webrtc_minilab_single COMMAND webrtc_minilab)
    add_test(NAME webrtc_minilab_repeat_10
        COMMAND webrtc_minilab --repeat=10)

    set_tests_properties(
        webrtc_minilab_single
        webrtc_minilab_repeat_10
        PROPERTIES
            RUN_SERIAL TRUE
            TIMEOUT 120
    )

    set_tests_properties(webrtc_minilab_single PROPERTIES
        PASS_REGULAR_EXPRESSION "summary rounds=1 result=success")
    set_tests_properties(webrtc_minilab_repeat_10 PROPERTIES
        PASS_REGULAR_EXPRESSION "summary rounds=10 result=success")
endif()
```

**API 卡片：`include(CTest)`。** CMake 自带的 CTest 模块会声明 `BUILD_TESTING` 缓存开关并调用
`enable_testing()`。默认值通常为 ON，用户可在配置时用 `-DBUILD_TESTING=OFF` 关闭本项目测试。
`if(BUILD_TESTING)` 使测试定义随开关一起消失，但不影响可执行文件本身。

**API 卡片：`add_test(NAME name COMMAND target args...)`。** `NAME` 后是 CTest 中唯一的测试名；
`COMMAND` 后首先写 CMake 可执行目标，再写命令行参数。使用目标名而不是硬编码 exe 路径，可以让
CMake 按生成器和配置定位程序。它只注册测试，不会在配置或构建阶段运行程序。每个测试进程由 CTest
拥有；MiniLab 仍只拥有自己进程内的 session 和库资源。

**API 卡片：`set_tests_properties()`。** 第一个调用同时修改两项测试：`RUN_SERIAL TRUE` 禁止
它们并行争用同一台机器的本地网络资源；`TIMEOUT 120` 允许 CTest 在整个子进程超过 120 秒时终止
它。后两个调用分别设置 `PASS_REGULAR_EXPRESSION`，要求输出包含稳定 summary。属性修改的是
CTest 元数据，不修改 MiniLab 内部状态。

执行链是 `cmake configure → 生成 CTestTestfile.cmake → ctest 启动 exe → 检查退出码、超时和
成功正则`。正则没有匹配异步 state 行或毫秒耗时，因为这些内容可能合法变化。当前门禁能证明两种
轮数得到稳定成功汇总；它不能证明媒体 Track、双机 LAN 或公网可达。第 1 章的精确依赖、编译选项
和 DLL 生成表达式说明仍分别见
[精确依赖](01_webrtc_from_rtmp.md#13-逐块理解-cmake)与
[运行库部署](01_webrtc_from_rtmp.md#13-逐块理解-cmake)。

### 构建

```powershell
cmake -S 'out/learn-webrtc-minilab' `
  -B 'out/build-webrtc-minilab-learning' `
  -DBUILD_TESTING=ON
cmake --build 'out/build-webrtc-minilab-learning'
```

### 运行或测试

```powershell
ctest --test-dir 'out/build-webrtc-minilab-learning' --output-on-failure
```

再从全新目录验证仓库最终项目：

```powershell
cmake -S 'tutorials/webrtc-minilab' `
  -B 'out/webrtc-minilab-final' `
  -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows `
  -DBUILD_TESTING=ON
cmake --build 'out/webrtc-minilab-final'
ctest --test-dir 'out/webrtc-minilab-final' --output-on-failure
```

### 观察证据

本仓库最终 MiniLab 已实际配置、构建和测试：

```text
Test #1: webrtc_minilab_single ...... Passed
Test #2: webrtc_minilab_repeat_10 ... Passed
100% tests passed, 0 tests failed out of 2
```

### 通过条件

- 两项测试均注册并通过；
- 单轮匹配 rounds=1，十轮匹配 rounds=10；
- 每项有 120 秒进程上限；
- 测试不依赖异步行顺序或固定耗时。

## API 与执行链：三层截止时间

```text
每轮业务等待：20 秒总 deadline
所有轮次后的 rtc::Cleanup：10 秒 deadline
CTest 进程外层：120 秒 TIMEOUT
```

内层给出准确错误阶段，外层防止代码本身失灵。CTest 的 `PASS_REGULAR_EXPRESSION` 只证明出现稳定成功
汇总；退出码非 0 仍会让测试失败。

## 本章小实验

把最终验收收进一段 PowerShell：同时检查帮助、非法参数、十轮阶段计数和敏感模式零命中。脚本只把
输出保存在内存，不写进仓库。

### 答案代码

#### 1. 检查命令、退出码与固定输出

```powershell
$exe = 'out/webrtc-minilab-final/webrtc_minilab.exe'

$help = (& $exe --help 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0 -or $help -notmatch 'repeat_range: 1\.\.100') {
    throw 'help contract failed'
}

$invalid = (& $exe --repeat=0 2>&1 | Out-String)
if ($LASTEXITCODE -ne 2 -or $invalid -notmatch 'error=invalid_arguments') {
    throw 'invalid argument contract failed'
}

$captured = (& $exe --repeat=10 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0) {
    throw 'MiniLab failed'
}
```

`& $exe` 使用 PowerShell 调用运算符启动原生程序；`2>&1` 把标准错误并入标准输出，
`Out-String` 再把输出收进内存字符串。每次原生程序结束后必须立即读取 `$LASTEXITCODE`，因为下一条
原生命令会覆盖它。帮助契约检查退出码 0 和轮数范围；非法参数检查退出码 2 和固定错误分类；
十轮运行必须返回 0。脚本没有把捕获内容写入文件。

#### 2. 统计稳定阶段，而不是依赖回调顺序

```powershell
if (([regex]::Matches(
        $captured,
        '(?m)^round=\d+ stage=shutdown state=closed result=success\r?$'
    )).Count -ne 10) {
    throw 'shutdown count failed'
}

if (([regex]::Matches(
        $captured,
        '(?m)^stage=cleanup state=complete result=success\r?$'
    )).Count -ne 1) {
    throw 'cleanup count failed'
}

if (([regex]::Matches(
        $captured,
        '(?m)^summary rounds=10 result=success\r?$'
    )).Count -ne 1) {
    throw 'summary failed'
}
```

`[regex]::Matches(text, pattern)` 返回所有匹配，`.Count` 是数量。`(?m)` 开启多行模式，使
`^` 和 `$` 针对每一行；`\r?` 同时兼容 Windows CRLF 与只含 LF 的捕获文本。脚本只统计由控制线程
输出的 shutdown、Cleanup 和 summary，不统计回调状态行，所以线程调度变化不会让门禁误报。

十轮应有 10 次 session shutdown、1 次进程级 Cleanup 和 1 次最终 summary。这些计数验证资源边界，
但不能证明包传输时序完全相同。

#### 3. 扫描敏感输出模式

```powershell
$forbidden = @(
    'a=candidate:',
    'a=ice-(?:ufrag|pwd):',
    'fingerprint:',
    '(?i)\b(?:stun|turn|turns):',
    '(?i)\btoken\b',
    '\b(?:\d{1,3}\.){3}\d{1,3}\b',
    '(?:[0-9A-Fa-f]{1,4}:){2,}',
    '\[[0-9A-Fa-f:]+\]:\d{1,5}',
    '\b[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[1-5][0-9a-fA-F]{3}-[89abAB][0-9a-fA-F]{3}-[0-9a-fA-F]{12}\b',
    '(?i)session-exchange',
    '[A-Za-z]:\\'
)

foreach ($pattern in $forbidden) {
    if ($captured -match $pattern) {
        throw 'sensitive output detected'
    }
}

'acceptance: ok'
```

`$forbidden` 是正则允许列表的反面：它覆盖 candidate、ICE 凭据、fingerprint、STUN/TURN URL、
Token、IPv4、IPv6、带端口 IPv6、完整 UUID、会话目录名和 Windows 绝对路径。`foreach` 按模式
扫描 `$captured`；命中时只抛固定错误，不打印原始命中内容，避免验收工具自己造成二次泄漏。

这里的零命中只约束 MiniLab 的可见输出。它不表示操作系统、抓包工具或第三方调试器中不存在网络
元数据，也不能代替产品日志审计。

### 实验验证

```powershell
git diff --no-index -- `
  'out/learn-webrtc-minilab/main.cpp' `
  'tutorials/webrtc-minilab/main.cpp'
if ($LASTEXITCODE -ne 0) { throw 'main.cpp differs' }

git diff --no-index -- `
  'out/learn-webrtc-minilab/CMakeLists.txt' `
  'tutorials/webrtc-minilab/CMakeLists.txt'
if ($LASTEXITCODE -ne 0) { throw 'CMakeLists.txt differs' }
```

实际验收中，帮助返回 0、非法参数返回 2、十轮返回 0，shutdown=10、Cleanup=1、summary=1，敏感
模式零命中，两份学习文件与最终项目一致。

### 实验通过条件

- 脚本输出 `acceptance: ok`；
- 两次 `git diff --no-index` 都返回 0；
- 扫描失败时不把命中原文复制到 issue、聊天或周报。

## 故障恢复

| 阶段/现象 | 含义 | 恢复 |
| --- | --- | --- |
| CMake 找不到 0.24.5 | 版本、triplet 或 toolchain 不匹配 | 回到第 1 章精确依赖检查 |
| `timeout` | 当前阶段未在总 deadline 内完成 | 根据 stage 检查回调和对象存活，不加 sleep |
| `connection_failed` | PeerConnection/DataChannel 失败 | 检查 description 顺序和生命周期 |
| `protocol_mismatch` | label、类型或固定消息不符 | 恢复 `minilab`、`ping`、`pong` |
| `invalid_state` | 描述缺失或关闭已开始 | 检查控制线程顺序 |
| `cleanup_timeout` | 仍有库对象存活 | 检查 channel/peer 是否已释放 |
| CTest 找不到程序 | 生成器或配置目录不一致 | 在同一目录重新配置、构建、测试 |

不需要关闭防火墙，也不要添加公网端点来“修复”这个单机实验。

## 与 RtmpMonitor 的真实进度映射

| 范围 | 当前事实 | MiniLab 对应部分 |
| --- | --- | --- |
| Week 2 | 文件 schema、原子会话包和双 probe Offer/Answer 自动技术验证已完成；`W2-GATE` 仍等待用户双控制台人工复核 | PeerConnection、non-trickle、DataChannel、异步等待和安全输出 |
| Week 3 | 协议无关 H.264 契约、外部解码入口和 RTMP 兼容自动技术门禁已通过 | 理解未来 Track 应提交 H.264 AU，而不是侵入解码层 |
| Week 4 | 自动技术门禁已通过 | endpoint session、MP4 publisher、SendOnly H.264 Track、同一客户端两种信令角色 |
| Week 5 | 尚未实现 | ReceiveOnly depacketize、viewer 解码显示与同一客户端媒体回环 |

准确结论只有：“本机两个真实 PeerConnection 已完成 DataChannel 回环。”不能写成“WebRTC 视频已完成”、
“两台电脑已直连”或“公网 P2P 已通过”。Week 2 双控制台文件信令是课后延伸，不并入这个短小项目。

## 当前完整目录结构

```text
tutorials/webrtc-minilab/
├─ CMakeLists.txt
└─ main.cpp

out/webrtc-minilab-final/
├─ webrtc_minilab.exe + 运行库
└─ CTest 生成文件
```

## 本章检查点

- [ ] 全新配置、构建、单轮和十轮成功。
- [ ] CTest 2/2 通过，均有 120 秒上限。
- [ ] 帮助返回 0，非法参数返回 2。
- [ ] 输出隐私扫描零命中。
- [ ] 学习文件与最终项目一致。
- [ ] 能准确区分 MiniLab、Week 2/3 和未来视频能力。

## 本章总结

你从空目录构建了两个真实 PeerConnection，完成内存 non-trickle Offer/Answer、Connected、DataChannel
`ping → pong`、有界等待和安全退出，并把稳定结果写成自动测试。更重要的是，你知道它证明了什么、
没有证明什么。

## 下一章将解决什么问题

六章教程到此结束。若要继续：先做 Week 2 双控制台人工信令实验；Track、H.264 和双客户端视频属于
Week 4～5，不能从 DataChannel 成功直接跳过去。

- 上一章：[第 5 章](05_libdatachannel_qt_integration.md)
- 返回：[WebRTC V2 文档首页](../README.md)
