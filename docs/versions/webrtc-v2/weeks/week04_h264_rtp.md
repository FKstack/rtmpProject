# V2 Week 04：H.264 RTP 重组

## 本周目标

把 libdatachannel Track 收到的 RTP packet 安全重组为带 generation 的 Annex-B H.264 访问单元。

## 知识

- RTP sequence number 为 16 位，timestamp 为 32 位；H.264 时钟通常为 90 kHz。
- 同一访问单元的 packet 共享 timestamp，marker 通常标记末包。
- Single NAL、STAP-A、FU-A 的边界和长度校验不同。
- SPS/PPS 是参数集，IDR 用于损坏后的解码恢复。

## 实验

- 使用固定十六进制 RTP fixture 手工解析 version、CC、extension、padding 和 payload。
- 构造跨序号和时间戳回绕的最小输入，验证展开后的时间线单调。

## 开发任务

- 实现 RTP header 安全解析，拒绝截断、非法 extension 和 padding。
- 支持 Single NAL、STAP-A、FU-A start/middle/end。
- 缓存有界 SPS/PPS；关键帧恢复时补齐必要参数集。
- 丢包、乱序或时间戳突变时丢弃当前 AU，等待/请求 IDR。
- 为 AU、缓存和 packet 设置明确字节上限。

## 验收

- 单 NAL、STAP-A、FU-A、SPS/PPS/IDR、序号/时间戳回绕通过。
- 丢包、乱序、重复、非法长度、超大数据、旧 generation 全部被安全拒绝。
- reset/stop 幂等，不产生残片或跨 session 拼接。

## 风险与停止条件

- 不把损坏 FU-A 的部分数据提交给 FFmpeg。
- 不依赖未协商的 payload type 或固定 SSRC。
- 未证明缓存有界前不得接入真实 Track。

## 下周入口

接入单路 recv-only WHEP PeerConnection 和 WHIP 参考发布器。
