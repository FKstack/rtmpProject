# 第 1 章：环境搭建与第一个 MiniLab 程序

这一章只做一件小事：从空目录得到一个能运行的 C++ 程序。WebRTC 还没有出现，但依赖、构建边界和
学习目录会一次准备好。后面五章都在同一个目录继续改，不会突然复制最终答案。

## 本章完成后你将得到什么

- 一个独立的 `webrtc_minilab` CMake 项目；
- 精确检查 libdatachannel 0.24.5 的配置门禁；
- 第一条可观察输出：`miniwebrtc ready`；
- 对 WebRTC、信令和 ICE 的第一幅“打电话”心智图。

本章不建立连接，也不访问 STUN、TURN 或公网。

## 前置检查点

- 会阅读简单 C++ `main()`；
- 已安装 Visual Studio Community 2026 18.9.1 的“使用 C++ 的桌面开发”；
- 在仓库根目录打开 **Developer PowerShell for VS 2026**；Visual Studio 2022 也可以按同样方式操作，
  但本章的实际证据来自 VS 2026；
- 准备一个 vcpkg，并把它的根目录只放进当前终端变量：

```powershell
$env:VCPKG_ROOT = '<vcpkg-root>'
```

`<vcpkg-root>` 是占位符，必须替换成你电脑上真实包含 `vcpkg.exe` 的目录，尖括号也不能保留。
VS 2026 Developer PowerShell 可能先把 `VCPKG_ROOT` 指向 Visual Studio 自带的 vcpkg；如果你使用另一个
vcpkg，应当在打开 Developer PowerShell **之后**重新设置此变量，再用下面两行确认：

```powershell
Test-Path "$env:VCPKG_ROOT/vcpkg.exe"
& "$env:VCPKG_ROOT/vcpkg.exe" version
```

第一行必须输出 `True`。不要把个人绝对路径写进源码、CMake 或教程。普通 PowerShell 可能找不到
Windows SDK 的 `rc.exe`；出现这种情况时不要修改 CMake，换回 Developer PowerShell。

## 预计时间

60～90 分钟。首次下载或编译依赖的等待时间另计。

## 本章在最终项目中的位置

最终项目位于 [`tutorials/webrtc-minilab/`](../../../../tutorials/webrtc-minilab/)。本章只建立它的构建
骨架；最后的两个 PeerConnection、Offer/Answer 和 DataChannel 会在后续章节逐步加入。

## 本章知识点

- `find_package(... EXACT CONFIG)`；
- CMake imported target；
- `target_link_libraries(... PRIVATE ...)`；
- WebRTC、signaling、ICE 各自负责什么。

## 当前项目目录

本章开始时，学习目录还不存在。先验证工具和依赖：

```powershell
cmake --version
ninja --version
& "$env:VCPKG_ROOT/vcpkg.exe" version
& "$env:VCPKG_ROOT/vcpkg.exe" install libdatachannel:x64-windows
& "$env:VCPKG_ROOT/vcpkg.exe" list | Select-String '^libdatachannel:'
New-Item -ItemType Directory -Force 'out/learn-webrtc-minilab' | Out-Null
```

本教程当前实测组合为 Windows 11 build 22631、Visual Studio Community 2026 18.9.1、
CMake 3.27.7、Ninja 1.13.2、MSVC 19.51.36256 和
`libdatachannel:x64-windows 0.24.5`。若列表显示其他版本，切换到项目确认过的 vcpkg 基线，不要删除
`EXACT` 绕过检查。

## 步骤 1.1：从空目录建立可运行程序

### 当前问题

我们需要先证明“编译器、CMake、vcpkg 和运行时 DLL”能一起工作。若直接从 PeerConnection 开始，
一个配置错误就会被误判成 WebRTC 错误。

### 修改文件

- `out/learn-webrtc-minilab/CMakeLists.txt`（新文件）；
- `out/learn-webrtc-minilab/main.cpp`（新文件）。

### 代码修改

从空目录新增两个文件。新文件没有旧代码，因此本步只有黄色“新增”区块。

#### 1.1.1 建立项目和 C++ 标准

<div style="background-color:#fff3cd;border-left:5px solid #d6a700;padding:10px 14px;margin:10px 0;">
<strong>新增（黄色）：CMakeLists.txt 的项目骨架</strong>
<pre><code>cmake_minimum_required(VERSION 3.21)

