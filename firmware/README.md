# nuka-esp32 固件

面向 Waveshare ESP32-S3-Touch-AMOLED-2.16，使用 ESP-IDF 5.5、LVGL 9 和 C11。工程包含板级驱动、系统服务、App、Vault、音频与设备 UI；功能与真机验收状态见 [进度记录](../docs/bench/progress.md) 和 [系统联调报告](../docs/bench/e2e-report.md)。

- [从源码构建](../docs/guides/11-build-from-source.zh-CN.md)：工具链、字体、主题、资源包与固件构建步骤。
- [SDL 模拟器](sim/README.md)：使用协议样例在桌面预览设备 UI。
- [刷机引导](../docs/guides/01-flash-firmware.zh-CN.md)：首次刷机先完整备份原厂固件，再按分区写入。
- [第二阶段计划](../docs/PLAN-v2.md)：按键语音助手与设备 UI 的范围及验收要求。

依赖由 `idf_component.yml` 和 `dependencies.lock` 锁定；仅提交 `sdkconfig.defaults`。构建目录、`sdkconfig`、资源包和固件备份均不进入 Git。
