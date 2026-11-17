# 刷入与恢复固件

适用于 Waveshare ESP32-S3-Touch-AMOLED-2.16、16 MiB Flash。使用 Windows PowerShell、macOS Terminal 或 Linux shell，安装 uv，并关闭占用串口的终端、IDE 和 Hub 配对进程。Linux 用户需要串口访问权限；不要以 root 日常运行 Hub。把下列 `<PORT>` 替换为实际探测的设备，不把串口名写进仓库。

## 备份与验证

三个平台执行相同命令（PowerShell 也支持）：

```sh
uvx --from esptool==4.11.0 esptool.py --chip esp32s3 --port <PORT> read_flash 0 0x1000000 factory-16m.bin
```

应该看到读取完成，文件大小必须为 **16777216** 字节。备份和校验值只留在本机，不提交 git。Windows 验证：

```powershell
(Get-Item -LiteralPath factory-16m.bin).Length
Get-FileHash -LiteralPath factory-16m.bin -Algorithm SHA256
```

macOS 验证：`stat -f%z factory-16m.bin` 和 `shasum -a 256 factory-16m.bin`；Linux 验证：`stat -c%s factory-16m.bin` 和 `sha256sum factory-16m.bin`。任一步失败都先修复，不刷写。已有 NukaVault 时另行导出加密备份。

## 命令行刷入

从本地构建或正式 Release 取得 `bootloader.bin`、`partition-table.bin`、`ota_data_initial.bin`、`nuka-esp32.bin`、`nukapack.bin`，在同一目录执行：

```sh
uvx --from esptool==4.11.0 esptool.py --chip esp32s3 --port <PORT> write_flash 0x0 bootloader.bin 0x8000 partition-table.bin 0xF000 ota_data_initial.bin 0x30000 nuka-esp32.bin 0x830000 nukapack.bin
```

工具应逐个写入并验证。只写列出的分区，不使用整片擦除；资源写入 bank 0。已有 nuka 系统的日常升级使用 OTA，让 inactive bank 验证后原子切换。不要向活跃资源 bank 直接覆盖更新。刷写后退出工具、重启，应该看到 112 px 表盘时间和中英文提示；三键与触摸可操作。固件和资源包 ABI 必须匹配。

项目也交付 ESP Web Tools 网页刷机页：确认完整备份后连接、刷入、关闭窗口、执行配对。页面的本地 vendor 固定关闭整片擦除，使用相同的五个独立分区镜像。HTTPS 或 localhost 上的桌面 Chrome / Edge 支持 Web Serial。本机自动开发刷写使用上面的命令行流程。

## 配网与对应配置

启动 Hub 后运行 `nuka pair`，在本机交互输入 Wi-Fi 凭据并核对设备配对码。命令由 USB 直接与设备通信；关闭串口监视器后再运行。凭据不放入 config.toml 或仓库。Hub 配置示例：

```toml
[hub]
name = "<HUB_NAME>"
host = "0.0.0.0"
port = 8790
public_url = "http://192.0.2.10:8790"
```

`192.0.2.10` 是文档占位符，运行时替换为你的地址。随后执行 `nuka doctor`；USB 应出现 `{"name":"usb","kind":"system","health":"ok"}`（还会带 detail/doc），Hub 应有 `hub_http: ok`。设备显示已配对、Hub 在线，再验证按键。

## 恢复与卸载

先停止 Hub / 串口工具。恢复同一块设备的完整原厂备份：

```sh
uvx --from esptool==4.11.0 esptool.py --chip esp32s3 --port <PORT> write_flash 0 factory-16m.bin
```

恢复后重启，确认回到原厂界面；这是用备份写回完整地址范围，不需要额外整片擦除命令。不要烧录 eFuse，不开启 Secure Boot 或 Flash 加密。恢复会替换 nuka 的本地数据，先保留所需的 Vault 备份。

## 常见问题

- 找不到设备：换用 USB 数据线。手动进入 ROM 时使用 **BOOT（GPIO0）**，先按住，再让开发板上电或复位，随后松开；Key3（GPIO18）是返回键。ROM 模式不运行 Flash 中的应用，屏幕可能保留旧画面。若电池维持供电导致拔插 USB 无法复位，先拔 USB、断开可接触的电池插头并等 10 秒，再按住 BOOT 接入 USB；不要仅靠拔插 USB 判断已经断电。工具确认 ROM 可通信后再刷写。当前 `sys.download` 已验证进入 ROM，但返回系统的完整流程尚未通过，限制记录在 [系统联调报告](../bench/e2e-report.md)。`doctor` 的 `usb: disabled` 会提示 `No nuka USB CDC device; check USB cable and native USB mode`；ROM 下载模式不是 nuka CDC，此时 disabled 属预期。
- 串口被占用：关闭 IDE、监视器和配对命令后重试；不要同时运行两个串口客户端。`usb: ok` 只说明枚举成功，仍需检查占用进程。
- 设备在线而 Hub 不通：启动 `nuka hub`，检查配置端口和本机防火墙。`hub_http: degraded` 提示 `Hub not reachable; start nuka hub and check hub.port`；若 `port: error` 提示 `Port occupied by another process`，先处理冲突。
- 英文资源错误页：重新生成匹配 ABI 的固件和资源包。`doctor` 不检查设备 Flash ABI；以设备错误页与 CDC `info` 为准。
