# 第 3 章：Offer、Answer 与 non-trickle ICE

两部电话已经摆上桌，现在交换“联系卡”。本章把原本容易写成一个大跳步的协商过程拆成三个都能
编译运行的检查点：先有可等待状态，再收集 Offer，最后交换 Offer/Answer 并 Connected。

## 本章完成后你将得到什么

- A 创建 DataChannel 和 Offer，B 创建 Answer；
- 两份 description 只在内存中交换；
- 每次都等待 ICE gathering complete，不用固定 `sleep`；
- A、B 都进入 `Connected`。

## 前置检查点

- 第 2 章 A/B 均创建、关闭，程序以 0 退出；
- 不依赖 A/B 异步回调的固定顺序；
- `CMakeLists.txt` 保持第 1 章版本。

## 预计时间

75～90 分钟。

## 本章在最终项目中的位置

这条内存信令链证明 WebRTC 协商和 ICE 回环。它对应 Week 2 “握手和道路”的核心原理，但不使用 Week 2
文件 schema，也不等于双机信令或视频 Track。

## 本章知识点

- `setLocalDescription()`、`localDescription()`、`setRemoteDescription()`；
- `onGatheringStateChange()`；
- `condition_variable::wait_until()`；
- SDP、candidate、non-trickle 与 Offerer 的准确含义。

## 当前项目目录

```text
out/learn-webrtc-minilab/
├─ CMakeLists.txt   # 不变
└─ main.cpp        # 第 2 章双端点检查点
```

## 步骤 3.1：加入共享状态与有截止时间的等待

### 当前问题

库回调发生在异步线程，控制线程不能靠“下一行代码”判断 gathering 或连接完成。需要一个只保存小型
事实的状态板，以及由回调唤醒、由控制线程判断的条件变量。

### 修改文件

`out/learn-webrtc-minilab/main.cpp`

### 代码修改

这一步先建立“回调写状态，控制线程等待状态”的桥梁，尚不创建 Offer。按下面三个小块修改。

#### 3.1.1 加入单调时钟和共享状态

<div style="background-color:#fff3cd;border-left:5px solid #d6a700;padding:10px 14px;margin:10px 0;">
<strong>新增（黄色）：单调 deadline 与条件变量状态板</strong>
<pre><code>using Clock = std::chrono::steady_clock;
using Deadline = Clock::time_point;
constexpr auto roundTimeout = std::chrono::seconds(20);

struct SharedState { ... };</code></pre>
</div>

在 libdatachannel 头文件后加入 `<chrono>`、`<condition_variable>`，并在 `outputMutex` 前复制：

```cpp
using Clock = std::chrono::steady_clock;
using Deadline = Clock::time_point;
constexpr auto roundTimeout = std::chrono::seconds(20);
```

在 `emitState()` 后复制：

```cpp
struct SharedState
{
    std::mutex mutex;
    std::condition_variable changed;
    bool offerGathered = false;
    bool answerGathered = false;
    bool offerConnected = false;
    bool answerConnected = false;
    bool failed = false;
};
```

`steady_clock` 只保证单调推进，不受用户调整系统时间影响，适合计算超时；`Deadline` 是绝对时刻，
`roundTimeout` 是 20 秒时长。整个协商只计算一次 deadline，后续阶段共享剩余预算，避免每一步重新
获得 20 秒导致总时长无限增长。

`SharedState` 是回调线程与控制线程之间唯一共享的数据。`mutex` 保护所有布尔字段；`changed` 只负责
唤醒等待者，不保存事件本身。Offer/Answer 的 gathering 与 connected 分开记录，是因为两个端点可在
不同时间完成。session 通过 `shared_ptr` 拥有状态；回调只持有 `weak_ptr`，避免状态与 PeerConnection
形成引用环。

#### 3.1.2 加入带 predicate 的等待函数

<div style="background-color:#fff3cd;border-left:5px solid #d6a700;padding:10px 14px;margin:10px 0;">
<strong>新增（黄色）：waitUntil() 和固定失败出口</strong>
<pre><code>template&lt;typename Predicate&gt;
bool waitUntil(Deadline deadline, Predicate predicate);

bool fail(const char *stage, const char *error);</code></pre>
</div>

