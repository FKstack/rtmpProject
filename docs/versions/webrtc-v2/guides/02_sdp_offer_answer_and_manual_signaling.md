# 第 2 章：创建两个 PeerConnection

上一章搭好了“电话工厂”，现在先造一部电话，再造第二部。我们只观察创建和关闭，不进行 Offer/Answer，
这样可以把对象生命周期与网络协商分开理解。

## 本章完成后你将得到什么

- 端点 A、B 各自拥有一个真实 `rtc::PeerConnection`；
- 显式空 ICE server 配置，不访问 STUN/TURN；
- 线程安全的固定字段状态输出；
- 明确释放资源并调用 `rtc::Cleanup()` 的最小程序。

## 前置检查点

- 第 1 章程序能输出 `miniwebrtc ready`；
- 当前终端仍是 VS 2026 Developer PowerShell；VS 2022 用户使用同名开发者终端即可；
- 学习源码位于 `out/learn-webrtc-minilab/`。

## 预计时间

45～60 分钟。

## 本章在最终项目中的位置

最终 MiniLab 的两端都由同一个控制线程创建和持有。本章先建立这个所有权；第 3 章才把 Offer/Answer
和 ICE gathering 接上去。

## 本章知识点

- `rtc::Configuration`；
- `rtc::PeerConnection`；
- `onStateChange()`、`onGatheringStateChange()`；
- `rtc::Cleanup()` 与本章尚未解决的无界等待。

## 当前项目目录

```text
out/learn-webrtc-minilab/
├─ CMakeLists.txt   # 本章不改
└─ main.cpp        # 当前只输出 miniwebrtc ready
```

## 步骤 2.1：先创建和关闭端点 A

### 当前问题

一个 `PeerConnection` 代表连接的一端。先只建 A，可以看清配置、回调和释放顺序，不让第二端的异步
输出干扰观察。

### 修改文件

`out/learn-webrtc-minilab/main.cpp`

### 代码修改

本步文件仍很短，直接完整替换最不容易漏掉头文件或命名空间。

<div style="background-color:#fde2e2;border-left:5px solid #c62828;padding:10px 14px;margin:10px 0;">
<strong>删除（红色）：第 1 章只输出 ready 的入口</strong>
<pre><code>#include &lt;iostream&gt;

int main()
{
    std::cout &lt;&lt; "miniwebrtc ready\n";
    return 0;
}</code></pre>
</div>

<div style="background-color:#fff3cd;border-left:5px solid #d6a700;padding:10px 14px;margin:10px 0;">
<strong>新增（黄色）：PeerConnection A、状态回调和显式清理</strong>
<pre><code>rtc::Configuration configuration;
auto endpointA = std::make_shared&lt;rtc::PeerConnection&gt;(configuration);
endpointA-&gt;onStateChange(...);
endpointA-&gt;close();
rtc::Cleanup().wait();</code></pre>
</div>

可直接复制为 `main.cpp` 的完整内容：

```cpp
#include <rtc/rtc.hpp>

#include <iostream>
#include <memory>
#include <string>

namespace {

const char *stateName(rtc::PeerConnection::State state)
{
    switch (state) {
    case rtc::PeerConnection::State::New: return "new";
    case rtc::PeerConnection::State::Connecting: return "connecting";
    case rtc::PeerConnection::State::Connected: return "connected";
    case rtc::PeerConnection::State::Disconnected: return "disconnected";
    case rtc::PeerConnection::State::Failed: return "failed";
    case rtc::PeerConnection::State::Closed: return "closed";
    }
    return "unknown";
}

} // namespace

int main()
{
    rtc::InitLogger(rtc::LogLevel::None);
    int exitCode = 0;
    try {
        rtc::Configuration configuration;
        configuration.iceServers.clear();
        configuration.disableAutoNegotiation = true;
        configuration.enableIceTcp = false;

        auto endpointA = std::make_shared<rtc::PeerConnection>(configuration);
        endpointA->onStateChange([](rtc::PeerConnection::State state) {
            std::cout << "endpoint=A stage=connection state="
                      << stateName(state) << '\n';
        });
        std::cout << "endpoint=A stage=created state=new\n";
        endpointA->close();
        endpointA.reset();
    } catch (...) {
        std::cout << "endpoint=A stage=runtime state=library_failure\n";
        exitCode = 1;
    }

    rtc::Cleanup().wait();
    std::cout << "endpoint=A stage=complete state="
              << (exitCode == 0 ? "ok" : "failed") << '\n';
    return exitCode;
}
```

