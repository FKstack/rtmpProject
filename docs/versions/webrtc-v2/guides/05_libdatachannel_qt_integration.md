# 第 5 章：异步等待与安全退出

第四章已经能成功一次，但可靠程序还要回答四个问题：失败如何稳定分类？A/B 重复状态如何统一？晚到
回调如何失效？参数和全局清理如何有界？本章按这个顺序重构，每步都再次执行同一个 `ping → pong`。

## 本章完成后你将得到什么

- 固定 `Error` 枚举和允许列表输出；
- A/B 通过 side index 使用同一套状态与 channel helper；
- weak state + generation + closing 的晚到回调门禁；
- 幂等 `close()`、10 秒有界 Cleanup；
- 无参数、`--repeat=N`、`--help` 和固定退出码。

## 前置检查点

- 第 4 章正常版本输出 `ping_pong state=ok`；
- `hello` 负向实验已恢复为 `ping`；
- 能说明控制线程推进流程，库回调只改状态。

## 预计时间

90～120 分钟。

## 本章在最终项目中的位置

本章结束时 `main.cpp` 与最终 MiniLab 完全一致。第 6 章只给 CMake 加测试，不再改变运行时逻辑。

## 本章知识点

- weak state、generation 和 closing；
- `resetCallbacks()`、幂等 close 与有界 `rtc::Cleanup()`；
- `std::from_chars()` 的无异常整数解析；
- 一个回合只使用一个 `steady_clock` deadline。

## 当前项目目录

```text
out/learn-webrtc-minilab/
├─ CMakeLists.txt
└─ main.cpp   # 第 4 章 ping/pong 检查点
```

## 步骤 5.1：把散落字符串收进固定错误分类

### 当前问题

字符串容易拼错，也容易诱使代码把原始异常、SDP 或 payload 当“更多细节”输出。错误类型应先在内部
收敛，日志只做固定映射。

### 修改文件

`out/learn-webrtc-minilab/main.cpp`

### 代码修改

这是纯重构：把自由字符串变成有限枚举，运行行为必须保持 `ping → pong` 成功。

<div style="background-color:#fde2e2;border-left:5px solid #c62828;padding:10px 14px;margin:10px 0;">
<strong>删除（红色）：状态中保存任意错误字符串</strong>
<pre><code>const char *error = "message_timeout";
fail("offer_gathering", "gathering_timeout");</code></pre>
</div>

<div style="background-color:#fff3cd;border-left:5px solid #d6a700;padding:10px 14px;margin:10px 0;">
<strong>新增（黄色）：固定 Error 枚举和唯一格式化函数</strong>
<pre><code>enum class Error { None, Timeout, ConnectionFailed,
                   ProtocolMismatch, InvalidState };
const char *errorName(Error error) noexcept;</code></pre>
</div>

在 timeout 常量之后复制：

```cpp
enum class Error {
    None,
    Timeout,
    ConnectionFailed,
    ProtocolMismatch,
    InvalidState,
};

const char *errorName(Error error) noexcept
{
    switch (error) {
    case Error::None: return "none";
    case Error::Timeout: return "timeout";
    case Error::ConnectionFailed: return "connection_failed";
    case Error::ProtocolMismatch: return "protocol_mismatch";
    case Error::InvalidState: return "invalid_state";
    }
    return "invalid_state";
}
```

`enum class` 不会把枚举值隐式转换成整数，也不会把 `None` 等名字泄漏到外层命名空间。`errorName()`
按值接收枚举，返回静态字符串，不分配内存；`noexcept` 声明它不会抛异常，因此错误输出路径本身不
引入新异常。default fallback 仍是固定允许值，不把未知整数写入日志。

把 `SharedState::error` 改成：

```cpp
Error error = Error::Timeout;
```

并把所有错误赋值/调用改为枚举，例如：

```cpp
live->error = Error::ProtocolMismatch;
state->error = Error::ConnectionFailed;
return fail("offer_gathering", Error::Timeout);
if (!offer) return fail("offer_exchange", Error::InvalidState);
```

用下面两个完整函数替换旧版本：

```cpp
bool fail(const char *stage, Error error)
{
    emitState(round_, "both", stage, errorName(error));
    return false;
}

Error stateError()
{
    const std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->error;
}
```

`stage` 仍是调用点选择的固定字符串，`error` 则只能来自枚举。`stateError()` 持锁复制一个小枚举值，
返回后不再借用状态内存。调用链从“任意字符串穿过多层”变成“内部枚举 → 唯一 errorName() → 固定
输出”，这为后面安全合并错误路径做准备。

本步没有改变 PeerConnection、DataChannel、等待或关闭顺序；构建后必须运行与第 4 章相同的测试，
证明纯重构前后成功输出一致。

### 构建

```powershell
cmake --build 'out/build-webrtc-minilab-learning'
```

### 运行或测试

```powershell
& 'out/build-webrtc-minilab-learning/webrtc_minilab.exe'
```

### 观察证据

这是纯重构。实际重放前后都以 `ping_pong state=ok` 和 `complete state=ok` 结束，退出码仍为 0。

### 通过条件

- 主线行为不变；
- 错误只来自 `Error` → `errorName()`；
- 不增加原始异常输出。

## 步骤 5.2：用 side index 统一 A/B 状态

### 当前问题

第 4 章为 A/B、Offer/Answer 分别维护字段和成员名。两端执行的是同一套策略，继续复制分支会让后续
回调失效和关闭顺序出现两份实现。本步只把角色名映射为稳定的 side：A 固定为 0，B 固定为 1。

### 修改文件

`out/learn-webrtc-minilab/main.cpp`

### 代码修改

这是纯结构重构。完成所有小块后再构建；成功行为仍必须是同一轮 `ping → pong`。

#### 5.2.1 把成对布尔字段改为数组

<div style="background-color:#fde2e2;border-left:5px solid #c62828;padding:10px 14px;margin:10px 0;">
<strong>删除（红色）：按角色命名的八个布尔字段</strong>
<pre><code>bool offerGathered;
bool answerGathered;
bool offerConnected;
bool answerConnected;</code></pre>
</div>

<div style="background-color:#fff3cd;border-left:5px solid #d6a700;padding:10px 14px;margin:10px 0;">
<strong>新增（黄色）：由 side 索引的四组状态</strong>
<pre><code>bool gathered[2] {false, false};
bool connected[2] {false, false};
bool channelOpen[2] {false, false};
bool messageReceived[2] {false, false};</code></pre>
</div>

用下面完整结构替换 `SharedState`：

```cpp
struct SharedState
{
    std::mutex mutex;
    std::condition_variable changed;
    bool gathered[2] {false, false};
    bool connected[2] {false, false};
    bool channelOpen[2] {false, false};
    bool messageReceived[2] {false, false};
    bool failed = false;
    Error error = Error::Timeout;
    std::shared_ptr<rtc::DataChannel> incomingChannel;
};
```

