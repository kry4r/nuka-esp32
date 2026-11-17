# 硬件传感器

适用于已安装 Python 3.11+ 和 nuka-hub 的 Windows、Linux、Apple Silicon macOS。psutil 负责 CPU、内存、磁盘、网络，以及硬件支持时的电池；可选采集器负责温度与 GPU。没有对应硬件的插件显示 `disabled`，没有测量值的字段不生成。配置字段见 [CONFIG](../CONFIG.md#3-完整示例与字段说明)。

## Windows PowerShell

从 [LibreHardwareMonitor 官方发布页](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/releases) 下载 ZIP，解压到自己管理的目录，然后运行：

```powershell
$lhmExe = Read-Host 'LibreHardwareMonitor.exe 的完整路径'
Start-Process -FilePath $lhmExe -Verb RunAs
```

在 LHM 中开启远程 Web 服务，绑定回环地址，保留 8085 端口。验证：

```powershell
(Invoke-RestMethod http://127.0.0.1:8085/data.json).Children.Count
```

应看到大于零的数量；返回的树包含 `SensorId`、`Value` 和 `Children`。需要管理员权限随登录启动时，在管理员 PowerShell 中创建任务：

```powershell
$lhmExe = Read-Host 'LibreHardwareMonitor.exe 的完整路径'
$taskAction = New-ScheduledTaskAction -Execute $lhmExe
$taskTrigger = New-ScheduledTaskTrigger -AtLogOn
Register-ScheduledTask -TaskName nuka-lhm -Action $taskAction -Trigger $taskTrigger -RunLevel Highest
Get-ScheduledTask -TaskName nuka-lhm | Select-Object TaskName,State
```

应显示 `nuka-lhm Ready`。NVIDIA 另需厂商驱动及 Hub 的 `nvidia` extra；先运行 `nvidia-smi --query-gpu=name --format=csv,noheader`，确认能列出 GPU。

## macOS

可选 [macmon](https://github.com/vladkens/macmon/tree/v0.10.0) 需要 Apple Silicon 和 Homebrew；Intel Mac 可以继续使用 psutil。

```sh
brew install macmon
macmon --version
macmon pipe --samples 1
```

应看到一条包含 `cpu_power`、`gpu_scaled_ratio`、`temp` 的 JSON。采集器读取当前 0.10 格式，无需 sudo。

## Linux

Debian/Ubuntu 安装 [lm-sensors](https://github.com/lm-sensors/lm-sensors)，交互确认适合自己硬件的探测步骤：

```sh
sudo apt-get install lm-sensors
sudo sensors-detect
sensors
```

应看到温度读数；只加载工具为当前硬件识别出的模块。AMD/Intel GPU 使用驱动暴露的 DRM sysfs 指标。NVIDIA 需要厂商驱动和 `nvidia` extra；用 `nvidia-smi --query-gpu=name --format=csv,noheader` 验证。

## 配置与验证

只启用对应平台的采集器。Windows 示例：

```toml
[collectors]
enabled = ["psutil", "lhm", "nvml"]
host_name = "example-desktop"

[collectors.lhm]
url = "http://127.0.0.1:8085/data.json"
interval = "2s"

[[collectors.lhm.map]]
field = "cpu.temp_c"
sensor_id = "/intelcpu/0/temperature/0"
```

macOS 的 `enabled` 使用 `["psutil", "macmon"]`；Linux 使用 `["psutil", "linux_sensors", "amd_sysfs", "nvml"]`。自定义 LHM 规则优先于内置规则，SensorId 应从自己的树中选择。三平台均运行 `nuka doctor`，再启动 `nuka hub`；应看到 psutil 为 `ok`，Hosts 卡片读数持续变化。网络和磁盘速率需要两个采样点。温度、风扇和支持的 GPU 指标由对应插件补充。

## 常见问题与回滚

`nuka doctor` 的插件条目包含 `name`、`health`、`detail`：

| 诊断信息 | 处理 |
|---|---|
| `nvml disabled: Install the nvidia extra` | 安装带 `nvidia` extra 的 nuka-hub，重启 Hub。 |
| `macmon disabled: macmon requires Apple Silicon` | 当前平台不适用，从 `enabled` 中移除。 |
| `linux_sensors disabled: No hwmon temperature sensors; install lm-sensors` | 运行 `sensors`，检查内核是否支持硬件及对应模块是否加载。 |
| `lhm error`，连接失败 | 启动 LHM、打开 Web 服务，再用前述命令核对 URL。 |

回滚时恢复原来的 `collectors` 配置并重启。Windows 用 `Unregister-ScheduledTask -TaskName nuka-lhm -Confirm:$false` 删除本篇创建的任务，再关闭 LHM。macOS 用 `brew uninstall macmon`；Debian/Ubuntu 在其他软件不再需要时用 `sudo apt-get remove lm-sensors`。sensors-detect 写入的启动模块设置需单独还原，不要卸载其他应用正在使用的驱动。


## 媒体信息与播放控制（HUB-21）

前置条件：使用当前登录的桌面会话，播放器已发布系统媒体会话；Docker 中的 Hub 不直接读取宿主桌面媒体，改在桌面运行 Agent。配置只需 `[media] enabled = true`；Agent 用 `[agent] send_media = true`。

Windows PowerShell 安装媒体与封面依赖：

```powershell
uv tool install --upgrade 'nuka-hub[media-win,images]'
nuka doctor
```

macOS 终端安装 [nowplaying-cli](https://github.com/kirtan-shah/nowplaying-cli) 2.0.0 或更新版本及封面依赖；该版本开始提供 Hub 使用的 JSON 接口（[版本说明](https://github.com/kirtan-shah/nowplaying-cli/releases/tag/v2.0.0)）：

```sh
brew install nowplaying-cli
uv tool install --upgrade 'nuka-hub[images]'
nowplaying-cli get --json title artist playbackRate
```

Linux 桌面终端安装 MPRIS 支持，保留当前用户的 `DBUS_SESSION_BUS_ADDRESS`：

```sh
uv tool install --upgrade 'nuka-hub[media-linux,images]'
nuka doctor
```

启动播放器，再启动 `nuka hub` 或 `nuka agent`。预期 Media 卡显示标题、播放状态；播放器支持时，播放/暂停、上一首、下一首可用。封面转换为 160×160 baseline JPEG，最大 20 KiB，下载 URL 包含 SHA-256；没有封面时不伪造图片。

故障时查看 Web 插件健康条目的 `media`：`Install nuka-hub[media-win,images]` / `Install nuka-hub[media-linux,images] and use the desktop D-Bus session` 表示缺少依赖；`No desktop D-Bus session` 表示启动环境不在桌面会话。Windows 使用 [SMTC](https://learn.microsoft.com/en-us/uwp/api/windows.media.control.globalsystemmediatransportcontrolssessionmanager)，Linux 播放器需要支持 [MPRIS](https://specifications.freedesktop.org/mpris-spec/latest/)；没有系统媒体会话时列表为空。macOS 的辅助工具使用私有系统接口，系统升级后需要按上游项目说明核对可用性。

回滚：将 `[media] enabled = false` 或 `[agent] send_media = false`，重启相应进程；macOS 可执行 `brew uninstall nowplaying-cli`。可选 Python extras 与 Hub 一起通过 `uv tool uninstall nuka-hub` 卸载。