在 `private:` 下、`makePeer()` 前复制：

```cpp
template<typename Predicate>
bool waitUntil(Deadline deadline, Predicate predicate)
{
    std::unique_lock<std::mutex> lock(state_->mutex);
    const bool signaled = state_->changed.wait_until(
        lock, deadline,
        [&] { return predicate(*state_) || state_->failed; });
    return signaled && !state_->failed && predicate(*state_);
}

bool fail(const char *stage, const char *error)
{
    emitState(round_, "both", stage, error);
    return false;
}
```

`waitUntil(deadline, predicate)` 的第二个参数是调用者提供的“完成条件”。`std::unique_lock` 与
`condition_variable` 配合：等待期间自动释放 mutex，使回调能够取得锁更新状态；醒来后重新持锁。
`wait_until(lock, deadline, predicate)` 会反复检查 predicate，因此能正确处理虚假唤醒。返回 `false`
有三种含义：到达 deadline、回调设置 `failed`，或醒来后目标条件仍不成立。本章暂时合并为固定错误，
第 5 章再拆成枚举。

`fail(stage, error)` 的两个参数都是静态允许列表字符串，函数不取得所有权；它输出一次固定分类并
返回 `false`，让 `run()` 可以直接 `return fail(...)`。函数不输出异常文本、SDP 或 candidate。

#### 3.1.3 让回调只提交状态

<div style="background-color:#fde2e2;border-left:5px solid #c62828;padding:10px 14px;margin:10px 0;">
<strong>删除（红色）：makePeer() 的回调只打印状态</strong>
<pre><code>peer-&gt;onStateChange([round, endpoint](State state) {
    emitState(round, endpoint, "connection", stateName(state));
});</code></pre>
</div>

<div style="background-color:#fff3cd;border-left:5px solid #d6a700;padding:10px 14px;margin:10px 0;">
<strong>新增（黄色）：回调更新 SharedState 并 notify_all()</strong>
<pre><code>const auto state = weakState.lock();
if (!state) return;
const std::lock_guard&lt;std::mutex&gt; lock(state-&gt;mutex);
state-&gt;changed.notify_all();</code></pre>
</div>

把 `run()` 中两个调用改为：

```cpp
endpointA_ = makePeer(configuration, "A", true);
endpointB_ = makePeer(configuration, "B", false);
```

用下面完整函数替换 `makePeer()`，并在成员区加入
`std::shared_ptr<SharedState> state_ = std::make_shared<SharedState>();`：

```cpp
std::shared_ptr<rtc::PeerConnection> makePeer(
    const rtc::Configuration &configuration,
    const std::string &endpoint,
    bool offerSide)
{
    auto peer = std::make_shared<rtc::PeerConnection>(configuration);
    const std::weak_ptr<SharedState> weakState(state_);
    peer->onStateChange([weakState, round = round_, endpoint, offerSide](
                            rtc::PeerConnection::State value) {
        const auto state = weakState.lock();
        if (!state) return;
        {
            const std::lock_guard<std::mutex> lock(state->mutex);
            if (value == rtc::PeerConnection::State::Connected) {
                if (offerSide) state->offerConnected = true;
                else state->answerConnected = true;
            } else if (value == rtc::PeerConnection::State::Failed) {
                state->failed = true;
            }
            state->changed.notify_all();
        }
        emitState(round, endpoint, "connection", stateName(value));
    });
    peer->onGatheringStateChange(
        [weakState, round = round_, endpoint, offerSide](
            rtc::PeerConnection::GatheringState value) {
            const auto state = weakState.lock();
            if (!state) return;
            {
                const std::lock_guard<std::mutex> lock(state->mutex);
                if (value == rtc::PeerConnection::GatheringState::Complete) {
                    if (offerSide) state->offerGathered = true;
                    else state->answerGathered = true;
                }
                state->changed.notify_all();
            }
            emitState(round, endpoint, "gathering", gatheringName(value));
        });
    emitState(round_, endpoint, "created", "new");
    return peer;
}
```

新增的 `offerSide` 只表示这个端点在**本轮信令**中是否创建 Offer；它不表示视频发送方。回调按值
捕获该标志。`weakState.lock()` 成功时得到一个临时共享所有权，保证本次回调执行期间状态仍存在；
失败说明 session 已销毁，回调直接丢弃。