数组长度固定为 2，因为本 MiniLab 的教学边界就是两个端点，不是可变 peer 集合。side 约定在本章
冻结为 `0=A/Offerer`、`1=B/Answerer`。每个元素仍由同一个 mutex 保护；数组不会改变线程模型，也
不会把信令角色变成媒体方向。

`gathered[side]`、`connected[side]` 和 `channelOpen[side]` 表示该端点的状态；消息是“被哪一端
接收”，所以 `messageReceived[1]` 表示 B 收到 ping，`messageReceived[0]` 表示 A 收到 pong。

#### 5.2.2 统一 PeerConnection 和 DataChannel 成员

<div style="background-color:#fde2e2;border-left:5px solid #c62828;padding:10px 14px;margin:10px 0;">
<strong>删除（红色）：四个按名称区分的成员</strong>
<pre><code>endpointA_
endpointB_
offerChannel_
answerChannel_</code></pre>
</div>

<div style="background-color:#fff3cd;border-left:5px solid #d6a700;padding:10px 14px;margin:10px 0;">
<strong>新增（黄色）：两个固定长度句柄数组</strong>
<pre><code>std::shared_ptr&lt;rtc::PeerConnection&gt; peers_[2];
std::shared_ptr&lt;rtc::DataChannel&gt; channels_[2];</code></pre>
</div>

把 `run()` 中的对象创建和 Offer/Answer 使用点按下面的完整片段替换：

```cpp
peers_[0] = makePeer(configuration, "A", 0);
peers_[1] = makePeer(configuration, "B", 1);

const std::weak_ptr<SharedState> weakState(state_);
peers_[1]->onDataChannel(
    [weakState](std::shared_ptr<rtc::DataChannel> channel) {
        const auto state = weakState.lock();
        if (!state) return;
        channel->onOpen([weakState] {
            const auto live = weakState.lock();
            if (!live) return;
            const std::lock_guard<std::mutex> lock(live->mutex);
            live->channelOpen[1] = true;
            live->changed.notify_all();
        });
        channel->onMessage([weakState](rtc::message_variant value) {
            const auto *text = std::get_if<std::string>(&value);
            const auto live = weakState.lock();
            if (!live) return;
            const std::lock_guard<std::mutex> lock(live->mutex);
            if (text == nullptr || *text != "ping") {
                live->failed = true;
                live->error = Error::ProtocolMismatch;
            } else {
                live->messageReceived[1] = true;
            }
            live->changed.notify_all();
        });
        const std::lock_guard<std::mutex> lock(state->mutex);
        state->incomingChannel = std::move(channel);
        state->changed.notify_all();
    });

channels_[0] = peers_[0]->createDataChannel("minilab");
```

`peers_[side]` 和 `channels_[side]` 都由控制线程的 session 拥有。回调只通过 weak state 写状态，不
读取这些成员数组，因此不会从库线程竞争修改句柄。`incomingChannel` 仍是 B 回调到控制线程之间的
临时交接槽，下一步骤才把两端通道注册统一到 helper。

把 Offer/Answer 和消息阶段的名称机械替换为以下索引写法：

```cpp
peers_[0]->setLocalDescription(rtc::Description::Type::Offer);
const auto offer = peers_[0]->localDescription();
peers_[1]->setRemoteDescription(
    rtc::Description(offer->generateSdp(), "offer"));
peers_[1]->setLocalDescription(rtc::Description::Type::Answer);

const auto answer = peers_[1]->localDescription();
peers_[0]->setRemoteDescription(
    rtc::Description(answer->generateSdp(), "answer"));

channels_[1] = std::move(state_->incomingChannel);
(void)channels_[0]->send(std::string("ping"));
(void)channels_[1]->send(std::string("pong"));
```

这些替换不改变 description 的方向：0 创建 Offer，1 创建 Answer；也不改变消息方向：0 发 ping，
1 发 pong。只把相同策略从两套名字压缩成一套索引。

#### 5.2.3 让 makePeer() 只依赖 side

<div style="background-color:#fde2e2;border-left:5px solid #c62828;padding:10px 14px;margin:10px 0;">
<strong>删除（红色）：offerSide 条件分支</strong>
<pre><code>if (offerSide) state-&gt;offerConnected = true;
else state-&gt;answerConnected = true;</code></pre>
</div>

<div style="background-color:#fff3cd;border-left:5px solid #d6a700;padding:10px 14px;margin:10px 0;">
<strong>新增（黄色）：状态直接写入对应 side</strong>
<pre><code>state-&gt;connected[side] = true;
state-&gt;gathered[side] = true;</code></pre>
</div>

用下面完整函数替换 `makePeer()`：

```cpp
std::shared_ptr<rtc::PeerConnection> makePeer(
    const rtc::Configuration &configuration,
    const std::string &endpoint,
    int side)
{
    auto peer = std::make_shared<rtc::PeerConnection>(configuration);
    const std::weak_ptr<SharedState> weakState(state_);
    peer->onStateChange([weakState, round = round_, endpoint, side](
                            rtc::PeerConnection::State value) {
        const auto state = weakState.lock();
        if (!state) return;
        {
            const std::lock_guard<std::mutex> lock(state->mutex);
            if (value == rtc::PeerConnection::State::Connected) {
                state->connected[side] = true;
            } else if (value == rtc::PeerConnection::State::Failed) {
                state->failed = true;
                state->error = Error::ConnectionFailed;
            }
            state->changed.notify_all();
        }
        emitState(round, endpoint, "connection", stateName(value));
    });
    peer->onGatheringStateChange(
        [weakState, round = round_, endpoint, side](
            rtc::PeerConnection::GatheringState value) {
            const auto state = weakState.lock();
            if (!state) return;
            {
                const std::lock_guard<std::mutex> lock(state->mutex);
                if (value == rtc::PeerConnection::GatheringState::Complete) {
                    state->gathered[side] = true;
                }
                state->changed.notify_all();
            }
            emitState(round, endpoint, "gathering", gatheringName(value));
        });
    emitState(round_, endpoint, "created", "new");
    return peer;
}
```

`side` 按值捕获，回调到达时不依赖 `makePeer()` 的栈。它只能是 0 或 1；调用点固定传常量，不接受
CLI 输入。`endpoint` 仍只用于固定输出，状态策略完全由 side 决定。

把成员区改为：

```cpp
int round_ = 0;
bool closed_ = false;
std::shared_ptr<SharedState> state_ = std::make_shared<SharedState>();
std::shared_ptr<rtc::PeerConnection> peers_[2];
std::shared_ptr<rtc::DataChannel> channels_[2];
```

`close()` 中依次关闭 `channels_[1]`、`channels_[0]`、临时 incoming，再关闭
`peers_[0]`、`peers_[1]` 并 reset 两个 peer。这个中间检查点仍沿用第 4 章关闭模型；回调失效顺序
将在步骤 5.4 修正。