#### 自定义函数：`stateName()`

| 项目 | 说明 |
| --- | --- |
| 参数 | `rtc::PeerConnection::State state`，按值传入一个很小的枚举 |
| 返回值 | 指向静态字符串字面量的 `const char *`，调用者不释放 |
| 状态修改 | 无；它是纯格式化函数 |
| 调用者 | `onStateChange()` 注册的回调 |
| 失败方式 | 未识别枚举返回固定 `unknown`，不抛出异常 |

它把库枚举转换成固定允许列表，避免把库内部异常或网络材料直接写入输出。匿名命名空间让函数只在
这个源文件可见，不创建公共接口。

#### API：`rtc::Configuration`

`rtc::Configuration` 来自 libdatachannel 0.24.5 的 `<rtc/rtc.hpp>`，构造后按值传给
`PeerConnection`。本章只设置三项：

| 字段 | 当前值 | 原理 |
| --- | --- | --- |
| `iceServers` | `clear()` 后为空 | 不访问 STUN/TURN；ICE 仍会收集本机 host candidate |
| `disableAutoNegotiation` | `true` | 后续由控制线程明确调用 `setLocalDescription()` |
| `enableIceTcp` | `false` | 本实验只验证 UDP host 路径，缩小可变因素 |

空 ICE server 不等于关闭 ICE，也不等于能穿越公网 NAT。配置对象由 `main()` 持有；PeerConnection
构造时消费其配置值，后续回调不会借用这个局部变量。

#### API：`PeerConnection`、`onStateChange()` 与 `close()`

```cpp
auto endpointA = std::make_shared<rtc::PeerConnection>(configuration);
endpointA->onStateChange(callback);
endpointA->close();
```

`std::make_shared` 创建对象和引用计数控制块，返回共享所有权句柄。`onStateChange()` 的参数是一个
接收 `State` 的回调；库可在内部线程调用它，所以回调不能假定与 `main()` 同步，也不能捕获即将失效
的局部引用。本步 lambda 没有捕获对象，只调用无状态的 `stateName()` 和输出流。

`close()` 请求关闭端点，返回 `void`；状态通知仍可能异步到达。随后 `reset()` 放弃本地共享所有权。
重复关闭的系统化保证要到第 5 章才实现，本章只调用一次。

#### API：`rtc::Cleanup()`

`rtc::Cleanup()` 启动 libdatachannel 全局后台资源清理并返回一个 future-like 对象；`.wait()` 阻塞到
清理结束。本章先用最直接写法建立生命周期顺序，但它没有本地截止时间：如果库无法完成清理，等待
可能没有上限。第 5 章会改用 `wait_for()` 和固定错误分类。

本步执行链是：控制线程创建配置和 A → 注册回调 → 请求关闭 → 释放最后一个本地句柄 → 等待库全局
清理。库线程只负责报告状态，不决定控制流程。构造、回调注册或关闭抛出的异常被转换为固定
`library_failure`，原始异常文本不会输出。

### 构建

```powershell
cmake --build 'out/build-webrtc-minilab-learning'
```

### 运行或测试

```powershell
& 'out/build-webrtc-minilab-learning/webrtc_minilab.exe'
```

### 观察证据

实际重放结果包含：

```text
endpoint=A stage=created state=new
endpoint=A stage=connection state=closed
endpoint=A stage=complete state=ok
```

`onStateChange()` 注册通知函数；通知可能由库线程执行，不保证与 `main()` 中的相邻语句同步排序。

### 通过条件