project(WebRtcMiniLab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)</code></pre>
</div>

可直接复制：

```cmake
cmake_minimum_required(VERSION 3.21)

project(WebRtcMiniLab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
```

这段代码解决“CMake 应该用什么规则解释项目”的问题。`cmake_minimum_required(VERSION 3.21)` 的
`VERSION` 参数是最低 CMake 版本；它还会选择与 3.21 对应的 policy 行为。`project()` 的第一个参数是
项目名，`LANGUAGES CXX` 表示只启用 C++ 编译器，不额外探测 C 编译器。

三个 `set()` 的参数分别是变量名和值：标准固定为 C++17，`REQUIRED ON` 禁止编译器悄悄降级，
`EXTENSIONS OFF` 要求标准模式而不是 MSVC/GNU 扩展模式。它们影响随后创建的目标，不会安装任何库。

#### 1.1.2 精确查找 libdatachannel

<div style="background-color:#fff3cd;border-left:5px solid #d6a700;padding:10px 14px;margin:10px 0;">
<strong>新增（黄色）：依赖发现与失败门禁</strong>
<pre><code>find_package(LibDataChannel 0.24.5 EXACT CONFIG QUIET)
if(NOT LibDataChannel_FOUND)
    message(FATAL_ERROR "WebRtcMiniLab requires libdatachannel 0.24.5.")
endif()</code></pre>
</div>

可直接复制到上一块之后：

```cmake
find_package(LibDataChannel 0.24.5 EXACT CONFIG QUIET)
if(NOT LibDataChannel_FOUND)
    message(FATAL_ERROR
        "WebRtcMiniLab requires libdatachannel 0.24.5. "
        "Install that exact version for the target architecture and reconfigure."
    )
endif()
if(NOT TARGET LibDataChannel::LibDataChannel)
    message(FATAL_ERROR
        "libdatachannel 0.24.5 was found without the required "
        "LibDataChannel::LibDataChannel imported target."
    )
endif()
```

`find_package()` 的参数含义如下：

| 参数 | 含义 | 本项目为什么这样传 |
| --- | --- | --- |
| `LibDataChannel` | 包名，也是后续 `_FOUND` 变量前缀 | 与 vcpkg 安装的 config package 一致 |
| `0.24.5` | 需要的版本 | 与项目已验证版本一致 |
| `EXACT` | 不接受其他版本 | 避免教程 API 与本机库漂移 |
| `CONFIG` | 只使用包提供的 CMake config | 获得包声明的 imported target |
| `QUIET` | 查找失败时不先输出通用长错误 | 由下面的固定、脱敏错误统一说明 |

成功时，包会创建 imported target `LibDataChannel::LibDataChannel`。它不是源文件路径，而是一个携带
头文件目录、链接库和传递依赖的 CMake 目标。第一次 `if()` 检查包是否找到，第二次检查包契约是否
完整；任一失败都会由 `message(FATAL_ERROR ...)` 在**配置阶段**终止，程序尚未进入编译阶段。

#### 1.1.3 创建和链接可执行目标

<div style="background-color:#fff3cd;border-left:5px solid #d6a700;padding:10px 14px;margin:10px 0;">
<strong>新增（黄色）：MiniLab 目标与私有依赖</strong>
<pre><code>add_executable(webrtc_minilab main.cpp)
target_link_libraries(webrtc_minilab PRIVATE LibDataChannel::LibDataChannel)</code></pre>
</div>

可直接复制到依赖门禁之后：

```cmake
add_executable(webrtc_minilab main.cpp)
target_link_libraries(webrtc_minilab PRIVATE LibDataChannel::LibDataChannel)
```

`add_executable()` 的第一个参数是目标名，后续参数是属于该目标的源文件；`main.cpp` 相对于当前
`CMakeLists.txt` 所在目录解析，所以这两个文件必须放在同一个 MiniLab 源目录。`target_link_libraries()`
的第一个参数仍是目标名，`PRIVATE` 表示 libdatachannel 只服务于这个可执行文件，不向不存在的下游
目标传播。它同时影响编译期头文件可见性和链接期库选择；运行时 DLL 复制由下一块负责。

#### 1.1.4 打开警告并部署运行库