### 构建

```powershell
cmake --build 'out/build-webrtc-minilab-learning'
```

### 运行或测试

```powershell
& 'out/build-webrtc-minilab-learning/webrtc_minilab.exe'
```

### 观察证据

输出仍包含双方 Connected、DataChannel open 和 `ping_pong=ok`，退出码为 0。重构前后的稳定行为
相同；A/B 回调行的相对顺序仍不属于契约。

### 通过条件

- 所有成对状态都通过 side 数组访问；
- side 只来自内部常量 0/1；
- Offer/Answer 和 ping/pong 方向不变；
- 与步骤 5.1 使用相同命令运行成功。

## 步骤 5.3：统一 DataChannel helper、结果类型和有界 Cleanup

### 当前问题

side index 已去掉 A/B 状态分支，但通道回调、异常转译、运行结果和 Cleanup 仍散落在 `run()/main()`。
本步把同一策略集中到 helper，并让一次运行和全局清理都有可分类结果。它是较大的纯重构，因此按
“文件前缀 → 通道 helper → session → main”五个可复制块重组 `main.cpp`；每块后都解释职责。

### 修改文件

`out/learn-webrtc-minilab/main.cpp`

### 代码修改

<div style="background-color:#fde2e2;border-left:5px solid #c62828;padding:10px 14px;margin:10px 0;">
<strong>删除（红色）：A/B 各自注册 onOpen/onMessage，main() 使用无界 Cleanup().wait()</strong>
<pre><code>channels_[0]-&gt;onOpen(...);
peers_[1]-&gt;onDataChannel(...);
rtc::Cleanup().wait();</code></pre>
</div>

<div style="background-color:#fff3cd;border-left:5px solid #d6a700;padding:10px 14px;margin:10px 0;">
<strong>新增（黄色）：attachChannel()、RunResult 和 wait_for(kCleanupTimeout)</strong>
<pre><code>attachChannel(weakState, side, channel);
RunResult run(int round);
cleanup.wait_for(kCleanupTimeout);</code></pre>
</div>

#### 5.3.1 文件前缀、错误类型和共享状态

先用下面内容替换文件开头直到 `SharedState` 结束；后续代码块按顺序紧接其后：

```cpp
#include <rtc/rtc.hpp>

#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
namespace {
using Clock = std::chrono::steady_clock;
using Deadline = Clock::time_point;
constexpr auto kRunTimeout = std::chrono::seconds(20);
constexpr auto kCleanupTimeout = std::chrono::seconds(10);
constexpr std::string_view kLabel = "minilab";
enum class Error {
    None,
    InvalidState,
    LibraryFailure,
    Timeout,
    ConnectionFailed,
    ProtocolMismatch,
    CleanupTimeout,
};
const char *errorName(Error error) noexcept
{
    switch (error) {
    case Error::None: return "none";
    case Error::InvalidState: return "invalid_state";
    case Error::LibraryFailure: return "library_failure";
    case Error::Timeout: return "timeout";
    case Error::ConnectionFailed: return "connection_failed";
    case Error::ProtocolMismatch: return "protocol_mismatch";
    case Error::CleanupTimeout: return "cleanup_timeout";
    }
    return "library_failure";
}
struct SharedState
{
    std::mutex mutex;
    std::condition_variable changed;
    bool gathered[2] {false, false};
    bool connected[2] {false, false};
    bool channelOpen[2] {false, false};
    bool pingReceived = false;
    bool pongReceived = false;
    bool failed = false;
    Error error = Error::None;
    std::shared_ptr<rtc::DataChannel> channels[2];
};
```

`kRunTimeout` 控制一轮协商，`kCleanupTimeout` 只控制进程级库清理，两者不能互相替代。`kLabel` 用
`string_view` 表示编译期只读文本，使用时再显式构造 `std::string` 传给需要拥有字符串的 API。

`SharedState::channels[side]` 成为回调与控制线程共享的正式句柄槽；`pingReceived/pongReceived` 用
协议语义命名，因为它们不再与任意消息数组混在一起。新增 `LibraryFailure` 和 `CleanupTimeout`
保证异常和清理超时也只能落入固定允许列表。

#### 5.3.2 统一 fail() 与 attachChannel()

紧接上一块复制：

```cpp
void fail(
    const std::weak_ptr<SharedState> &weakState,
    Error error
) noexcept
{
    const auto state = weakState.lock();
    if (!state) return;
    const std::lock_guard lock(state->mutex);
    state->failed = true;
    state->error = error;
    state->changed.notify_all();
}

void attachChannel(
    const std::weak_ptr<SharedState> &weakState,
    int side,
    std::shared_ptr<rtc::DataChannel> channel
)
{
    const auto state = weakState.lock();
    if (!state) return;
    try {
        if (channel->label() != std::string(kLabel)) {
            fail(weakState, Error::ProtocolMismatch);
            channel->close();
            return;
        }
        channel->onOpen([weakState, side] {
            const auto state = weakState.lock();
            if (!state) return;
            const std::lock_guard lock(state->mutex);
            state->channelOpen[side] = true;
            state->changed.notify_all();
        });
        channel->onMessage(
            [weakState](rtc::binary) {
                fail(weakState, Error::ProtocolMismatch);
            },
            [weakState, side](rtc::string message) {
                const auto state = weakState.lock();
                if (!state) return;
                const std::lock_guard lock(state->mutex);
                const bool expected = side == 0 ? message == "pong"
                                                : message == "ping";
                if (!expected) {
                    state->failed = true;
                    state->error = Error::ProtocolMismatch;
                } else if (side == 0) {
                    state->pongReceived = true;
                } else {
                    state->pingReceived = true;
                }
                state->changed.notify_all();
            }
        );
        channel->onError([weakState](rtc::string) {
            fail(weakState, Error::ConnectionFailed);
        });

        const bool alreadyOpen = channel->isOpen();
        const std::lock_guard lock(state->mutex);
        state->channels[side] = channel;
        state->channelOpen[side] = alreadyOpen;
        state->changed.notify_all();
    } catch (...) {
        fail(weakState, Error::LibraryFailure);
    }
}
```

`fail(weakState,error)` 不属于 session 对象，任何回调都能调用；weak state 失效时静默丢弃。参数
`error` 按值复制，函数不记录异常消息。

`attachChannel()` 的 `side` 决定该端期望收到什么：0 只接受 pong，1 只接受 ping。`channel` 按值
取得共享所有权，验证 label 后注册三类回调。`onMessage(binary,text)` 的两个参数分别处理二进制和
文本；任何二进制输入直接归类为协议不匹配。`onError` 接收库错误字符串，但 lambda 故意不命名也不
输出它，只提交 `ConnectionFailed`。

`isOpen()` 处理“通道在注册 onOpen 前已经打开”的竞态。helper 在持锁区一次提交句柄和当前 open
状态，避免控制线程看到 open 却拿不到句柄。异常统一转成 `LibraryFailure`。

