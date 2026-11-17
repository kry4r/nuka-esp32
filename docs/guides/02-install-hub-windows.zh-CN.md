# Windows 安装 nuka-hub

适用于 Windows 10/11 x64 或 ARM64、Python 3.11 及以上。普通安装使用当前用户权限；防火墙规则和 LHM 的提权任务需要管理员权限。默认界面语言为中文。开发与验证时关闭天气、AI 和服务器上游，或只使用本地 fixture。

安装 uv 的分平台入口如下；本篇后续命令使用 PowerShell。

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

新开终端，运行 `uv --version`，应显示版本号。安装已发布的版本：

```powershell
uv tool install --python 3.12 'nuka-hub[all]==0.1.0'
nuka --version
nuka init
```

尚未发布到 PyPI 时，在源码仓库根目录改用 `uv tool install --python 3.12 './hub[all]'`。不应从同名的其他项目安装。`nuka --version` 应为 `0.1.0`。初始化选择 `zh-CN`，保存只显示一次的管理员令牌，首次可跳过可选集成。配置默认写入 `%APPDATA%\nuka\config.toml`；自定义路径统一传 `--config`。

对应的配置片段如下，部署地址由使用者填写，占位地址不能直接用于真实设备。

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

先以前台方式运行，另开 PowerShell 检查健康状态：

```powershell
nuka hub
```

```powershell
Invoke-RestMethod http://localhost:8790/api/v1/health
nuka doctor
```

健康接口应含 `ok=true`、`proto=1`；`doctor` 的 `hub_http` 应为 `ok`。浏览器打开 `http://localhost:8790`，用管理员令牌登录，进入中文配置管理页。前台验证后按 Ctrl+C 停止，再安装登录任务：

```powershell
nuka service install
nuka service status
schtasks /Run /TN nuka-hub
```

应看到 `nuka-hub` 计划任务；再次检查健康接口。服务在用户登录后启动。LHM 单独按[硬件传感器引导](04-hardware-sensors.zh-CN.md)配置，需要时从管理员 PowerShell 使用 `nuka service install --elevated`。

局域网连接需要在管理员 PowerShell 放行选定端口；规则只适用于专用网络和本地子网：

```powershell
New-NetFirewallRule -DisplayName 'nuka-hub TCP' -Direction Inbound -Action Allow -Protocol TCP -LocalPort 8790 -RemoteAddress LocalSubnet -Profile Private
New-NetFirewallRule -DisplayName 'nuka-hub mDNS' -Direction Inbound -Action Allow -Protocol UDP -LocalPort 5353 -RemoteAddress LocalSubnet -Profile Private
nuka doctor
```

预期 `firewall` 提示找到入站 TCP 规则；mDNS 成功时为 `ok`。用另一台局域网机器访问配置中的地址，确认网络实际可达，再按[设备配对引导](03-pair-device.zh-CN.md)输入 Wi-Fi。

卸载与回滚：先执行以下命令；配置、数据库和钥匙串默认保留。升级前另行备份这些文件，恢复旧版本时也恢复对应备份。

```powershell
nuka service uninstall
uv tool uninstall nuka-hub
Get-NetFirewallRule -DisplayName 'nuka-hub TCP','nuka-hub mDNS' | Remove-NetFirewallRule
# 在仍已发布的版本之间回滚：
uv tool install --force --python 3.12 'nuka-hub[all]==0.1.0'
```

常见问题：

| 现象 / doctor 输出 | 排查 |
|---|---|
| `hub_http: degraded`，`Hub not reachable; start nuka hub and check hub.port` | 用前台 `nuka hub` 查看配置错误；确认服务使用同一个配置路径 |
| `port: error`，`Port occupied by another process` | 停止重复的前台 Hub，或修改 `hub.port` 后重装任务；不要同时运行两个 Hub 占同一端口 |
| `firewall: degraded`，`No inbound TCP allow rule found for the configured port` | 核对端口、网络类型和管理员权限；只添加本篇所需规则 |
| `lhm_privilege: degraded` | 以管理员身份运行 LHM；普通 psutil 采集不需要提权 |
| `usb: disabled` | 检查数据线及设备是否已运行 TinyUSB 固件；下载模式不会显示 nuka CDC |

uv 安装依据：[官方安装文档](https://docs.astral.sh/uv/getting-started/installation/)。服务、区域与配置行为以本仓库 CONFIG / PROTOCOL 为准。