<div style="background-color:#fff3cd;border-left:5px solid #d6a700;padding:10px 14px;margin:10px 0;">
<strong>新增（黄色）：编译器警告与 Windows POST_BUILD</strong>
<pre><code>if(MSVC)
    target_compile_options(webrtc_minilab PRIVATE /W4 /permissive-)
endif()

add_custom_command(TARGET webrtc_minilab POST_BUILD ...)</code></pre>
</div>

可直接复制到目标链接之后：

```cmake
if(MSVC)
    target_compile_options(webrtc_minilab PRIVATE /W4 /permissive-)
else()
    target_compile_options(webrtc_minilab PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    add_custom_command(TARGET webrtc_minilab POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:webrtc_minilab>
            $<TARGET_FILE_DIR:webrtc_minilab>
        COMMAND_EXPAND_LISTS
        VERBATIM
    )
endif()
```

`target_compile_options()` 只给 `webrtc_minilab` 增加警告参数；`PRIVATE` 的传播含义与上一块一致。
`MSVC` 和 `WIN32` 是 CMake 在配置期提供的条件：前者判断编译器家族，后者判断目标平台。

`add_custom_command(TARGET ... POST_BUILD)` 在目标链接成功后运行。`${CMAKE_COMMAND} -E` 调用 CMake
自身的跨平台文件命令；`copy_if_different` 避免无变化时重复复制。两个 `$<...>` 是生成器表达式：
`TARGET_RUNTIME_DLLS` 在生成阶段展开为目标所需 DLL 列表，`TARGET_FILE_DIR` 展开为 exe 所在目录。
`COMMAND_EXPAND_LISTS` 把 DLL 列表拆成独立参数，`VERBATIM` 让 CMake 正确处理空格和引号。复制失败
会使构建失败，而不会静默留下一个缺 DLL 的 exe。

#### 1.1.5 写入程序入口

<div style="background-color:#fff3cd;border-left:5px solid #d6a700;padding:10px 14px;margin:10px 0;">
<strong>新增（黄色）：main.cpp 完整内容</strong>
<pre><code>#include &lt;iostream&gt;

int main()
{
    std::cout &lt;&lt; "miniwebrtc ready\n";
    return 0;
}</code></pre>
</div>

可直接复制为 `out/learn-webrtc-minilab/main.cpp` 的完整内容：

```cpp
#include <iostream>

int main()
{
    std::cout << "miniwebrtc ready\n";
    return 0;
}
```

`main()` 是进程入口，没有参数；返回 `0` 是操作系统可观察的成功退出码。`std::cout` 来自
`<iostream>`，`\n` 只追加换行，不强制每次刷新流。此时源码尚未调用 libdatachannel，但链接目标已经
证明头文件、库和 Windows 运行时依赖能被 CMake 正确解析。

执行链是 CMake 配置 → 生成 Ninja 规则 → 编译 `main.cpp` → 链接 imported target → 复制运行库 →
操作系统调用 `main()`。CMake target 拥有编译/链接规则，进程只拥有 `std::cout` 等普通运行时资源；
本章当前能力仅是构建与启动证明，尚未创建 PeerConnection、线程或网络会话。

本步执行链是：CMake 配置并验证包 → 生成 Ninja 规则 → MSVC 编译 `main.cpp` → 链接目标 → 复制 DLL
→ 程序输出固定文本并返回 0。现在仍没有 PeerConnection、线程回调或网络行为。

### 构建

```powershell
cmake -S 'out/learn-webrtc-minilab' `
  -B 'out/build-webrtc-minilab-learning' `
  -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build 'out/build-webrtc-minilab-learning'
```

这里 `-S` 指向包含本章 `CMakeLists.txt` 和 `main.cpp` 的**源目录**，`-B` 指向只保存缓存、对象文件和
exe 的**构建目录**。不要把源码写进 `out/build-webrtc-minilab-learning/`，也不要把 `-S` 指向另一个
Visual Studio 模板项目。

Windows 的 `POST_BUILD` 规则只把这个 MiniLab 需要的运行库复制到它自己的输出目录，不影响根工程。

### 运行或测试

```powershell
& 'out/build-webrtc-minilab-learning/webrtc_minilab.exe'
$LASTEXITCODE
```

### 观察证据

本仓库使用 VS2026 Developer PowerShell 实际重放该检查点时，CMake 识别到 MSVC 19.51.36256，
配置和 Ninja 构建成功，程序输出：