#### 5.3.3 RunResult 和 MiniLabSession 公共流程

紧接 helper 复制：

```cpp
struct RunResult
{
    Error error = Error::None;
    const char *stage = "complete";
    std::chrono::milliseconds elapsed {0};
};

class MiniLabSession final
{
public:
    MiniLabSession() : state_(std::make_shared<SharedState>()) {}
    ~MiniLabSession() { (void)close(); }
    MiniLabSession(const MiniLabSession &) = delete;
    MiniLabSession &operator=(const MiniLabSession &) = delete;

    RunResult run(int round)
    {
        const auto started = Clock::now();
        const Deadline deadline = started + kRunTimeout;
        std::cout << "round=" << round << " stage=start result=success\n";
        try {
            rtc::Configuration configuration;
            configuration.iceServers.clear();
            configuration.disableAutoNegotiation = true;
            configuration.enableIceTcp = false;
            peers_[0] = std::make_shared<rtc::PeerConnection>(configuration);
            peers_[1] = std::make_shared<rtc::PeerConnection>(configuration);
            registerPeer(0);
            registerPeer(1);

            const std::weak_ptr<SharedState> weakState(state_);
            peers_[1]->onDataChannel(
                [weakState](std::shared_ptr<rtc::DataChannel> channel) {
                    attachChannel(weakState, 1, std::move(channel));
                }
            );
            attachChannel(
                weakState,
                0,
                peers_[0]->createDataChannel(std::string(kLabel))
            );
            std::cout << "round=" << round
                      << " stage=peers state=new result=success\n";
            peers_[0]->setLocalDescription(rtc::Description::Type::Offer);
            if (const Error error = waitUntil(
                    deadline, [](const SharedState &s) { return s.gathered[0]; }
                ); error != Error::None) {
                return failure(started, "offer_gathering", error);
            }
            const auto offer = peers_[0]->localDescription();
            if (!offer) return failure(started, "offer", Error::InvalidState);
            peers_[1]->setRemoteDescription(
                rtc::Description(offer->generateSdp(), "offer")
            );
            peers_[1]->setLocalDescription(rtc::Description::Type::Answer);
            std::cout << "round=" << round
                      << " stage=offer state=complete result=success\n";
            if (const Error error = waitUntil(
                    deadline, [](const SharedState &s) { return s.gathered[1]; }
                ); error != Error::None) {
                return failure(started, "answer_gathering", error);
            }
            const auto answer = peers_[1]->localDescription();
            if (!answer) return failure(started, "answer", Error::InvalidState);
            peers_[0]->setRemoteDescription(
                rtc::Description(answer->generateSdp(), "answer")
            );
            std::cout << "round=" << round
                      << " stage=answer state=complete result=success\n";
            if (const Error error = waitUntil(deadline, [](const SharedState &s) {
                    return s.connected[0] && s.connected[1];
                }); error != Error::None) {
                return failure(started, "connection", error);
            }
            if (const Error error = waitUntil(deadline, [](const SharedState &s) {
                    return s.channelOpen[0] && s.channelOpen[1] &&
                           s.channels[0] && s.channels[1];
                }); error != Error::None) {
                return failure(started, "data_channel", error);
            }
            std::cout << "round=" << round
                      << " stage=connection state=connected result=success\n";
            std::cout << "round=" << round
                      << " stage=data_channel state=open result=success\n";
            const auto pingStarted = Clock::now();
            (void)channel(0)->send(std::string("ping"));
            if (const Error error = waitUntil(
                    deadline, [](const SharedState &s) { return s.pingReceived; }
                ); error != Error::None) {
                return failure(started, "ping", error);
            }
            (void)channel(1)->send(std::string("pong"));
            if (const Error error = waitUntil(
                    deadline, [](const SharedState &s) { return s.pongReceived; }
                ); error != Error::None) {
                return failure(started, "pong", error);
            }
            const auto elapsed = std::chrono::duration_cast<
                std::chrono::milliseconds>(Clock::now() - pingStarted);
            std::cout << "round=" << round
                      << " stage=ping_pong elapsed_ms=" << elapsed.count()
                      << " result=success\n";
            return {Error::None, "complete", elapsed};
        } catch (...) {
            return failure(started, "runtime", Error::LibraryFailure);
        }
    }

    bool close() noexcept
    {
        if (closed_) return closeSucceeded_;
        closed_ = true;
        std::shared_ptr<rtc::DataChannel> channels[2];
        {
            const std::lock_guard lock(state_->mutex);
            channels[0] = std::move(state_->channels[0]);
            channels[1] = std::move(state_->channels[1]);
        }
        for (auto &channel : channels) closeObject(channel);
        for (auto &peer : peers_) closeObject(peer);
        peers_[0].reset();
        peers_[1].reset();
        return closeSucceeded_;
    }

private:
```

`RunResult` 把错误、阶段和耗时作为一个值返回；`stage` 仍指向静态字符串。`run(round)` 的参数只
用于允许列表输出，不进入网络。它捕获整轮开始时间和 ping 开始时间：前者用于失败耗时，后者只测
应用 ping/pong RTT。

复制构造和复制赋值被删除，因为 session 是唯一资源所有者；复制两个 session 句柄会模糊谁负责
关闭。`close()` 返回布尔结果且重复调用返回同一结果。此检查点还没有 generation，真正的回调失效
顺序在下一步加入。

#### 5.3.4 Peer 回调、等待和资源 helper

接在 `private:` 后复制，并用最后的 `};` 结束类和匿名命名空间：

```cpp
    void registerPeer(int side)
    {
        const std::weak_ptr<SharedState> weakState(state_);
        peers_[side]->onGatheringStateChange(
            [weakState, side](
                rtc::PeerConnection::GatheringState value
            ) {
                const auto state = weakState.lock();
                if (!state) return;
                const std::lock_guard lock(state->mutex);
                if (value == rtc::PeerConnection::GatheringState::Complete) {
                    state->gathered[side] = true;
                }
                state->changed.notify_all();
            }
        );
        peers_[side]->onStateChange(
            [weakState, side](rtc::PeerConnection::State value) {
                const auto state = weakState.lock();
                if (!state) return;
                const std::lock_guard lock(state->mutex);
                if (value == rtc::PeerConnection::State::Connected) {
                    state->connected[side] = true;
                } else if (value == rtc::PeerConnection::State::Failed ||
                           value == rtc::PeerConnection::State::Closed) {
                    state->failed = true;
                    state->error = Error::ConnectionFailed;
                }
                state->changed.notify_all();
            }
        );
    }

    template<typename Predicate>
    Error waitUntil(Deadline deadline, Predicate ready)
    {
        std::unique_lock lock(state_->mutex);
        if (!state_->changed.wait_until(lock, deadline, [&] {
                return ready(*state_) || state_->failed;
            })) {
            return Error::Timeout;
        }
        if (state_->failed) return state_->error;
        return Error::None;
    }

    std::shared_ptr<rtc::DataChannel> channel(int side)
    {
        const std::lock_guard lock(state_->mutex);
        return state_->channels[side];
    }

    static RunResult failure(
        Clock::time_point started,
        const char *stage,
        Error error
    ) noexcept
    {
        return {
            error,
            stage,
            std::chrono::duration_cast<std::chrono::milliseconds>(
                Clock::now() - started
            ),
        };
    }

    template<typename T>
    void resetCallbacks(const std::shared_ptr<T> &object) noexcept
    {
        if (!object) return;
        try { object->resetCallbacks(); }
        catch (...) { closeSucceeded_ = false; }
    }

    template<typename T>
    void closeObject(const std::shared_ptr<T> &object) noexcept
    {
        if (!object) return;
        try { object->close(); }
        catch (...) { closeSucceeded_ = false; }
    }

    std::shared_ptr<SharedState> state_;
    std::shared_ptr<rtc::PeerConnection> peers_[2];
    bool closed_ = false;
    bool closeSucceeded_ = true;
};

} // namespace
```