回调在持锁区只更新小字段并 `notify_all()`，不等待网络，也不操作 UI。输出放在锁外，避免慢终端
延长共享状态临界区。控制线程稍后通过 `waitUntil()` 决定下一步，回调本身不推进协商。

### 构建

```powershell
cmake --build 'out/build-webrtc-minilab-learning'
```

### 运行或测试

```powershell
& 'out/build-webrtc-minilab-learning/webrtc_minilab.exe'
```

### 观察证据

实际重放仍只有 A/B 的 `created → closed`，最终 `complete state=ok`。这说明新增状态板没有偷偷启动网络。

### 通过条件

- 编译、运行成功；
- 与第 2 章稳定检查点相同；
- 回调只写状态和通知，不推进 Offer/Answer 流程。

## 步骤 3.2：创建 Offer 并等 gathering complete

### 当前问题

一个空 PeerConnection 不会自动产生可协商内容。A 需要先创建 DataChannel，再显式创建本地 Offer；因为
本教程没有持续信令通道，必须等候选收集完整后才能把 description 交给 B。

### 修改文件

`out/learn-webrtc-minilab/main.cpp`

### 代码修改

DataChannel 的存在让 Offer 包含真实的数据协商部分；本步只创建通道和 Offer，不发送消息。

<div style="background-color:#fde2e2;border-left:5px solid #c62828;padding:10px 14px;margin:10px 0;">
<strong>删除（红色）：创建 A/B 后立即返回成功</strong>
<pre><code>endpointA_ = makePeer(configuration, "A", true);
endpointB_ = makePeer(configuration, "B", false);
return true;</code></pre>
</div>

<div style="background-color:#fff3cd;border-left:5px solid #d6a700;padding:10px 14px;margin:10px 0;">
<strong>新增（黄色）：创建 DataChannel、Offer 并等待 gathering complete</strong>
<pre><code>offerChannel_ = endpointA_-&gt;createDataChannel("minilab");
endpointA_-&gt;setLocalDescription(rtc::Description::Type::Offer);
waitUntil(deadline, ...);</code></pre>
</div>

在 `run()` 保存 `round_` 后加入本轮唯一 deadline：

```cpp
const Deadline deadline = Clock::now() + roundTimeout;
```

在创建 A/B 后、`return true` 前复制：

```cpp
offerChannel_ = endpointA_->createDataChannel("minilab");
endpointA_->setLocalDescription(rtc::Description::Type::Offer);
if (!waitUntil(deadline, [](const SharedState &state) {
        return state.offerGathered;
    })) {
    return fail("offer_gathering", "gathering_timeout");
}
emitState(round_, "A", "offer_gathering", "complete");
```

在成员区加入：

```cpp
std::shared_ptr<rtc::DataChannel> offerChannel_;
```

并在 `close()` 最前面、关闭 PeerConnection 之前加入：

```cpp
try { if (offerChannel_) offerChannel_->close(); } catch (...) {}
```

#### API：`createDataChannel()`

```cpp
std::shared_ptr<rtc::DataChannel> createDataChannel(const std::string &label);
```

`label` 是协商给远端识别的逻辑名称，本项目固定传 `minilab`。返回的共享句柄由 session 保存；
PeerConnection 也管理底层通道，所以关闭时先请求通道关闭，再关闭 PeerConnection。创建 DataChannel
会触发后续 SDP 中的数据通道协商，但不会自动证明远端已经收到或打开通道。

#### API：`setLocalDescription(Description::Type::Offer)`

该调用要求端点生成本地 Offer 并启动 ICE gathering。参数 `Offer` 是信令角色，不是媒体方向。
因为第 2 章设置了 `disableAutoNegotiation=true`，应用必须显式调用它。结果通过异步 gathering 回调
到达，不由函数返回值同步表示；配置错误或库失败可能抛异常，由 `main()` 的固定错误分类接住。

predicate 只读取持锁的 `SharedState`。`waitUntil()` 使用第 3.1 节计算的 deadline，成功后只能说明 A
已经收集完本机候选，尚未把 Offer 交给 B。整个过程中不会打印 SDP、candidate、地址或端口。

### 构建

