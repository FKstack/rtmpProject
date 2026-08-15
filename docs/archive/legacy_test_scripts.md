# 已退役的本机测试脚本

> 本页仅用于解释历史文档中的脚本名称。以下脚本已从当前分支删除，不再是公共开发入口。
> 它们最后存在于提交 `1729da1`，需要研究历史实现时可通过 Git 查看，但不要直接在新机器运行。

## 删除原因

这些脚本包含维护者电脑上的 Qt、FFmpeg、nginx、盘符、显示器布局或性能采样假设，或者已被统一开发/SRS 入口替代。继续公开会让 `git clone` 后的开发者误以为它们适用于通用环境。

| 历史脚本 | 原用途 | 当前替代方式 |
|---|---|---|
| `compare_renderers.ps1` | CPU/OpenGL 长时 A/B 总控 | 历史性能结果仅作记录；按目标硬件重新设计资格测试 |
| `run_16_stream_automated_tests.ps1` | 16 路测试编排 | CMake/CTest；真实流测试按当前环境自行编排 |
| `test_16_stream_video.ps1` | 本机 16 路预录流测试 | CTest 与项目 CLI；性能资格需按目标机器重新建立 |
| `test_16_stream_live_latency.ps1` | 本机双屏延迟测试 | 按目标显示拓扑重新建立测试 |
| `test_desktop_latency.ps1` | 早期桌面延迟测试 | 当前 SRS 指南和应用指标 |
| `test_week4_multi_stream.ps1` | Week 4 多路测试 | CTest 中的多路与动态网格测试 |
| `test_week6_opengl.ps1` | Week 6 OpenGL 环境测试 | `setup_windows_dev.ps1` 与 CTest |
| `verify_rtmp_chain.ps1` | 旧 nginx-rtmp 链路 | `scripts/srs/srs_dev_wsl.ps1` 与 `scripts/srs/verify_srs_chain.ps1` |
| `verify_opengl_arm64_env.sh` | ARM64 OpenGL 检查 | `setup_linux_arm64_dev.sh --action all --render-mode gles3` |

## 从 Git 查看历史版本

下面的命令只把历史内容输出到终端，不会恢复文件：

```powershell
git show 1729da1:scripts/test_16_stream_video.ps1
```

如果确实要复用，请先移除个人路径和环境假设，并在独立分支上重新验证。当前仓库支持的入口以根目录 `README.md` 和 `docs/guides/build-and-testing/` 下的现行指南为准。
