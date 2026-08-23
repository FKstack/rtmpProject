# 第 4 章：让两个端点说话——DataChannel `ping → pong`

上一章只能证明两端 Connected。本章先等双方拿到同一个 DataChannel，再加一个只有两句话的应用协议：
A 发 `ping`，B 回 `pong`。每一步都立即运行，避免把“通道没开”和“消息协议错”混在一起。

## 本章完成后你将得到什么

- Offerer 创建、Answerer 接收同一个 DataChannel；
- 双方通道都 open 后才发送；
- 一次真实的 `ping → pong` 和固定成功阶段；
- 错误消息立即归类为 `protocol_mismatch`。

## 前置检查点

- 第 3 章 A=Offerer、B=Answerer 已恢复；
- 两端都 Connected；
- Offer/Answer 只在内存中交换，输出无敏感材料。

## 预计时间

45～60 分钟。

## 本章在最终项目中的位置

这是最终 MiniLab 唯一的应用数据。它证明 ICE/DTLS/SCTP 上的 DataChannel 通路，不包含 Track、RTP、
H.264、FFmpeg 或画面。

## 本章知识点

- `onDataChannel()`、`onOpen()`、`onMessage()`；
- `send()` 的 buffered 返回语义；
- ICE → DTLS → SCTP → DataChannel 分层；
- DataChannel 与媒体 Track 的边界。

## 当前项目目录

```text
out/learn-webrtc-minilab/
├─ CMakeLists.txt
└─ main.cpp   # 已完成 non-trickle 协商和 Connected
```

## 步骤 4.1：接住 DataChannel 并等待双方 open

### 当前问题

A 创建 channel 后，B 不能再创建一个同名 channel；B 必须用 `onDataChannel()` 接收协商来的对象。连接
状态 Connected 也不保证应用已经拿到可发送的 channel。

### 修改文件

`out/learn-webrtc-minilab/main.cpp`

### 代码修改

B 使用 `onDataChannel()` 接收 A 创建的通道；控制线程要等两端都 open，而不是只等 PeerConnection
Connected。

#### 4.1.1 接收远端通道并记录两端 open

<div style="background-color:#fff3cd;border-left:5px solid #d6a700;padding:10px 14px;margin:10px 0;">
<strong>新增（黄色）：远端通道句柄和两端 open 状态</strong>
<pre><code>std::shared_ptr&lt;rtc::DataChannel&gt; incomingChannel;
bool offerChannelOpen = false;
bool answerChannelOpen = false;
endpointB_-&gt;onDataChannel(...);</code></pre>
</div>

在 `SharedState` 的 connected 字段后复制：

```cpp
std::shared_ptr<rtc::DataChannel> incomingChannel;
bool offerChannelOpen = false;
bool answerChannelOpen = false;
```

`incomingChannel` 暂时保存 B 收到的共享句柄；两个布尔值分别表示 A 创建的通道和 B 接收的通道已经
open。状态对象由 session 和回调共同通过 `shared_ptr` 生命周期管理，字段仍受同一个 mutex 保护。
目前只允许一个 `minilab` 通道；第二个远端通道会覆盖句柄，因此这不是多通道容器。

在创建 A/B 后、A 创建 `offerChannel_` 之前复制完整的 B 接收回调：

```cpp
const std::weak_ptr<SharedState> weakState(state_);
endpointB_->onDataChannel(
    [weakState](std::shared_ptr<rtc::DataChannel> channel) {
        const auto state = weakState.lock();
        if (!state) return;

        channel->onOpen([weakState] {
            const auto live = weakState.lock();
            if (!live) return;
            const std::lock_guard<std::mutex> lock(live->mutex);
            live->answerChannelOpen = true;
            live->changed.notify_all();
        });

        const std::lock_guard<std::mutex> lock(state->mutex);
        state->incomingChannel = std::move(channel);
        state->changed.notify_all();
    });
```

**API 卡片：`PeerConnection::onDataChannel(callback)`。** 这是 libdatachannel 0.24.5 的远端
DataChannel 到达回调注册接口。callback 接收
`std::shared_ptr<rtc::DataChannel>`；PeerConnection 在协商出对端创建的通道时由库线程调用它。
参数按值传入，回调先注册子回调，再把共享句柄移动到 `SharedState`。`std::move` 只转移这份
`shared_ptr`，不会复制消息或立即销毁底层通道。