```powershell
cmake --build 'out/build-webrtc-minilab-learning'
```

### 运行或测试

```powershell
& 'out/build-webrtc-minilab-learning/webrtc_minilab.exe'
```

### 观察证据

实际输出新增：

```text
round=1 endpoint=A stage=gathering state=in_progress
round=1 endpoint=A stage=gathering state=complete
round=1 endpoint=A stage=offer_gathering state=complete
```

程序仍不会 Connected，因为 Offer 还没交给 B。

### 通过条件

- A 到达 gathering complete；
- 控制线程随后输出 `offer_gathering state=complete`；
- 没有 SDP、candidate、地址、端口、凭据或 fingerprint 输出。

## 步骤 3.3：内存交换 Offer/Answer 并等待 Connected

### 当前问题

Offer 只留在 A 就像联系卡还在抽屉里。必须把 A 的完整 description 交给 B，B 生成完整 Answer 再交回
A，最后等待两端都连接。

### 修改文件

`out/learn-webrtc-minilab/main.cpp`

### 代码修改

本步把完整 Offer 交给 B，再把完整 Answer 交回 A。description 只在局部内存变量中短暂停留。

#### 3.3.1 交换 Offer 并生成 Answer

<div style="background-color:#fde2e2;border-left:5px solid #c62828;padding:10px 14px;margin:10px 0;">
<strong>删除（红色）：A gathering 完成后直接结束本轮</strong>
<pre><code>emitState(round_, "A", "offer_gathering", "complete");
return true;</code></pre>
</div>

<div style="background-color:#fff3cd;border-left:5px solid #d6a700;padding:10px 14px;margin:10px 0;">
<strong>新增（黄色）：localDescription → remoteDescription → Answer</strong>
<pre><code>const auto offer = endpointA_-&gt;localDescription();
endpointB_-&gt;setRemoteDescription(...);
endpointB_-&gt;setLocalDescription(rtc::Description::Type::Answer);</code></pre>
</div>

在 A 的 `offer_gathering complete` 输出后复制：

```cpp
const auto offer = endpointA_->localDescription();
if (!offer) return fail("offer_exchange", "description_unavailable");
const std::string offerSdp = offer->generateSdp();
endpointB_->setRemoteDescription(rtc::Description(offerSdp, "offer"));
endpointB_->setLocalDescription(rtc::Description::Type::Answer);
emitState(round_, "A_to_B", "offer_exchange", "success");

if (!waitUntil(deadline, [](const SharedState &state) {
        return state.answerGathered;
    })) {
    return fail("answer_gathering", "gathering_timeout");
}
emitState(round_, "B", "answer_gathering", "complete");
```

`localDescription()` 返回 optional-like 结果：没有可用描述时为空，所以必须先检查。`generateSdp()`
把 description 序列化成字符串；这里只作为内存搬运格式，既不写文件也不传给日志。

`rtc::Description(offerSdp, "offer")` 的第一个参数是完整 SDP 文本，第二个参数声明其类型。构造出的
临时对象按值交给 `setRemoteDescription()`，B 由此了解 A 的能力和完整候选。随后 B 显式创建
`Answer` 并异步 gathering。类型字符串写错、SDP 不完整或状态顺序错误都可能使库抛异常或进入
Failed，不能用打印原始 SDP 的方式排障。

#### 3.3.2 把 Answer 交回 A 并等待双方 Connected

<div style="background-color:#fff3cd;border-left:5px solid #d6a700;padding:10px 14px;margin:10px 0;">
<strong>新增（黄色）：Answer 交换和双端连接门禁</strong>
<pre><code>const auto answer = endpointB_-&gt;localDescription();
endpointA_-&gt;setRemoteDescription(...);
waitUntil(deadline, bothConnected);</code></pre>
</div>

紧接上一块复制：

```cpp
const auto answer = endpointB_->localDescription();
if (!answer) return fail("answer_exchange", "description_unavailable");
const std::string answerSdp = answer->generateSdp();
endpointA_->setRemoteDescription(rtc::Description(answerSdp, "answer"));
emitState(round_, "B_to_A", "answer_exchange", "success");

if (!waitUntil(deadline, [](const SharedState &state) {
        return state.offerConnected && state.answerConnected;
    })) {
    return fail("connection", "connection_timeout");
}
emitState(round_, "both", "connection", "connected");
```

