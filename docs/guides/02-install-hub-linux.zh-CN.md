# Linux 安装 nuka-hub

适用于有 systemd 用户服务的 Linux、Python ≥3.11。服务使用当前普通用户；防火墙、开机后保持用户服务需要 sudo。初次验证只启用 psutil，上游全部关闭或使用本地 fixture。

先安装 uv，以下分别是三个平台的命令；本篇后续步骤只用于 Linux。

```powershell
# Windows PowerShell
winget install --id astral-sh.uv -e
```

```sh
# macOS
brew install uv
# Linux
curl -LsSf https://astral.sh/uv/install.sh | sh
uv --version
uv tool install --python 3.12 'nuka-hub[all]==0.1.0'
nuka --version
nuka init
```

尚未发布时，从仓库根目录使用 `uv tool install --python 3.12 './hub[all]'`。预期版本为 `0.1.0`。配置位于 `~/.config/nuka/config.toml`；妥善保存初始化只显示一次的管理员令牌。

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

示例地址需替换为设备可访问的局域网地址。先以前台方式运行 `nuka hub`，另一终端执行：

```sh
curl --fail http://localhost:8790/api/v1/health
nuka doctor
```

预期健康响应包含 `ok:true`、`proto:1`，`hub_http` 为 `ok`。浏览器打开 `http://localhost:8790`，用管理员令牌登录。无桌面钥匙串的机器可显式使用文件密钥，文件不得进入仓库：

```sh
nuka secret set ai.example --file
chmod 600 ~/.config/nuka/secrets.toml
```

按提示交互输入密钥。配置中只引用密钥名称，测试期间不配置真实 AI 服务。停止前台 Hub 后安装服务：

```sh
nuka service install
nuka service status
systemctl --user status nuka-hub.service
sudo loginctl enable-linger "$USER"
```

预期用户服务为 active；重新登录或重启后再检查健康接口。若使用 UFW，只为本机实际局域网子网开放所需端口；下面子网是占位符，先替换：

```sh
sudo ufw allow from 192.0.2.0/24 to any port 8790 proto tcp
sudo ufw allow from 192.0.2.0/24 to any port 5353 proto udp
nuka doctor
```

从第二台局域网机器检查健康接口；mDNS 不通时使用配对时保存的地址。USB 权限按[设备配对引导](03-pair-device.zh-CN.md)配置；媒体和硬件采集见[传感器引导](04-hardware-sensors.zh-CN.md)。

卸载 / 回滚时保留配置、密钥和数据，升级前先备份：

```sh
nuka service uninstall
sudo ufw delete allow from 192.0.2.0/24 to any port 8790 proto tcp
sudo ufw delete allow from 192.0.2.0/24 to any port 5353 proto udp
uv tool uninstall nuka-hub
# 回到已发布的指定版本：
uv tool install --force --python 3.12 'nuka-hub[all]==0.1.0'
```

删除防火墙规则时使用安装时的实际子网。若仅为 nuka 开启 linger 且不再需要，可运行 `sudo loginctl disable-linger "$USER"`。

| 现象 / doctor 输出 | 排查 |
|---|---|
| `hub_http: degraded` | 前台启动查看配置错误；检查用户服务配置路径和端口 |
| `port: error`，`Port occupied by another process` | 停止重复 Hub；修改端口后重装服务 |
| `firewall: degraded` | 检查 UFW / nftables；从另一台机器验证 TCP 8790 |
| `mdns: degraded`，`Hub discovery failed; check UDP multicast/firewall or use the paired URL` | 检查 UDP 多播和网段隔离；使用已配对地址 |
| `usb: disabled` | 检查数据线、设备模式和串口组权限，重新登录后再试 |

uv 安装依据[官方文档](https://docs.astral.sh/uv/getting-started/installation/)。不需要关闭整个防火墙。