`onDataChannel()` 本身没有“通道已经可发送”的返回值；真正的 open 证据来自下一层 `onOpen()`。
weak state 提升失败表示 session 已销毁，回调直接丢弃；代码不从库回调中推进 Offer/Answer，也不
记录 label、payload 或网络材料。

在 A 创建 `offerChannel_` 后复制：

```cpp
offerChannel_->onOpen([weakState] {
    const auto state = weakState.lock();
    if (!state) return;
    const std::lock_guard<std::mutex> lock(state->mutex);
    state->offerChannelOpen = true;
    state->changed.notify_all();
});
```

**API 卡片：`DataChannel::onOpen(callback)`。** callback 不接收参数，表示该 DataChannel 的 SCTP
状态已进入可发送阶段。它可能由库线程在 A/B 不同时间触发；注册函数不转移 DataChannel 所有权，
session 仍拥有 A 的 `offerChannel_`，共享状态暂存 B 的 `incomingChannel`。回调只提升 weak state、
持锁写一个端点专属标志并通知条件变量。

两个 open 标志不能合并成“任意一端已开”，否则 A open 而 B 句柄尚未交给控制线程时就可能提前
发送。当前能力是可靠接住一条远端通道并观察双方 open；还没有消息协议，下一小节才把句柄转移给
session。

#### 4.1.2 等待完整通道并转移所有权

<div style="background-color:#fde2e2;border-left:5px solid #c62828;padding:10px 14px;margin:10px 0;">
<strong>删除（红色）：PeerConnection Connected 后立即结束</strong>
<pre><code>emitState(round_, "both", "connection", "connected");
return true;</code></pre>
</div>

<div style="background-color:#fff3cd;border-left:5px solid #d6a700;padding:10px 14px;margin:10px 0;">
<strong>新增（黄色）：两端 open 和 incomingChannel 三重门禁</strong>
<pre><code>offerChannelOpen &amp;&amp; answerChannelOpen &amp;&amp; incomingChannel</code></pre>
</div>

在连接成功输出后复制：

```cpp
if (!waitUntil(deadline, [](const SharedState &state) {
        return state.offerChannelOpen && state.answerChannelOpen &&
               static_cast<bool>(state.incomingChannel);
    })) {
    return fail("data_channel", "data_channel_timeout");
}
{
    const std::lock_guard<std::mutex> lock(state_->mutex);
    answerChannel_ = std::move(state_->incomingChannel);
}
emitState(round_, "both", "data_channel", "open");
```

在成员区加入 `std::shared_ptr<rtc::DataChannel> answerChannel_;`，并在 `close()` 开头先关闭它：

```cpp
try { if (answerChannel_) answerChannel_->close(); } catch (...) {}
```

predicate 同时检查两个 open 标志和 B 的实际句柄，防止“回调已标记 open，但控制线程还没有取得
通道”的竞态。`static_cast<bool>` 只测试 shared pointer 是否非空。移动到 `answerChannel_` 后，session
正式拥有两端通道；移动不复制对象，只转移这一份共享句柄。

执行链是：B 收到远端通道并注册 open → A/B 各自回调更新状态 → 控制线程三重检查 → 在锁内移动
句柄 → 锁外继续。当前通道能打开，但还没有消息协议。

### 构建

```powershell
cmake --build 'out/build-webrtc-minilab-learning'
```

### 运行或测试

```powershell
& 'out/build-webrtc-minilab-learning/webrtc_minilab.exe'
```

### 观察证据

实际重放新增：

```text
round=1 endpoint=both stage=data_channel state=open
round=1 endpoint=both stage=complete state=ok
```

程序还没有消息阶段，这是有意的中间检查点。

### 通过条件

- A 创建、B 接收 channel；
- 双方 `onOpen` 都到达才输出 `data_channel open`；
- 没有发送消息，没有固定 sleep。

## 步骤 4.2：加入固定协议 `ping → pong`

### 当前问题

通道打开只证明有一个寄件窗口，不能证明应用协议正确。需要让两端只接受预期的文本，并让错误类型或
内容立即变成固定分类。

### 修改文件

`out/learn-webrtc-minilab/main.cpp`

### 代码修改

`onMessage()` 只把消息归类为“预期文本”或固定协议错误；payload 本身不进入输出。

#### 4.2.1 为 A/B 注册消息回调