`registerPeer(side)` 只注册通用回调；`waitUntil()` 现在返回 `Error`，因此能区分 timeout 和回调提交
的连接/协议错误。`channel(side)` 在锁内复制 shared pointer，调用者获得足以覆盖本次 `send()` 的
共享所有权。

`failure(started,stage,error)` 是纯值构造 helper；`duration_cast` 把单调时钟时长转换成整毫秒。
`resetCallbacks()/closeObject()` 是模板，因为 PeerConnection 与 DataChannel 都提供同名生命周期
API；空指针直接成功，异常只把 `closeSucceeded_` 置为 false，绝不从 `noexcept close()` 逃出。

此检查点定义了 `resetCallbacks()`，但 `close()` 还没有调用它；下一步会按正确顺序接入。把重构和
生命周期变化分开，能用同一测试证明哪一步改变了什么。

#### 5.3.5 main() 和有界全局 Cleanup

最后复制进程入口：

```cpp
int main()
{
    bool succeeded = true;
    int completed = 0;
    try {
        rtc::InitLogger(rtc::LogLevel::None);
        MiniLabSession session;
        RunResult result = session.run(1);
        const bool firstClose = session.close();
        const bool secondClose = session.close();
        if (!firstClose || !secondClose) {
            result = {Error::LibraryFailure, "shutdown", result.elapsed};
        }
        std::cout << "round=1 stage=shutdown state=closed result="
                  << ((firstClose && secondClose) ? "success" : "failure")
                  << '\n';
        if (result.error != Error::None) {
            std::cout << "round=1 stage=" << result.stage
                      << " elapsed_ms=" << result.elapsed.count()
                      << " result=failure error=" << errorName(result.error)
                      << '\n';
            succeeded = false;
        } else ++completed;
    } catch (...) {
        std::cout << "stage=runtime result=failure error=library_failure\n";
        succeeded = false;
    }
    try {
        auto cleanup = rtc::Cleanup();
        if (cleanup.wait_for(kCleanupTimeout) == std::future_status::timeout) {
            std::cout << "stage=cleanup result=failure error="
                      << errorName(Error::CleanupTimeout) << '\n';
            succeeded = false;
        } else {
            cleanup.get();
            std::cout << "stage=cleanup state=complete result=success\n";
        }
    } catch (...) {
        std::cout << "stage=cleanup result=failure error="
                  << errorName(Error::LibraryFailure) << '\n';
        succeeded = false;
    }
    std::cout << "summary rounds=" << completed
              << " result=" << (succeeded ? "success" : "failure") << '\n';
    return succeeded && completed == 1 ? 0 : 1;
}
```

`firstClose/secondClose` 用同一对象验证最小幂等性。`completed` 只在 run 成功时递增；summary 是后续
CTest 的稳定判定字段。

`rtc::Cleanup()` 返回的 future 先用 `wait_for(10s)` 等待。返回 `timeout` 时记录固定
`cleanup_timeout`；就绪时必须再调用 `get()`，因为 `get()` 会传播后台清理异常。这个 10 秒只约束
全局库资源，不延长一轮 20 秒协商 deadline。

### 构建

```powershell
cmake --build 'out/build-webrtc-minilab-learning'
```

### 运行或测试

```powershell
& 'out/build-webrtc-minilab-learning/webrtc_minilab.exe'
```

### 观察证据

实际检查点包含：

```text
round=1 stage=peers state=new result=success
round=1 stage=connection state=connected result=success
round=1 stage=data_channel state=open result=success
round=1 stage=ping_pong elapsed_ms=<动态值> result=success
round=1 stage=shutdown state=closed result=success
stage=cleanup state=complete result=success
summary rounds=1 result=success
```

`elapsed_ms` 是动态值，不作为唯一通过条件。该重构后的实测退出码为 0。

### 通过条件

- A/B channel 统一经过 `attachChannel()`；
- 二进制、label、文本和库错误都有固定分类；
- run 返回 `RunResult`，不输出原始异常；
- Cleanup 最多等待 10 秒并调用 `get()`；
- 重构前后的 ping/pong 稳定结果一致。

## 步骤 5.4：用 generation 失效晚到回调并幂等关闭

### 当前问题

weak pointer 只能阻止状态对象销毁后的访问。如果 state 仍活着但关闭已经开始，旧回调仍可能修改
`connected` 或消息结果。关闭必须先声明“这一代会话作废”，再断开回调并关闭库对象。

### 修改文件

`out/learn-webrtc-minilab/main.cpp`

### 代码修改

#### 5.4.1 给状态和所有回调加入 generation

<div style="background-color:#fde2e2;border-left:5px solid #c62828;padding:10px 14px;margin:10px 0;">
<strong>删除（红色）：只要 weakState.lock() 成功就接受回调</strong>
<pre><code>const auto state = weakState.lock();
if (!state) return;
state-&gt;connected[side] = true;</code></pre>
</div>

<div style="background-color:#fff3cd;border-left:5px solid #d6a700;padding:10px 14px;margin:10px 0;">
<strong>新增（黄色）：closing 与创建时 generation 双重检查</strong>
<pre><code>if (state-&gt;closing || state-&gt;generation != generation) return;</code></pre>
</div>

在 `SharedState` 的条件变量后加入：

```cpp
bool closing = false;
std::uint64_t generation = 1;
```

`closing` 表示停止已经开始；`generation` 是递增的会话票号。回调注册时复制当前票号，执行时必须
同时满足“未 closing 且票号仍相同”。`uint64_t` 的宽度固定，实际程序不会在一次进程中接近回绕。

用下面函数替换 `fail()`：