- A 被创建并最终报告 `closed`；
- 程序最后输出 `complete state=ok`，退出码为 0；
- 没有 gathering 或 connected 输出，因为还没有协商。

## 步骤 2.2：加入端点 B 和统一状态输出

### 当前问题

一部电话没有对端。加入 B 后，A/B 回调可能并发写终端；还要把两端所有权集中到一个会话对象，才能
让关闭流程只存在一处。

### 修改文件

`out/learn-webrtc-minilab/main.cpp`

### 代码修改

这次修改分为三个可复制单元；全部完成后再构建。每个单元都以稳定函数或命名空间为锚点。

#### 2.2.1 加入 gathering 名称和线程安全输出

<div style="background-color:#fde2e2;border-left:5px solid #c62828;padding:10px 14px;margin:10px 0;">
<strong>删除（红色）：回调直接向 std::cout 拼接多个字段</strong>
<pre><code>std::cout &lt;&lt; "endpoint=A stage=connection state="
          &lt;&lt; stateName(state) &lt;&lt; '\n';</code></pre>
</div>

<div style="background-color:#fff3cd;border-left:5px solid #d6a700;padding:10px 14px;margin:10px 0;">
<strong>新增（黄色）：单一 emitState() 输出入口</strong>
<pre><code>std::mutex outputMutex;
void emitState(int round, const std::string &amp;endpoint,
               const char *stage, const char *state);</code></pre>
</div>

在 `<memory>` 后加入 `<mutex>`；然后在 `stateName()` 后、`MiniLabSession` 前复制：

```cpp
const char *gatheringName(rtc::PeerConnection::GatheringState state)
{
    switch (state) {
    case rtc::PeerConnection::GatheringState::New: return "new";
    case rtc::PeerConnection::GatheringState::InProgress: return "in_progress";
    case rtc::PeerConnection::GatheringState::Complete: return "complete";
    }
    return "unknown";
}

void emitState(int round, const std::string &endpoint,
               const char *stage, const char *state)
{
    const std::lock_guard<std::mutex> lock(outputMutex);
    std::cout << "round=" << round
              << " endpoint=" << endpoint
              << " stage=" << stage
              << " state=" << state << '\n';
}
```

并在匿名命名空间开头、`stateName()` 前加入：

```cpp
std::mutex outputMutex;
```

`gatheringName()` 与 `stateName()` 职责相同，只格式化 gathering 枚举。`emitState()` 的 `round` 按值
传入；`endpoint` 以 `const std::string &` 借用，调用期间必须有效；`stage/state` 指向静态字符串，
函数不取得所有权。`std::lock_guard` 构造时锁住全局 mutex，离开作用域自动解锁，即使输出抛异常也
不会忘记解锁。它只保证**一行不被两个回调交错写坏**，不保证 A/B 回调先后顺序。

#### 2.2.2 把 A/B 所有权收进会话对象

<div style="background-color:#fde2e2;border-left:5px solid #c62828;padding:10px 14px;margin:10px 0;">
<strong>删除（红色）：main() 中只持有局部 endpointA</strong>
<pre><code>auto endpointA = std::make_shared&lt;rtc::PeerConnection&gt;(configuration);
endpointA-&gt;close();
endpointA.reset();</code></pre>
</div>

<div style="background-color:#fff3cd;border-left:5px solid #d6a700;padding:10px 14px;margin:10px 0;">
<strong>新增（黄色）：MiniLabSession 同时拥有 A/B</strong>
<pre><code>std::shared_ptr&lt;rtc::PeerConnection&gt; endpointA_;
std::shared_ptr&lt;rtc::PeerConnection&gt; endpointB_;</code></pre>
</div>

把原来的 `} // namespace` 下移到这个类之后，并复制完整类：