<div style="background-color:#fff3cd;border-left:5px solid #d6a700;padding:10px 14px;margin:10px 0;">
<strong>新增（黄色）：ping/pong 状态和固定错误</strong>
<pre><code>bool pingReceived = false;
bool pongReceived = false;
const char *error = "message_timeout";</code></pre>
</div>

在 `SharedState` 中加入上面三个字段。然后在 B 的 `onOpen()` 后加入：

```cpp
channel->onMessage([weakState](rtc::message_variant value) {
    const auto *text = std::get_if<std::string>(&value);
    const auto live = weakState.lock();
    if (!live) return;
    const std::lock_guard<std::mutex> lock(live->mutex);
    if (text == nullptr || *text != "ping") {
        live->failed = true;
        live->error = "protocol_mismatch";
    } else {
        live->pingReceived = true;
    }
    live->changed.notify_all();
});
```

在 A 的 `onOpen()` 后加入对称回调：

```cpp
offerChannel_->onMessage([weakState](rtc::message_variant value) {
    const auto *text = std::get_if<std::string>(&value);
    const auto state = weakState.lock();
    if (!state) return;
    const std::lock_guard<std::mutex> lock(state->mutex);
    if (text == nullptr || *text != "pong") {
        state->failed = true;
        state->error = "protocol_mismatch";
    } else {
        state->pongReceived = true;
    }
    state->changed.notify_all();
});
```

**API 卡片：`DataChannel::onMessage(callback)`。** libdatachannel 0.24.5 会为文本或二进制消息调用
该回调，参数类型是 `rtc::message_variant`。它可以保存文本或二进制消息；
`std::get_if<std::string>(&value)` 的参数是 variant
地址；当当前值是文本时返回指针，否则返回 `nullptr`，不会抛 `bad_variant_access`。消息对象只在回调
期间有效；代码在持锁期间比较内容并只保存布尔结果，不保存 payload。

B 只接受 `ping`，A 只接受 `pong`。错误时设置 `failed` 和固定 `protocol_mismatch`，随后唤醒控制线程。
回调不自动发送回复，因为本章刻意让所有协议步骤仍由控制线程排序。

#### 4.2.2 在控制线程执行 ping → pong

<div style="background-color:#fde2e2;border-left:5px solid #c62828;padding:10px 14px;margin:10px 0;">
<strong>删除（红色）：通道 open 后立即返回成功</strong>
<pre><code>emitState(round_, "both", "data_channel", "open");
return true;</code></pre>
</div>

<div style="background-color:#fff3cd;border-left:5px solid #d6a700;padding:10px 14px;margin:10px 0;">
<strong>新增（黄色）：发送、等待接收证据、再回复</strong>
<pre><code>offerChannel_-&gt;send("ping");
waitUntil(deadline, pingReceived);
answerChannel_-&gt;send("pong");
waitUntil(deadline, pongReceived);</code></pre>
</div>

在 `data_channel open` 输出后复制：

```cpp
(void)offerChannel_->send(std::string("ping"));
if (!waitUntil(deadline, [](const SharedState &state) {
        return state.pingReceived;
    })) return fail("ping_pong", stateError());
(void)answerChannel_->send(std::string("pong"));
if (!waitUntil(deadline, [](const SharedState &state) {
        return state.pongReceived;
    })) return fail("ping_pong", stateError());
emitState(round_, "both", "ping_pong", "ok");
```

在 `waitUntil()` 后加入：

```cpp
const char *stateError()
{
    const std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->error;
}
```

**API 卡片：`DataChannel::send(message)`。** libdatachannel 0.24.5 的文本重载接收字符串并返回布尔
值，表示消息是否被接受进入发送缓冲；它不是远端送达回执。
这里显式转为 `void`，因为真正的协议成功条件是 B 回调设置 `pingReceived`，随后 A 回调设置
`pongReceived`。`stateError()` 持锁读取固定字符串指针；字符串字面量为静态生命周期，调用者不释放。

ping 和 pong 等待继续使用本轮同一 deadline。完成输出不包含 payload，只说明固定协议状态为 `ok`。
这条链证明 ICE、DTLS、SCTP 和 DataChannel 已协作，但没有 Track、RTP 或 H.264。

### 构建

```powershell
cmake --build 'out/build-webrtc-minilab-learning'
```

### 运行或测试

```powershell
& 'out/build-webrtc-minilab-learning/webrtc_minilab.exe'
```