```cpp
void fail(
    const std::weak_ptr<SharedState> &weakState,
    std::uint64_t generation,
    Error error
) noexcept
{
    const auto state = weakState.lock();
    if (!state) return;
    const std::lock_guard lock(state->mutex);
    if (state->closing || state->generation != generation) return;
    state->failed = true;
    state->error = error;
    state->changed.notify_all();
}
```

新增的 `generation` 按值传入。即使 state 还存在，只要关闭已推进代次，旧错误也会被丢弃。所有
`fail(weakState,error)` 调用都必须改为 `fail(weakState,generation,error)`。

#### 5.4.2 把代次传播到 DataChannel 回调

把 `attachChannel()` 的签名改为：

```cpp
void attachChannel(
    const std::weak_ptr<SharedState> &weakState,
    std::uint64_t generation,
    int side,
    std::shared_ptr<rtc::DataChannel> channel
)
```

每个 DataChannel 回调按值捕获 generation，并在持锁后先执行同一检查。文本回调的完整形状如下：

```cpp
[weakState, generation, side](rtc::string message) {
    const auto state = weakState.lock();
    if (!state) return;
    const std::lock_guard lock(state->mutex);
    if (state->closing || state->generation != generation) return;
    const bool expected = side == 0 ? message == "pong"
                                    : message == "ping";
    if (!expected) {
        state->failed = true;
        state->error = Error::ProtocolMismatch;
    } else if (side == 0) {
        state->pongReceived = true;
    } else {
        state->pingReceived = true;
    }
    state->changed.notify_all();
}
```

`onOpen` 捕获 `weakState,generation,side`；二进制和 `onError` 捕获
`weakState,generation` 并调用新版 `fail()`。这三个回调都不能只检查 weak pointer。

注册完回调后，用下面代码替换直接提交句柄的持锁区：

```cpp
const bool alreadyOpen = channel->isOpen();
bool reject = false;
if (state) {
    const std::lock_guard lock(state->mutex);
    reject = state->closing || state->generation != generation;
    if (!reject) {
        state->channels[side] = channel;
        state->channelOpen[side] = alreadyOpen;
        state->changed.notify_all();
    }
} else reject = true;
if (reject) {
    channel->resetCallbacks();
    channel->close();
}
```

`reject` 覆盖一个重要窗口：通道在 helper 开始时有效，但注册过程中 session 可能已经关闭。此时不能
再把句柄放回共享状态；应在锁外先 `resetCallbacks()` 再 `close()`。这段局部清理不由 session 数组
拥有，因而必须就地完成。

在 `run()` 注册通道前读取一次代次，并传给两端：

```cpp
const std::weak_ptr<SharedState> weakState(state_);
const std::uint64_t generation = state_->generation;
peers_[1]->onDataChannel(
    [weakState, generation](std::shared_ptr<rtc::DataChannel> channel) {
        attachChannel(weakState, generation, 1, std::move(channel));
    }
);
attachChannel(
    weakState,
    generation,
    0,
    peers_[0]->createDataChannel(std::string(kLabel))
);
```

读取发生在控制线程、回调注册之前；这一代所有 PeerConnection/DataChannel 回调共享同一个票号。

#### 5.4.3 先失效回调，再关闭资源

<div style="background-color:#fde2e2;border-left:5px solid #c62828;padding:10px 14px;margin:10px 0;">
<strong>删除（红色）：直接 close DataChannel 和 PeerConnection</strong>
<pre><code>for (auto &amp;channel : channels) closeObject(channel);
for (auto &amp;peer : peers_) closeObject(peer);</code></pre>
</div>

<div style="background-color:#fff3cd;border-left:5px solid #d6a700;padding:10px 14px;margin:10px 0;">
<strong>新增（黄色）：closing → generation++ → resetCallbacks → close</strong>
<pre><code>state_-&gt;closing = true;
++state_-&gt;generation;
resetCallbacks(...);
closeObject(...);</code></pre>
</div>

用下面完整函数替换 `close()`：

```cpp
bool close() noexcept
{
    if (closed_) return closeSucceeded_;
    closed_ = true;
    std::shared_ptr<rtc::DataChannel> channels[2];
    {
        const std::lock_guard lock(state_->mutex);
        state_->closing = true;
        ++state_->generation;
        state_->changed.notify_all();
        channels[0] = std::move(state_->channels[0]);
        channels[1] = std::move(state_->channels[1]);
    }
    for (auto &channel : channels) resetCallbacks(channel);
    for (auto &peer : peers_) resetCallbacks(peer);
    for (auto &channel : channels) closeObject(channel);
    for (auto &peer : peers_) closeObject(peer);
    peers_[0].reset();
    peers_[1].reset();
    return closeSucceeded_;
}
```

顺序具有语义：先在锁内标记 closing、推进代次并唤醒控制线程；再把通道所有权移到局部；锁外依次
断开通道回调、PeerConnection 回调、关闭通道、关闭 PeerConnection，最后释放 peer 句柄。若
`close()` 被再次调用，`closed_` 让它只返回第一次结果，不重复操作资源。

`resetCallbacks()` 不等于销毁对象，它只是阻止已注册回调继续进入应用。已经在执行的回调仍靠
generation 检查拒绝晚到提交，所以两道防线都需要。

#### 5.4.4 Peer 回调和等待也检查关闭状态

用下面完整函数替换 `registerPeer()`：

```cpp
void registerPeer(int side)
{
    const std::weak_ptr<SharedState> weakState(state_);
    const std::uint64_t generation = state_->generation;
    peers_[side]->onGatheringStateChange(
        [weakState, generation, side](
            rtc::PeerConnection::GatheringState value
        ) {
            const auto state = weakState.lock();
            if (!state) return;
            const std::lock_guard lock(state->mutex);
            if (state->closing || state->generation != generation) return;
            if (value == rtc::PeerConnection::GatheringState::Complete) {
                state->gathered[side] = true;
            }
            state->changed.notify_all();
        }
    );
    peers_[side]->onStateChange(
        [weakState, generation, side](rtc::PeerConnection::State value) {
            const auto state = weakState.lock();
            if (!state) return;
            const std::lock_guard lock(state->mutex);
            if (state->closing || state->generation != generation) return;
            if (value == rtc::PeerConnection::State::Connected) {
                state->connected[side] = true;
            } else if (value == rtc::PeerConnection::State::Failed ||
                       value == rtc::PeerConnection::State::Closed) {
                state->failed = true;
                state->error = Error::ConnectionFailed;
            }
            state->changed.notify_all();
        }
    );
}
```

用下面函数替换 `waitUntil()`：

```cpp
template<typename Predicate>
Error waitUntil(Deadline deadline, Predicate ready)
{
    std::unique_lock lock(state_->mutex);
    if (!state_->changed.wait_until(lock, deadline, [&] {
            return ready(*state_) || state_->failed || state_->closing;
        })) {
        return Error::Timeout;
    }
    if (state_->failed) return state_->error;
    return state_->closing ? Error::InvalidState : Error::None;
}
```