```cpp
class MiniLabSession final
{
public:
    ~MiniLabSession() { close(); }

    bool run(int round)
    {
        round_ = round;
        rtc::Configuration configuration;
        configuration.iceServers.clear();
        configuration.disableAutoNegotiation = true;
        configuration.enableIceTcp = false;

        endpointA_ = makePeer(configuration, "A");
        endpointB_ = makePeer(configuration, "B");
        return true;
    }

    void close() noexcept
    {
        if (closed_) return;
        closed_ = true;
        try { if (endpointA_) endpointA_->close(); } catch (...) {}
        try { if (endpointB_) endpointB_->close(); } catch (...) {}
        endpointA_.reset();
        endpointB_.reset();
    }

private:
    std::shared_ptr<rtc::PeerConnection> makePeer(
        const rtc::Configuration &configuration,
        const std::string &endpoint)
    {
        auto peer = std::make_shared<rtc::PeerConnection>(configuration);
        peer->onStateChange([round = round_, endpoint](
                                rtc::PeerConnection::State state) {
            emitState(round, endpoint, "connection", stateName(state));
        });
        peer->onGatheringStateChange(
            [round = round_, endpoint](
                rtc::PeerConnection::GatheringState state) {
                emitState(round, endpoint, "gathering", gatheringName(state));
            });
        emitState(round_, endpoint, "created", "new");
        return peer;
    }

    int round_ = 0;
    bool closed_ = false;
    std::shared_ptr<rtc::PeerConnection> endpointA_;
    std::shared_ptr<rtc::PeerConnection> endpointB_;
};
```

`run(round)` 保存本轮编号并创建两端；当前没有可能返回 `false` 的业务分支，布尔返回值只是为后续
步骤保留统一调用形状。`makePeer(configuration, endpoint)` 借用配置和端点名，在函数内创建并返回
共享句柄；返回后 session 成为 A/B 的控制线程所有者。

两个回调都按值捕获 `round` 和 `endpoint`。即使 `run()` 已返回，回调仍拥有自己的字符串副本，不会
引用悬空局部变量。`onGatheringStateChange()` 的参数是 gathering 枚举；本章没有创建 Offer，因此
它通常不会进入 `InProgress/Complete`。

`close() noexcept` 用 `closed_` 做最小幂等保护：析构函数再次调用时立即返回。它先请求关闭再释放
句柄；异常被吞掉是因为关闭路径不能让析构异常逃出。第 5 章会进一步先失效回调、再关闭对象。

#### 2.2.3 让 main() 只负责组合和全局 Cleanup

<div style="background-color:#fde2e2;border-left:5px solid #c62828;padding:10px 14px;margin:10px 0;">
<strong>删除（红色）：main() 直接创建和关闭 PeerConnection A</strong>
<pre><code>rtc::Configuration configuration;
auto endpointA = std::make_shared&lt;rtc::PeerConnection&gt;(configuration);</code></pre>
</div>

<div style="background-color:#fff3cd;border-left:5px solid #d6a700;padding:10px 14px;margin:10px 0;">
<strong>新增（黄色）：main() 使用 MiniLabSession</strong>
<pre><code>MiniLabSession session;
session.run(1);
session.close();</code></pre>
</div>

用下面内容完整替换 `main()`：

```cpp
int main()
{
    rtc::InitLogger(rtc::LogLevel::None);
    int exitCode = 0;
    try {
        MiniLabSession session;
        if (!session.run(1)) exitCode = 1;
        session.close();
    } catch (...) {
        emitState(1, "none", "failed", "library_failure");
        exitCode = 1;
    }

    rtc::Cleanup().wait();
    emitState(1, "both", "complete",
              exitCode == 0 ? "ok" : "failed");
    return exitCode;
}
```

`main()` 是组合根：它决定 session 的作用域、错误转译和全局 Cleanup 时机，但不再了解 A/B 回调
细节。session 在离开 `try` 作用域前已经关闭并析构，因此 `Cleanup()` 不会与仍由控制线程持有的
PeerConnection 竞争。`rtc::InitLogger(LogLevel::None)` 关闭库原生日志，防止教程输出混入地址、
candidate 或原始异常；应用自己的固定字段输出仍保留。

完整执行链是：`main()` 创建 session → `run()` 创建 A/B 并注册回调 → 库线程报告状态 → 控制线程
调用 `close()` → A/B 释放 → 全局 Cleanup。当前仍没有 Offer/Answer，也不能 Connected。