Offer 和 Answer 使用同一组 API，但方向相反。最后的 predicate 要求两个端点回调都报告 Connected；
只看到 A Connected 不能代表 B 的状态已被控制线程确认。三次等待都共享同一 deadline，所以任一阶段
变慢都会减少后续预算，而不会把总上限放大为 60 秒。

执行链是 A 创建 DataChannel/Offer → A gathering complete → 内存 Offer 交给 B → B 生成并完成
Answer gathering → 内存 Answer 交回 A → 等待 A/B Connected。description 的数据所有权只在局部
`optional` 和 PeerConnection 之间转移，不写文件、不输出内容。当前能力是单进程 non-trickle 协商；
它尚未接收远端 DataChannel、等待 open 或发送消息，也不能证明 LAN、NAT 或公网连通。

本章仍只显式关闭 A 创建的 `offerChannel_`，再关闭两个 PeerConnection。B 对远端 DataChannel 的
接收、所有权转移和 open/message 回调从第 4 章开始加入；第 5 章再补齐
`closing/generation/resetCallbacks()` 的最终关闭门禁。

### 构建

```powershell
cmake --build 'out/build-webrtc-minilab-learning'
```

### 运行或测试

```powershell
& 'out/build-webrtc-minilab-learning/webrtc_minilab.exe'
```

### 观察证据

实际重放成功，控制线程的因果检查点为：

```text
round=1 endpoint=A stage=offer_gathering state=complete
round=1 endpoint=A_to_B stage=offer_exchange state=success
round=1 endpoint=B stage=answer_gathering state=complete
round=1 endpoint=B_to_A stage=answer_exchange state=success
round=1 endpoint=both stage=connection state=connected
round=1 endpoint=both stage=complete state=ok
```

### 通过条件

- Offer 和 Answer 都在 gathering complete 后读取；
- 两端均 Connected；
- 整个流程共用同一个 deadline；
- 会话材料只在内存，不显示也不落盘。

## 原理和类比：一次性交付完整联系卡

```text
A create Offer ─ wait gathering complete ─ Offer(内存) ─> B
B set Offer + create Answer ─ wait complete ─ Answer(内存) ─> A
A/B ICE connectivity checks ────────────────────────────> Connected
```

- Offer/Answer 是本次谁先提议、谁回应，与 publisher/viewer 无关；
- SDP 是会话描述容器，不是账号或身份；
- candidate 是可能可走的道路入口，ICE 还要实际检查；
- non-trickle 是“等地址收齐后一次性交付”，适合没有持续信令通道的实验；
- 空 ICE server 只产生 host candidate，单机成功不能证明跨 NAT 或公网成功。

## 本章小实验

完整对调协商角色：让 B 成为 Offerer、A 成为 Answerer。不要只换输出标签。

### 答案代码

<div style="background-color:#fde2e2;border-left:5px solid #c62828;padding:10px 14px;margin:10px 0;">
<strong>删除（红色）：A=Offerer、B=Answerer 的 run()</strong>
<pre><code>makePeer(configuration, "A", true);
endpointA_-&gt;createDataChannel("minilab");
endpointB_-&gt;setLocalDescription(rtc::Description::Type::Answer);</code></pre>
</div>

<div style="background-color:#fff3cd;border-left:5px solid #d6a700;padding:10px 14px;margin:10px 0;">
<strong>新增（黄色）：B=Offerer、A=Answerer</strong>
<pre><code>makePeer(configuration, "B", true);
endpointB_-&gt;createDataChannel("minilab");
endpointA_-&gt;setLocalDescription(rtc::Description::Type::Answer);</code></pre>
</div>

用下面完整函数替换 `MiniLabSession::run()`：

