# macOS 安装 nuka-hub

适用于 macOS、Apple Silicon 或 Intel、Python ≥3.11。使用当前登录用户安装 LaunchAgent；开启入站访问需要管理员权限。初次验证只启用 psutil，其他上游关闭或使用本地 fixture。

先按所在平台安装 uv；本篇服务步骤只适用于 macOS。

```powershell
# Windows PowerShell
winget install --id astral-sh.uv -e
```

```sh
# macOS
brew install uv
# Linux
curl -LsSf https://astral.sh/uv/install.sh | sh
```

`uv --version` 应显示版本号。然后运行：

```sh
uv tool install --python 3.12 'nuka-hub[all]==0.1.0'
nuka --version
nuka init
```

尚未发布到 PyPI 时，从仓库根目录使用 `uv tool install --python 3.12 './hub[all]'`。预期版本为 `0.1.0`。初始化保留中文默认值并妥善保存只显示一次的管理员令牌。配置默认位于 `~/Library/Application Support/nuka/config.toml`。

配置片段如下；示例地址需替换，设备不能访问本机的 localhost。

```toml
[hub]
name = "nuka-hub"
host = "0.0.0.0"
port = 8790
lang = "zh-CN"
public_url = "http://192.0.2.10:8790"

[collectors]
enabled = ["psutil"]

[weather]
enabled = false
```

以前台方式验证，另一终端检查：

```sh
nuka hub
```

```sh
curl --fail http://localhost:8790/api/v1/health
nuka doctor
```

预期健康 JSON 为 `ok:true`、`proto:1`，`doctor` 中 `hub_http` 为 `ok`。在浏览器打开 `http://localhost:8790` 用管理员令牌登录。停止前台 Hub，再安装服务：

```sh
nuka service install
nuka service status
launchctl print "gui/$(id -u)/io.nuka.hub"
```

应看到 `io.nuka.hub` LaunchAgent；再次检查健康接口，退出登录再登录后应自动启动。不要同时保留前台 Hub。温度与媒体权限见[传感器引导](04-hardware-sensors.zh-CN.md)。

macOS 弹出网络权限时允许当前 Python 的入站连接。若需要显式加入应用防火墙：

```sh
nukaPython="$(uv tool dir)/nuka-hub/bin/python"
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --add "$nukaPython"
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --unblockapp "$nukaPython"
nuka doctor
```

`doctor` 会报告已检查防火墙，仍需从另一台局域网机器验证配置地址；发现异常时可用配对时保存的地址代替 mDNS。防火墙不需要整体关闭。

卸载 / 回滚：配置和钥匙串保留，升级前先备份数据目录。

```sh
nuka service uninstall
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --remove "$(uv tool dir)/nuka-hub/bin/python"
uv tool uninstall nuka-hub
# 已发布版本之间的回滚：
uv tool install --force --python 3.12 'nuka-hub[all]==0.1.0'
```

| 现象 / doctor 输出 | 排查 |
|---|---|
| `hub_http: degraded` | 以前台启动查看配置错误；检查 LaunchAgent 的配置路径和 Python 路径 |
| `port: error`，`Port occupied by another process` | 停止另一份 Hub；修改端口后重装 LaunchAgent |
| `mdns: degraded`，`Hub discovery failed; check UDP multicast/firewall or use the paired URL` | 检查同一局域网与 UDP 多播；使用已配对地址继续连接 |
| `firewall: degraded` | 这是人工复核提示；从第二台机器验证 TCP 8790，检查应用防火墙和系统网络权限 |
| `usb: disabled` | 检查 USB 数据线及固件模式；按[配对引导](03-pair-device.zh-CN.md)操作 |

uv 命令依据[官方文档](https://docs.astral.sh/uv/getting-started/installation/)，Hub 配置与安全策略见 CONFIG / PROTOCOL。
