# 配对设备与 USB 管理

适用于 Windows、macOS、Linux 上已经安装的 nuka-hub，以及刷入 nuka 固件的设备。使用数据线；先退出串口监视器。Hub 必须正在运行，管理员令牌已经保存。此步骤不刷机、不擦 Flash、不配置 eFuse。

## USB 配对

Windows PowerShell：

```powershell
nuka doctor
nuka pair
```

macOS：

```sh
nuka doctor
nuka pair
```

Linux：

```sh
nuka doctor
nuka pair
```

doctor 应报告 `usb: ok` 与 `hub_http: ok`。pair 自动按 VID 和产品名识别唯一设备，不把串口号写进配置。按提示填写 SSID、隐藏输入 Wi-Fi 密码；最多三个网络。填写 Hub WS 地址，例如 `ws://192.0.2.10:8790/ws/device,wss://hub.example.com/ws/device`。设备显示 Hub 名称和六位码后，核对并在设备上确认；拒绝或 60 秒超时不会写入配网。成功后 CLI 显示 `Device connected successfully.`，Web 设备页显示在线。未经 TLS 的局域网 WS 可用，但内容未加密。

Hub 配置片段：

```toml
[hub]
name = "example"
port = 8790
public_url = "https://hub.example.com"
[mdns]
enabled = true
```

Wi-Fi、设备令牌和 Hub 地址写入设备 NVS；不放在 Hub 的 TOML 中。多地址可按局域网、Tailscale、公网顺序填写，连接到的 Hub ID 必须与配对保存的一致。使用 Cloudflare Access 时，交互输入 HTTPS origin、Service Token ID 和隐藏的 secret；凭据只发送到匹配 origin，仍需设备令牌。

## SoftAP 配对

在设备配网页连接屏幕指示的 `Nuka-XXXX` 热点，打开设备显示的配置地址。填写 SSID、密码和一个 Hub WS 地址，提交后切回原网络。Hub 本地或 LAN 的 Web「设备」页出现待配对项；填写设备屏幕上的六位码与名称。应看到设备重新连接并在线。待配对项五分钟失效；输错五次后需重新发起。公网 Web 无法执行配对。

## Vault 导入导出

仅 USB 本机进程处理；Vault 数据不经过 Hub、Wi-Fi、Web 或 WS。先在设备解锁 Vault 并打开两分钟 USB 管理模式：

```sh
nuka vault export backup.nukavault
nuka vault import backup.nukavault
nuka vault import example-otpauth.txt --otpauth
```

三个平台命令相同。备份口令隐藏输入；CLI 固定使用 scrypt N=32768/r=8/p=1，仅派生密钥，设备完成 AES-GCM。备份文件只含密文；导入后应在设备核对条目。CLI 会清零可变口令和派生密钥缓冲并退出；CPython/getpass 的临时不可变副本无法保证原地擦除，因此不要声称全进程内存已完全零化。URI 文件含敏感明文，使用个人目录，不进 Git。

## 撤销与回滚

在本地 Web 设备页删除设备会撤销令牌并断开连接；重新运行 pair 可更换配置。需要恢复设备出厂配置时用设备恢复菜单，先保存加密 Vault 备份；不要整片擦除 Flash。删除不再需要的备份由用户自行处理。

## 常见错误

| doctor 输出 | 处理 |
|---|---|
| `usb: disabled`，`No nuka USB CDC device; check USB cable and native USB mode` | 检查数据线、nuka 固件和 USB 模式；只连接一台 nuka，关闭串口监视器 |
| `hub_http: degraded`，`Hub not reachable; start nuka hub and check hub.port` | 先启动 Hub，并让 pair 使用同一个配置目录 |
| `port: error`，`Port occupied by another process` | 退出占用者或修改 hub.port，再重启 Hub |
| `mdns: degraded`，`Hub discovery failed; check UDP multicast/firewall or use the paired URL` | 放行组播或使用明确的配对 URL；设备不会自动信任另一个 Hub |

拒绝、超时与 Vault 上锁是设备返回的错误，不是 doctor 的 USB 可用性探测。首次真机往返状态见 bench 报告，不以本文代替实测。
