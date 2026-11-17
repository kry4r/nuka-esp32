# NukaVault

适用：运行本项目固件的 ESP32-S3、支持数据传输的 USB 线，以及已安装 Python 3.11+ / nuka-hub CLI 的 Windows、macOS 或 Linux。Vault 操作由本机 `nuka` USB CLI 完成，无需启动 Hub。系统需允许当前用户访问 USB CDC；Linux 可按发行版要求加入串口设备的访问组，重新登录后验证。不要为此修改设备固件或烧录 eFuse。

在设备打开 NukaVault，首次设置并重复输入 6–8 位数字 PIN。解锁后可以查看 TOTP、生成密码和进入 USB 管理模式。管理授权只持续 2 分钟。PIN 保护设备上的库；备份口令单独保护导出的文件，恢复到另一台设备时仍使用原备份口令。请先用虚构条目验证流程。

默认是红色提示的软件降级模式：随机根密钥保存在独立 NVS 中，Flash 被读出后可以离线猜测 PIN。既有 eFuse HMAC 密钥槽可用于硬件模式，但本项目开发流程不烧录 eFuse。烧录不可逆，必须另行获得维护者明确确认。Secure Boot 和 Flash 加密未开启，能够刷入恶意固件的人可以在下次解锁时窃取 PIN；物理探针和侧信道不在防护范围内。

解锁失败次数先持久化再验证 PIN，5 次触发 5 分钟冷却，默认 10 次清空独立 Vault 分区；策略支持 20 次或关闭清空。冷却期间重新上电不会解除冷却，会重新等待完整 5 分钟。熄屏、双击 Key3、离开 Vault 超过 30 秒或解锁超时都会上锁。清空库不能代替备份。

## 配置与连接验证

Vault 不使用 `config.toml` 字段；不需要改动现有 Hub 配置，也不要为它新增地址或密钥：

```toml
# NukaVault 通过设备本地设置；没有对应的 TOML 配置段。
```

三个平台都先执行 `nuka doctor`。连接正确时会有 USB 检查，逐行 JSON 形如 `{"name":"usb","kind":"system","health":"ok","detail":"nuka USB CDC available","doc":"guides/03-pair-device"}`。doctor 只检查 CDC 是否可用，不会读取库、验证 PIN 或备份口令。关闭正在占用串口的 monitor 后，在设备上解锁并手动进入 USB 管理模式。

## 导出、恢复和 otpauth

每条 Vault 命令前都在设备上重新确认已解锁、管理模式有效。导出时设备会显示本地确认；导入先验证认证和 JSON，再显示新增/跳过数量，确认后才写入。以下命令使用相同语法。

Windows PowerShell：

```powershell
nuka doctor
nuka vault export "$env:TEMP/example.nukavault"
Get-Item "$env:TEMP/example.nukavault" | Select-Object Length
nuka vault import "$env:TEMP/example.nukavault"
nuka vault import "$env:TEMP/example-otpauth.txt" --otpauth
```

macOS：

```sh
nuka doctor
nuka vault export /tmp/example.nukavault
wc -c /tmp/example.nukavault
nuka vault import /tmp/example.nukavault
nuka vault import /tmp/example-otpauth.txt --otpauth
```

Linux：

```sh
nuka doctor
nuka vault export /tmp/example.nukavault
wc -c /tmp/example.nukavault
nuka vault import /tmp/example.nukavault
nuka vault import /tmp/example-otpauth.txt --otpauth
```

导出用隐藏提示输入备份口令，应输出 `Encrypted backup written.`，文件至少 55 字节。恢复应输出形如 `{"added":2,"skipped":0}`；在已有同名同类型条目的设备导入时会跳过它们，名称区分大小写。先导出，再在设备恢复菜单清空 NukaVault，重新设置 PIN、进入管理模式并导入，核对条目和 TOTP，才算完成恢复验证。清空是有意删除库，操作前必须确认备份和口令均可用。

`example-otpauth.txt` 为私有文件，每行一个 URI，例如下列公开 RFC 测试密钥，仅用于演示，不应保护真实账号：

```text
otpauth://totp/Example:demo?secret=GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ&issuer=Example&algorithm=SHA1&digits=6&period=30
```

备份格式为 NVB1 v1，固定 scrypt `N=32768,r=8,p=1`。CLI 只派生密钥和传输密文，AES-GCM 解密及条目解析在设备内部 RAM 中完成，不经过 Hub、网络或 Web。设备用后零化密钥和明文；CPython 中不可变临时副本不能逐一保证擦除，命令完成后 CLI 退出。Vault 分区当前 64 KiB；设备按实际分区容量限制声明的导入文件大小，空间不足会拒绝，不改动现有条目。

在文本编辑器打开一个虚构文档，选择用户名、密码或组合输入，再在设备按住确认至少 0.5 秒。组合为用户名、Tab、密码、Enter。每次都要在本机确认输入焦点；未插 USB 时不能键入。默认密码隐藏，长按显示仅持续 5 秒。

## 排查与回滚

| 现象 / doctor 输出 | 排查 |
|---|---|
| USB 检查不是 `ok` | 检查数据线、当前用户串口权限和设备 USB 模式；关闭其他串口程序，重新运行 doctor。 |
| doctor USB 为 `ok`，命令提示解锁并开启管理模式 | 在设备触摸输入 PIN，再手动进入管理模式；超过 2 分钟重新开启。 |
| doctor USB 为 `ok`，导入返回 `bad_request` | 核对备份口令、NVB1 文件、大小、otpauth 算法；错误口令不会改动现有库。doctor 不判断文件内容。 |
| doctor USB 为 `ok`，设备提示冷却 | 等待亮屏计时完成；重新上电不能绕过。不要用反复重试验证真实库。 |
| doctor USB 为 `ok`，TOTP 提示时间异常 | 通过配网的 SNTP 或 RTC 校准时间；7 天未同步会给出警告。 |

取消本地确认或等待管理模式过期会终止传输。恢复到旧数据时，使用已验证的加密备份导入；导入按类型和名称去重，不覆盖现有条目。卸载本机 CLI 不会删除设备库；需要清空时用设备恢复菜单。演示结束后删除私有 URI 文件和临时备份，正式备份另存安全位置，禁止提交到 git。