```text
miniwebrtc ready
0
```

### 通过条件

- 构建目录中存在 `webrtc_minilab.exe` 和所需运行库；
- 程序只输出 `miniwebrtc ready`；
- 退出码为 0；
- 根 CMake 和产品目标没有被修改。

## 原理和类比：电话、地址簿与道路

| WebRTC 概念 | 电话类比 | 职责 |
| --- | --- | --- |
| `PeerConnection` | 一部电话 | 管理一次端到端连接的一端 |
| signaling | 地址簿或传递联系卡的人 | 交换 Offer/Answer；WebRTC 不规定实现方式 |
| Offer / Answer | 邀请与应答 | 协商双方能共同使用的会话 |
| ICE | 尝试不同道路的导航 | 收集候选入口并检查哪条路真能通 |
| DataChannel | 接通后的加密纸条 | 传应用消息，不是视频 |
| Track / RTP | 通话里的声音或画面 | 承载实时媒体，尚未进入 MiniLab |

RTMP Server 像固定总机；P2P WebRTC 更像先互换联系卡，再尝试端到端道路。能送达一张 DataChannel
纸条，也不等于已经能发送 H.264 视频。

## 本章小实验

验证“精确依赖门禁”真的有效：不要破坏正常构建目录，另建一个负向目录并让 CMake 假装找不到包。

### 答案代码

```powershell
cmake -S 'out/learn-webrtc-minilab' `
  -B 'out/build-webrtc-minilab-negative' `
  -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows `
  -DCMAKE_DISABLE_FIND_PACKAGE_LibDataChannel=TRUE
$negativeExit = $LASTEXITCODE

cmake -S 'out/learn-webrtc-minilab' `
  -B 'out/build-webrtc-minilab-learning' `
  -DCMAKE_DISABLE_FIND_PACKAGE_LibDataChannel=FALSE
cmake --build 'out/build-webrtc-minilab-learning'
& 'out/build-webrtc-minilab-learning/webrtc_minilab.exe'
```

### 实验验证

负向配置应以非 0 退出，提示需要精确的 0.24.5；提示中不应泄露包的本机安装路径。恢复配置后程序
再次输出 `miniwebrtc ready` 并返回 0。本仓库实测负向配置返回 1，恢复后返回 0。

### 实验通过条件

- `$negativeExit -ne 0`；
- 错误只给固定修复方向，没有自动下载依赖；
- 正常构建目录仍可成功运行。

## 故障恢复

| 现象 | 原因 | 恢复 |
| --- | --- | --- |
| 找不到编译器或 `rc.exe` | 没使用 VS Developer PowerShell | 从 VS 2026 工具菜单重新打开 Developer PowerShell |
| 找不到 Ninja | 工具不在 `PATH` | 安装 Ninja 或 VS 的 CMake 工具并重新开终端 |
| 找不到 libdatachannel | toolchain、triplet 或环境变量错误 | 核对 `$env:VCPKG_ROOT` 和 `x64-windows` |
| 版本不是 0.24.5 | vcpkg 基线不同 | 切换依赖基线，不删除 `EXACT` |
| 缺 DLL | 单独移动了 exe | 从构建目录运行，或重新执行构建 |

## 当前完整目录结构

```text
out/
├─ learn-webrtc-minilab/
│  ├─ CMakeLists.txt
│  └─ main.cpp
└─ build-webrtc-minilab-learning/
   └─ webrtc_minilab.exe + 运行库
```

## 本章检查点

- [ ] 依赖列表显示 libdatachannel 0.24.5。
- [ ] 正向配置、构建、运行均成功。
- [ ] 负向配置能在配置期脱敏失败，恢复后重新成功。
- [ ] 能解释 signaling 交换联系信息，ICE 寻找可用道路。

## 本章总结

你建立了一个与产品隔离的 C++17 工程，并用精确版本和 imported target 固定依赖。程序暂时只有一行
输出，这正是第一个可靠检查点：后续错误可以明确归因于新加入的 WebRTC 代码。

## 下一章将解决什么问题

一部电话不能完成通话。下一章会创建端点 A、B，观察回调，并学习为什么异步关闭顺序不能靠猜。

- 下一章：[第 2 章：创建两个 PeerConnection](02_sdp_offer_answer_and_manual_signaling.md)
- 教程首页：[WebRTC V2 文档](../README.md)