关闭会 `notify_all()`，所以等待 predicate 必须把 `closing` 当作终止条件；否则控制线程可能在资源
已关闭后仍等到 deadline。返回 `InvalidState` 能区分“外部关闭打断”与网络 timeout。

### 构建

```powershell
cmake --build 'out/build-webrtc-minilab-learning'
```

### 运行或测试

```powershell
& 'out/build-webrtc-minilab-learning/webrtc_minilab.exe'
```

### 观察证据

generation 检查点实际编译、运行成功；程序仍只输出一次 shutdown、一次 Cleanup 和成功 summary，
退出码为 0。

### 通过条件

- 每个库回调同时捕获 weak state 和创建时 generation；
- close 先 closing/推进 generation，再 reset callback 和关闭对象；
- 等待在 closing 时立即结束，不等满 deadline；
- close 重复调用返回第一次结果；
- Cleanup 发生在 PeerConnection 释放之后。

## 步骤 5.5：加入受限 CLI 和多轮独立会话

### 当前问题

现在单轮生命周期已经安全，但还不能从命令行验证重复关闭。参数解析必须拒绝多余字符、越界轮数和
未知选项；每轮必须创建新的 `MiniLabSession`，全局 `rtc::Cleanup()` 则只能在所有轮次结束后
执行一次。

### 修改文件

`out/learn-webrtc-minilab/main.cpp`

### 代码修改

### 5.5.1 严格解析参数

<div style="background-color:#fff3cd;border-left:5px solid #d6a700;padding:10px 14px;margin:10px 0;">
<strong>新增（黄色）：受限参数结构、解析函数与帮助函数</strong>
<pre><code>struct Arguments { int repeat = 1; bool help = false; bool valid = true; };

Arguments parseArguments(int argc, char *argv[]) noexcept;
void printUsage();</code></pre>
</div>

在 `MiniLabSession` 类定义之后、匿名命名空间结束之前复制：

```cpp
struct Arguments {
    int repeat = 1;
    bool help = false;
    bool valid = true;
};

Arguments parseArguments(int argc, char *argv[]) noexcept
{
    Arguments result;
    if (argc == 1) {
        return result;
    }
    if (argc != 2) {
        result.valid = false;
        return result;
    }

    const std::string_view argument(argv[1]);
    if (argument == "--help") {
        result.help = true;
        return result;
    }

    constexpr std::string_view prefix = "--repeat=";
    if (argument.substr(0, prefix.size()) != prefix) {
        result.valid = false;
        return result;
    }

    const std::string_view number = argument.substr(prefix.size());
    const auto parsed = std::from_chars(
        number.data(), number.data() + number.size(), result.repeat
    );
    result.valid = !number.empty()
        && parsed.ec == std::errc()
        && parsed.ptr == number.data() + number.size()
        && result.repeat >= 1
        && result.repeat <= 100;
    return result;
}

void printUsage()
{
    std::cout << "usage: webrtc_minilab [--repeat=N]\n"
              << "       webrtc_minilab --help\n"
              << "repeat_range: 1..100\n";
}
```

这段代码解决的是“输入边界不明确”的问题。`Arguments` 是解析结果值对象：`repeat` 是计划执行
的轮数，默认 1；`help` 表示只显示帮助；`valid` 表示整个参数集合是否合法。`parseArguments()`
只读取 `argc/argv`，不修改全局状态，调用者是 `main()`。它按值返回结果，因此返回后不依赖
`argv` 的生命周期。

**API 卡片：`argc` 与 `argv`。** `argc` 是包括程序名在内的参数数量；`argv` 是由启动环境
拥有的 C 字符串指针数组。函数可以读取这些字符串，但本例不保存指针，也不修改字符串。无参数时
`argc == 1`；本教程只允许再多一个参数，因此 `argc != 2` 会直接判为非法。

**API 卡片：`std::from_chars(first, last, value)`。** 这是 C++17 的无异常数字解析 API，
头文件为 `<charconv>`。前两个参数组成左闭右开的字符范围，第三个参数是写入目标；返回
`std::from_chars_result`，其中 `ec` 是错误码，`ptr` 指向停止解析的位置。成功条件不能只检查
`ec`：还要确认输入非空、`ptr == last`，否则 `--repeat=10abc` 会被错误地接受。本例再限制
`1..100`，避免无限或意外的长时间运行。失败不会抛异常，也不会修改任何网络对象。

执行链为：`main() → parseArguments() → Arguments`。这个边界先于 logger、PeerConnection 和
Cleanup，因此参数错误时不会创建 WebRTC 资源。当前仅支持无参数、`--help` 和
`--repeat=N`；不接受 SDP、文件路径或网络地址。

### 5.5.2 每轮创建独立会话

<div style="background-color:#fde2e2;border-left:5px solid #c62828;padding:10px 14px;margin:10px 0;">
<strong>删除（红色）：固定只执行 round=1 的 main()</strong>
<pre><code>int main()
{
    MiniLabSession session;
    RunResult result = session.run(1);
    // 关闭一次会话，然后执行全局 Cleanup。
}</code></pre>
</div>

<div style="background-color:#fff3cd;border-left:5px solid #d6a700;padding:10px 14px;margin:10px 0;">
<strong>新增（黄色）：先处理参数，再循环创建短生命周期会话</strong>
<pre><code>int main(int argc, char *argv[])
{
    const Arguments arguments = parseArguments(argc, argv);
    for (int round = 1; round &lt;= arguments.repeat; ++round) {
        MiniLabSession session;
        // run、两次 close 和结果检查
    }
    // 循环结束后只执行一次 rtc::Cleanup()
}</code></pre>
</div>

用下面的完整 `main()` 替换旧函数：

```cpp
int main(int argc, char *argv[])
{
    const Arguments arguments = parseArguments(argc, argv);
    if (!arguments.valid) {
        std::cout << "result=failure error=invalid_arguments\n";
        printUsage();
        return 2;
    }
    if (arguments.help) {
        printUsage();
        return 0;
    }

    bool succeeded = true;
    int completed = 0;
    try {
        rtc::InitLogger(rtc::LogLevel::None);
        for (int round = 1; round <= arguments.repeat; ++round) {
            MiniLabSession session;
            RunResult result = session.run(round);
            const bool firstClose = session.close();
            const bool secondClose = session.close();
            if (!firstClose || !secondClose) {
                result = {Error::LibraryFailure, "shutdown", result.elapsed};
            }

            std::cout << "round=" << round
                      << " stage=shutdown state=closed result="
                      << ((firstClose && secondClose) ? "success" : "failure")
                      << '\n';

            if (result.error != Error::None) {
                std::cout << "round=" << round
                          << " stage=" << result.stage
                          << " elapsed_ms=" << result.elapsed.count()
                          << " result=failure error=" << errorName(result.error)
                          << '\n';
                succeeded = false;
                break;
            }
            ++completed;
        }
    } catch (...) {
        std::cout << "stage=runtime result=failure error=library_failure\n";
        succeeded = false;
    }

    try {
        auto cleanup = rtc::Cleanup();
        if (cleanup.wait_for(kCleanupTimeout) == std::future_status::timeout) {
            std::cout << "stage=cleanup result=failure error="
                      << errorName(Error::CleanupTimeout) << '\n';
            succeeded = false;
        } else {
            cleanup.get();
            std::cout << "stage=cleanup state=complete result=success\n";
        }
    } catch (...) {
        std::cout << "stage=cleanup result=failure error="
                  << errorName(Error::LibraryFailure) << '\n';
        succeeded = false;
    }

    std::cout << "summary rounds=" << completed
              << " result=" << (succeeded ? "success" : "failure") << '\n';
    return succeeded && completed == arguments.repeat ? 0 : 1;
}
```