### 构建

```powershell
cmake --build 'out/build-webrtc-minilab-learning'
```

### 运行或测试

```powershell
& 'out/build-webrtc-minilab-learning/webrtc_minilab.exe'
```

### 观察证据

本检查点实际输出包含 A/B 各一次 `created`、各一次 `closed`，最后为：

```text
round=1 endpoint=both stage=complete state=ok
```

A/B 的关闭回调顺序可以变化；`emitState()` 的互斥量只保证单行不交错，不把异步回调变成同步流程。

### 通过条件

- A、B 都被创建和关闭；
- 每行输出完整，不出现两个回调拼在同一行；
- 不把 A/B 的关闭先后顺序写成断言；
- 程序退出码为 0。

## 执行链与当前限制

```text
控制线程：Configuration → create A → create B → close A/B → release → Cleanup
库回调：               state change ────────────────┘
```

当前只有 `New → Closed`。没有 Offer/Answer，就没有 gathering、ICE connectivity check 或 Connected。

## 本章小实验

连续运行三次。在运行前先预测：哪些计数必须稳定，哪些顺序不应该被测试锁死？

### 答案代码

```powershell
$exe = 'out/build-webrtc-minilab-learning/webrtc_minilab.exe'
1..3 | ForEach-Object {
    $text = (& $exe 2>&1 | Out-String)
    if ($LASTEXITCODE -ne 0) { throw "run failed" }
    if (($text | Select-String 'endpoint=A stage=created').Count -ne 1) { throw 'A create' }
    if (($text | Select-String 'endpoint=B stage=created').Count -ne 1) { throw 'B create' }
    if (($text | Select-String 'endpoint=A stage=connection state=closed').Count -ne 1) { throw 'A close' }
    if (($text | Select-String 'endpoint=B stage=connection state=closed').Count -ne 1) { throw 'B close' }
    if (($text | Select-String 'endpoint=both stage=complete state=ok').Count -ne 1) { throw 'complete' }
}
'ordering experiment: ok'
```

### 实验验证

本仓库实测三次均满足五个计数。创建调用固定为 A 后 B；不同端点的异步关闭顺序不属于契约。

### 实验通过条件

- 三次运行都返回 0；
- 每次 A/B 的创建和关闭各出现一次；
- 不比较 A 与 B 的异步关闭先后。

## 故障恢复

| 现象 | 先检查 | 恢复 |
| --- | --- | --- |
| 找不到 `rtc/rtc.hpp` | 第 1 章是否用 vcpkg toolchain 配置 | 重新配置学习构建目录 |
| 回调行交错 | 是否绕过 `emitState()` 直接输出 | 所有状态行都经同一互斥量 |
| 没有 gathering | 尚未创建 Offer | 这是本章正确现象 |
| Cleanup 不退出 | 对象仍存活且等待无上限 | 先释放 A/B；第 5 章加入超时 |

## 当前完整目录结构

目录结构不变，`main.cpp` 已增长为双端点生命周期检查点：

```text
out/learn-webrtc-minilab/
├─ CMakeLists.txt
└─ main.cpp
```

## 本章检查点

- [ ] 一个 PeerConnection 只代表一端。
- [ ] 空 ICE server 不等于禁用 ICE。
- [ ] 回调顺序不作为控制流程。
- [ ] A/B 均关闭，最终成功退出。
- [ ] 知道本章 `Cleanup().wait()` 还没有超时。

## 本章总结

你创建了两部真实“电话”，但还没有交换联系卡。控制线程拥有对象，库线程只通过回调报告状态；这个
边界会成为后面所有安全等待和关闭设计的基础。

## 下一章将解决什么问题

下一章让 A 发 Offer、B 回 Answer，并等待 ICE gathering complete 和双方 Connected。

- 上一章：[第 1 章](01_webrtc_from_rtmp.md)
- 下一章：[第 3 章](03_ice_stun_turn_and_public_networks.md)
