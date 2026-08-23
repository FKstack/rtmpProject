# WebRTC V2 Week 4 资产边界

## 资格样本

总控脚本在 Git 忽略目录本地生成 6 秒合成 MP4，不下载任何媒体。生成参数固定为 1280×720、30 fps、
H.264 Constrained Baseline Level 3.1、无 B 帧、GOP 30、无音频；ffprobe 验证后才复制到客户端旁的
`webrtc-assets/sample.mp4`。样本和其运行时副本均不提交、不安装、不打包。

当前本机已有的其他 MP4 明确不用于资格：其编码属性与 `profile-level-id=42e01f` 不匹配，且许可
来源未知。未知许可媒体不得进入 Git、发布包或自动下载入口。

## 路径与 CLI

客户端不接受任意文件路径。唯一合法来源是 `--source=sample`，解析到可执行文件旁的固定相对位置。
viewer、camera、loop、路径参数和未知参数全部拒绝。缺少样本返回 endpoint/媒体类错误，不回显绝对
路径。

## 输出与证据

仓库只记录脱敏计数、状态和错误枚举。SDP、candidate 内容、IP、端口、fingerprint、ICE 凭据、
STUN/TURN URL、Token、完整 UUID、绝对路径、原始媒体和大段日志都留在仓库之外或忽略目录中。