`main()` 是组合根：它拥有每轮的 `MiniLabSession`，而 session 拥有当轮的 PeerConnection 和
DataChannel。`for` 循环每次进入都会构造新 session，离开循环体时销毁它；因此某轮的 generation、
回调和通道不能流入下一轮。显式连续调用两次 `close()` 是幂等性测试，不是业务必须调用两次。

`rtc::InitLogger(rtc::LogLevel::None)` 关闭 libdatachannel 内部日志，避免库把网络材料写到教程
输出。外层 `catch (...)` 只映射为固定 `library_failure`，不会打印 `what()`。一旦某轮失败，
循环立刻停止，`completed` 只统计完整成功的轮次。

Cleanup 位于循环之后，所以无论执行 1 轮还是 100 轮都只调用一次。`wait_for(kCleanupTimeout)`
是 10 秒的外层上限；`get()` 负责取得异步结果并传播异常。退出码契约为：成功或帮助返回 0，
运行/超时失败返回 1，参数错误返回 2。这里的 `return` 值由调用进程读取，不是 WebRTC 状态码。

执行链现在是：
`parseArguments → [MiniLabSession::run → close → close] × N → rtc::Cleanup → summary`。
当前能力是有界重复运行；它仍不并行运行多个 session，也不接收 SDP、路径、STUN/TURN 或媒体参数。

### 构建

```powershell
cmake --build 'out/build-webrtc-minilab-learning'
```

### 运行或测试

```powershell
& 'out/build-webrtc-minilab-learning/webrtc_minilab.exe' --repeat=10
& 'out/build-webrtc-minilab-learning/webrtc_minilab.exe' --help
& 'out/build-webrtc-minilab-learning/webrtc_minilab.exe' --repeat=0
$LASTEXITCODE
```

第一条命令验证十次独立 session，第二条只显示用法，第三条验证边界拒绝。`$LASTEXITCODE` 是
PowerShell 保存的最近原生进程退出码；非法参数后的稳定值应为 2。

### 观察证据

实际重放十轮成功；帮助返回 0；`--repeat=0` 输出 `invalid_arguments` 并返回 2。十轮各自创建
新 PeerConnection，最后只出现一次 Cleanup 和一次 summary。

### 通过条件

- `--repeat=1..100` 严格解析；
- 每轮新建并关闭一个 session；
- 所有轮次结束后只 Cleanup 一次；
- 学习 `main.cpp` 与
  [最终 MiniLab 源码](../../../../tutorials/webrtc-minilab/main.cpp) 执行
  `git diff --no-index` 时返回 0。

## 原理和类比：调度员与过期车票

- 控制线程是调度员，决定下一站；
- 库回调是报信员，只更新状态板并按铃；
- deadline 是整趟车的末班时间，不是每站重置；
- generation 是车票批次，上一趟的迟到通知不能进入下一趟；
- `close()` 是统一清场路线，正常、失败和析构都复用。

未来接 Qt 也保持这个规则：库回调不能直接操作 QWidget；组合根把不可变状态或有界数据投递给拥有
线程。MiniLab 本身不依赖 Qt。

## 本章小实验

运行三轮并统计生命周期事件，验证“每轮 shutdown、全局只 Cleanup 一次”。

### 答案代码

```powershell
$exe = 'out/build-webrtc-minilab-learning/webrtc_minilab.exe'
$text = (& $exe --repeat=3 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0) { throw 'repeat failed' }
if (([regex]::Matches(
        $text,
        '(?m)^round=\d+ stage=shutdown state=closed result=success\r?$'
    )).Count -ne 3) {
    throw 'shutdown count'
}
if (([regex]::Matches(
        $text,
        '(?m)^stage=cleanup state=complete result=success\r?$'
    )).Count -ne 1) {
    throw 'cleanup count'
}
if (([regex]::Matches(
        $text,
        '(?m)^summary rounds=3 result=success\r?$'
    )).Count -ne 1) {
    throw 'summary count'
}
'lifecycle counts: ok'
```

### 实验验证

本仓库实测退出 0，shutdown=3、Cleanup=1、成功 summary=1。

### 实验通过条件

- 三轮都成功并各关闭一次；
- 幂等 close 的第二次调用不产生第二行 shutdown；
- Cleanup 和 summary 各一次。

## 故障恢复

| 现象 | 检查 | 恢复 |
| --- | --- | --- |
| 等待挂住 | 每个回调是否 notify、是否共用 deadline | 修复谓词，不加 sleep |
| 退出偶发崩溃 | 回调是否捕获裸 `this` 或缺 generation | 改为 weak state + generation |
| 关闭死锁 | 是否持 state 锁调用 reset/close | 先移出对象并解锁 |
| 第二轮继承状态 | 是否复用 session | 每轮重新构造 |
| 参数被宽松接受 | 是否检查 `from_chars` 的 `ptr` | 要求完整消费且范围 1..100 |

## 当前完整目录结构

```text
out/learn-webrtc-minilab/
├─ CMakeLists.txt
└─ main.cpp   # 已与最终 MiniLab main.cpp 一致
```

## 本章检查点

- [ ] 错误只来自固定枚举。
- [ ] A/B 共用 side-index 状态和 helper。
- [ ] 所有库回调使用 weak state 并检查 generation/closing。
- [ ] close 顺序正确且重复调用安全。
- [ ] Cleanup 有 10 秒上限且全局只执行一次。
- [ ] CLI 和退出码边界固定。

## 本章总结

MiniLab 从“能跑一次”变成了有明确所有权、总截止时间、迟到回调门禁、幂等释放和严格 CLI 的可重复
程序。主线能力仍是同一个 ping/pong，重构用相同输出证明没有改变行为。

## 下一章将解决什么问题

最后一章把这些稳定不变量写成 CTest、超时、输出正则和隐私扫描，并映射到 RtmpMonitor 的真实进度。

- 上一章：[第 4 章](04_rtp_rtcp_h264_media_pipeline.md)
- 下一章：[第 6 章](06_p2p_testing_security_and_troubleshooting.md)
