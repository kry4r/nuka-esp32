# 总览：nuka-esp32 是什么、如何部署

nuka-esp32 是一套针对 Waveshare ESP32-S3-Touch-AMOLED-2.16 的开源固件，配合跨平台伴侣服务 **nuka-hub**，把这块 2.16 英寸 AMOLED 小方屏变成一块「开发者桌面终端」：显示电脑与服务器状态、AI 订阅余额、AI 编程 Agent 的运行状态并可在设备上审批工具调用，兼作 TOTP 验证器和密码键入器。固件（ESP-IDF 5.5 + LVGL 9）与 Hub（Python 3.11+）只通过 [PROTOCOL](../PROTOCOL.md)、[CONFIG](../CONFIG.md) 和 `protocol/` 中的 schema 通信。

本引导帮你判断需要哪些硬件、在 D1–D4 四种部署形态中选择一种，并给出最快的体验路径。安装前请阅读根目录 [AGENTS.md](../AGENTS.md)（协作约束，仅贡献者）与 [README](../../README.md)。

## 适用场景与前置条件

| 项 | 要求 |
|---|---|
| 设备 | Waveshare ESP32-S3-Touch-AMOLED-2.16（SoC ESP32-S3R8，16 MiB Flash，8 MB Octal PSRAM，480×480 AMOLED） |
| 电池 | 可选；板子自带充电与电量计（AXP2101），无电池也能用 USB 供电运行 |
| 电脑 | Windows 10/11、macOS 或 Linux；安装 Python 3.11+ 与 [uv](https://docs.astral.sh/uv/) |
| 权限 | Linux 需串口访问权限（加入 `dialout`/`uucp` 组并重新登录）；Windows 用 LibreHardwareMonitor 需管理员权限 |
| 网络 | 设备与 Hub 在同一局域网（D1/D2/D4），或经 Tailscale/隧道访问（D3） |

设备端无需安装任何东西；刷机由网页或 `esptool` 完成（见 [01-flash-firmware](01-flash-firmware.zh-CN.md)）。

## 部署形态：怎么选

| 形态 | 描述 | 典型场景 | 入口 |
|---|---|---|---|
| D1 单机 | Hub 与设备在同一局域网，Hub 采集本机指标 | 一台主力电脑 | [02-install-hub](02-install-hub-windows.zh-CN.md) |
| D2 多机 | 一台 Hub，其余机器以 Agent 角色上报 | 台式机 + 笔记本 + 家庭服务器 | [02-install-hub](02-install-hub-linux.zh-CN.md) |
| D3 公网 | 在 D1/D2 之上，Hub 经 cloudflared + Cloudflare Access 暴露 Web | 外出用手机查看、设备不在同一网络 | [09-public-access](09-public-access.zh-CN.md) |
| D4 服务器 Hub | Hub 跑在 Linux 服务器或 Docker，无本机 GUI 数据 | 7×24 运行，桌面机以 Agent 接入 | [02-install-hub-docker](02-install-hub-docker.zh-CN.md) |

选择建议：先用 D1 跑通，再把第二台机器加成 D2；需要离开家访问时才加 D3；Hub 不需要本机 GUI（温度/风扇）时放服务器用 D4。

## 快速体验（合成数据，不接真实上游）

先在 Hub 上体验全部卡片的合成数据，无需设备：

```sh
cd hub
uv sync --all-extras
uv run nuka hub --demo
```

启动时会打印管理员令牌。用浏览器打开 `http://localhost:8790`，输入令牌登录；另开终端用 demo 启动时打印的 device 令牌运行 `uv run nuka sim-device --token <YOUR_TOKEN>`。应该看到 Web 管理页和设备模拟器都能收到各卡片数据，demo 不会访问任何真实服务。退出时按 `Ctrl+C`。

## 从零部署的路径

1. [01-flash-firmware](01-flash-firmware.zh-CN.md)：备份原厂固件 → 刷入固件与资源包 → 验证表盘显示。
2. [02-install-hub](02-install-hub-windows.zh-CN.md)：安装 Hub、`nuka init`、`nuka service install`、放行防火墙端口。
3. [03-pair-device](03-pair-device.zh-CN.md)：USB 或 SoftAP 配网、配对，设备显示真实数据。
4. 按需开启可选集成：服务器监控（[05-server-sources](05-server-sources.zh-CN.md)）、AI 配额（[07-ai-quota](07-ai-quota.zh-CN.md)）、Agent 审批（[08-agent-hooks](08-agent-hooks.zh-CN.md)）、公网（[09-public-access](09-public-access.zh-CN.md)）、Vault（[10-nukavault](10-nukavault.zh-CN.md)）。
5. 每一步之后运行 `nuka doctor` 自查；汇总问题见 [12-troubleshooting](12-troubleshooting.zh-CN.md)。

## 最小配置

`nuka init` 生成 `config.toml` 后，最小可用配置如下（全部为占位符）：

```toml
[hub]
name = "<HUB_NAME>"
host = "0.0.0.0"
port = 8790
lang = "zh-CN"
timezone = "auto"
```

`192.0.2.x` / `198.51.100.x` / `203.0.113.x` 是文档占位地址；真实部署只把地址写进本机 `config.toml` 或设备 NVS，不写进仓库。各集成的配置片段见对应引导。

## 卸载与回滚

- Hub：`nuka service uninstall` 卸载开机服务，`uv tool uninstall nuka-hub` 移除命令；数据目录默认在平台用户配置目录下，删除前先导出需要的 Vault 备份。
- 设备：刷机前已按 [01-flash-firmware](01-flash-firmware.zh-CN.md) 备份原厂固件；写回 `factory-16m.bin` 即可恢复原厂界面。

## 常见问题

- 不知道从哪开始：先跑「快速体验」确认 Hub 正常，再走「从零部署的路径」第 1 步。
- `nuka doctor` 输出看不懂：`health` 为 `ok`/`degraded`/`disabled`/`error` 之一，`detail` 是原因，`doc` 指向对应引导；对照 [12-troubleshooting](12-troubleshooting.zh-CN.md)。
- 设备连不上 Hub：先确认两者在同一网络、Hub 端口放行；`doctor` 的 `hub_http`/`port`/`mdns`/`firewall` 逐项排查。