### 观察证据

实际重放退出 0，尾部为：

```text
round=1 endpoint=both stage=connection state=connected
round=1 endpoint=both stage=data_channel state=open
round=1 endpoint=both stage=ping_pong state=ok
round=1 endpoint=both stage=complete state=ok
```

### 通过条件

- 先 open，再 ping/pong；
- B 只接受 `ping`，A 只接受 `pong`；
- 结果由接收回调确认，不解释 `send()` 的 bool 为送达结果；
- 不输出 payload 和网络材料。

## 原理和类比：上锁道路上的寄件窗口

```text
ICE          找到可走的道路
DTLS         验证对方并协商密钥
SCTP         在道路上提供有边界、可排序的数据消息
DataChannel  应用拿到的寄件窗口
```

视频不是更大的 DataChannel 消息。未来媒体路径会是：

```text
H.264 Access Unit → RTP packetizer → Track → SRTP
SRTP → Track → depacketizer → H.264 Access Unit → FFmpeg
```

因此本章成功只证明数据通路，不证明 Track、RTP/H.264、解码或出画。

## 本章小实验

把 A 发出的 `ping` 改成 `hello`。预测程序是在 20 秒后 timeout，还是立即出现协议分类？

### 答案代码

<div style="background-color:#fde2e2;border-left:5px solid #c62828;padding:10px 14px;margin:10px 0;">
<strong>删除（红色）：发送协议规定的 ping</strong>
<pre><code>(void)offerChannel_-&gt;send(std::string("ping"));</code></pre>
</div>

<div style="background-color:#fff3cd;border-left:5px solid #d6a700;padding:10px 14px;margin:10px 0;">
<strong>新增（黄色）：故意发送不匹配的 hello</strong>
<pre><code>(void)offerChannel_-&gt;send(std::string("hello"));</code></pre>
</div>

可直接复制替换发送行：

```cpp
(void)offerChannel_->send(std::string("hello"));
```

B 的 `onMessage()` 仍只接受 `ping`，因此它会设置 `failed=true` 和 `protocol_mismatch`，唤醒正在等待
`pingReceived` 的控制线程。实验没有修改网络、超时或异常处理，因而失败类别可以确定归因于应用层
消息协议。完成观察后把该行恢复为 `ping` 并重新运行，成功路径才算恢复。

### 实验验证

```powershell
cmake --build 'out/build-webrtc-minilab-learning'
& 'out/build-webrtc-minilab-learning/webrtc_minilab.exe'
$LASTEXITCODE
```

实测立即输出 `stage=ping_pong state=protocol_mismatch`，最后 `complete state=failed`，退出码为 1。将
`hello` 恢复为 `ping`，重新构建后应再次退出 0。

### 实验通过条件

- 错误版本返回 1 且固定分类为 `protocol_mismatch`；
- 输出不回显 `hello`；
- 恢复后 `ping_pong state=ok`。

## 故障恢复

| 分类 | 检查 | 不要做 |
| --- | --- | --- |
| `data_channel_timeout` | `onDataChannel`、双方 `onOpen` | 增加 sleep |
| `message_timeout` | 回调是否注册并通知 | 打印所有 payload |
| `protocol_mismatch` | label、消息类型和固定词 | 把未知消息原样写日志 |

## 当前完整目录结构

```text
out/learn-webrtc-minilab/
├─ CMakeLists.txt
└─ main.cpp   # Connected + DataChannel ping/pong
```

## 本章检查点

- [ ] 能解释 `onDataChannel` 与 `createDataChannel` 的不同端点职责。
- [ ] 能解释 `send()` 返回 false 的 buffered 语义。
- [ ] 能按 ICE → DTLS → SCTP → DataChannel 讲清分层。
- [ ] 协议错误实验立即、脱敏地失败。
- [ ] 知道这不是视频实现。

## 本章总结

你让两端经真实 DataChannel 完成了一个最小应用协议。底层连接、通道可用和消息正确是三个不同检查点，
失败时不再只能得到笼统的“WebRTC 不工作”。

## 下一章将解决什么问题

成功跑一次还不够。下一章把错误、晚到回调、重复关闭、参数边界和全局 Cleanup 收紧为有界生命周期。

- 上一章：[第 3 章](03_ice_stun_turn_and_public_networks.md)
- 下一章：[第 5 章](05_libdatachannel_qt_integration.md)