```cpp
bool run(int round)
{
    round_ = round;
    const Deadline deadline = Clock::now() + roundTimeout;
    rtc::Configuration configuration;
    configuration.iceServers.clear();
    configuration.disableAutoNegotiation = true;
    configuration.enableIceTcp = false;

    endpointA_ = makePeer(configuration, "A", false);
    endpointB_ = makePeer(configuration, "B", true);

    offerChannel_ = endpointB_->createDataChannel("minilab");
    endpointB_->setLocalDescription(rtc::Description::Type::Offer);
    if (!waitUntil(deadline, [](const SharedState &state) {
            return state.offerGathered;
        })) {
        return fail("offer_gathering", "gathering_timeout");
    }
    emitState(round_, "B", "offer_gathering", "complete");

    const auto offer = endpointB_->localDescription();
    if (!offer) return fail("offer_exchange", "description_unavailable");
    const std::string offerSdp = offer->generateSdp();
    endpointA_->setRemoteDescription(rtc::Description(offerSdp, "offer"));
    endpointA_->setLocalDescription(rtc::Description::Type::Answer);
    emitState(round_, "B_to_A", "offer_exchange", "success");

    if (!waitUntil(deadline, [](const SharedState &state) {
            return state.answerGathered;
        })) {
        return fail("answer_gathering", "gathering_timeout");
    }
    emitState(round_, "A", "answer_gathering", "complete");

    const auto answer = endpointA_->localDescription();
    if (!answer) return fail("answer_exchange", "description_unavailable");
    const std::string answerSdp = answer->generateSdp();
    endpointB_->setRemoteDescription(rtc::Description(answerSdp, "answer"));
    emitState(round_, "A_to_B", "answer_exchange", "success");

    if (!waitUntil(deadline, [](const SharedState &state) {
            return state.offerConnected && state.answerConnected;
        })) {
        return fail("connection", "connection_timeout");
    }
    emitState(round_, "both", "connection", "connected");
    return true;
}
```

不能只交换 `setLocalDescription()` 两行。创建 DataChannel 的端点、读本地 description 的端点、
写远端 description 的端点以及输出方向必须作为一个完整角色集合同时对调。`offerSide` 的布尔状态
也必须对调，否则 gathering/connected 会写进错误字段并导致超时。远端 DataChannel 的接收和打开
属于下一章，不参与本章的信令角色反转。

实验仍然只有 A 创建/接收通道这一业务差异，没有媒体方向：两端都只是 DataChannel 测试端点。这正
是“Offerer 不等于 publisher”的可运行证据。

### 实验验证

```powershell
cmake --build 'out/build-webrtc-minilab-learning'
& 'out/build-webrtc-minilab-learning/webrtc_minilab.exe'
```

实测反向版本退出 0，并输出 `endpoint=B_to_A stage=offer_exchange state=success`、双方 Connected 和最终
成功。实验完成后按红/黄说明把所有对调点恢复为 A=Offerer，保证第 4 章从正确检查点继续。

### 实验通过条件

- B 收集 Offer，A 收集 Answer；
- 双方仍 Connected；
- 能解释“Offerer 不是业务发送方”的结论。

## 故障恢复

| 固定分类 | 含义 | 检查 |
| --- | --- | --- |
| `gathering_timeout` | 截止前未完成收集 | DataChannel、`setLocalDescription()` 和回调 |
| `description_unavailable` | 完成后取不到本地描述 | description 调用顺序与对象生命周期 |
| `connection_timeout` | 描述已交换但未双端 Connected | type 是否配对、是否等待 complete |
| `library_failure` | 库调用异常 | 精确版本和调用顺序；不打印 `what()` |

## 当前完整目录结构

```text
out/learn-webrtc-minilab/
├─ CMakeLists.txt
└─ main.cpp   # 两端 non-trickle 协商并 Connected
```

## 本章检查点

- [ ] 能说清 description API 的调用顺序。
- [ ] 理解 SDP、candidate、ICE 的不同职责。
- [ ] 不使用固定 sleep。
- [ ] 正向和反向 Offerer 实验都成功。
- [ ] 输出无会话敏感材料。

## 本章总结

你已经让两个真实 PeerConnection 完成 non-trickle 协商。信令只是搬运 description；ICE 负责验证道路；
Offerer 只是本次先发提议的一方，不等于发布者。

## 下一章将解决什么问题

Connected 只表示底层连接建立。下一章会等待双方 DataChannel 打开，再发送 `ping → pong` 验证应用消息。

- 上一章：[第 2 章](02_sdp_offer_answer_and_manual_signaling.md)
- 下一章：[第 4 章](04_rtp_rtcp_h264_media_pipeline.md)
