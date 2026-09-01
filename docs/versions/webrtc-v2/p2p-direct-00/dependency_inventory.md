# P2P-DIRECT-00 依赖与供应链基线

| 组件 | 当前真实版本 | 当前用途 | 本阶段结论 |
| --- | --- | --- | --- |
| Eclipse Paho MQTT C | 1.3.16 | legacy control 使用异步非 TLS target | fixture 单独 PRIVATE 链接 TLS target；产品迁移留到 DIRECT-02 |
| OpenSSL | 3.6.3 | Paho TLS / libdatachannel 传递依赖 | 不进入公共 link interface |
| libdatachannel | 0.24.5 | WebRTC transport | 保持 EXACT，运行行为不变 |
| Qt | 6.6.1 MSVC | UI/Core/Network/Multimedia | 不新增公共 Qt 契约 |
| FFmpeg | 8.1.2#3 | RTMP 与 H.264 解码 | 保留，未改依赖方向 |
| MSVC | 19.51.36256.0 | Windows x64 编译器 | 四矩阵记录实际版本 |
| CMake | 4.3.1-msvc1 | VS2026 fresh configure/Graphviz | 记录版本，不记录安装路径 |

Windows 依赖来自本机 vcpkg classic mode，仓库当前没有 manifest 锁定完整 dependency graph。这是供应链
可复现性风险，但本阶段只记录，不顺带迁移依赖管理。个人安装路径不得进入结果或 CMake DAG。

EMQX 6.2.3 与 Mosquitto 2.1.2 是隔离候选而非当前产品依赖；只有 vendor 版本、校验和、许可和实际
fixture 一致时才能记录通过。

最终 ARM64 RASTER/GLES3 动态依赖仍只包含 legacy `libpaho-mqtt3a.so.1`；Windows fixture 的
`paho-mqtt3as`、OpenSSL 和测试输出目录没有扩散到 ARM 或产品公共链接面。
