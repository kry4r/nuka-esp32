# 故障排查与 doctor 输出对照

本引导汇总 nuka-esp32 的常见问题、`nuka doctor` 的每一项输出含义，以及对应修复和引导链接。遇到问题时先跑 doctor，再按下表对照；`doctor` 只做检查、不修改任何配置或防火墙规则。

前置条件：已安装 `nuka`（见 [02-install-hub](02-install-hub-windows.zh-CN.md)），设备已刷入固件（[01-flash-firmware](01-flash-firmware.zh-CN.md)）。

## 运行 doctor

三个平台命令相同：

```sh
nuka doctor
```

输出是一行一个 JSON 对象，字段固定为：

```json
{"name": "usb", "kind": "system", "health": "ok", "detail": "nuka USB CDC available", "doc": "guides/03-pair-device"}
```

- `health`：`ok`（正常）、`degraded`（可用但需留意）、`disabled`（未启用，属预期）、`error`（需要处理）。
- `detail`：具体原因，按字面判断即可。
- `doc`：对应引导的相对路径（相对 `docs/guides/`），直接打开查看详细步骤。

## doctor 检查项对照

| 检查项 | 含义 | 正常输出 | 异常输出与处理 |
|---|---|---|---|
| `usb` | 是否枚举到 nuka USB CDC（VID 0x303A / PID 0x4001） | `ok` + `nuka USB CDC available` | `disabled` + `No nuka USB CDC device…`：换数据线、确认原生 USB 模式；ROM 下载模式不是 CDC，此时 disabled 属预期，见 [01-flash-firmware](01-flash-firmware.zh-CN.md) |
| `hub_http` | 本机 Hub 健康接口是否可达 | `ok` + `Hub health reachable` | `degraded` + `Hub not reachable; start nuka hub and check hub.port`：先启动 Hub，核对端口 |
| `port` | 配置端口是否被占用 | `ok` + `Port available`（或 `Port is owned by the running Hub`） | `error` + `Port occupied by another process`：换端口或停掉占用进程，见 [02-install-hub](02-install-hub-windows.zh-CN.md) |
| `mdns` | mDNS 能否发现已配对 Hub | `ok` + `Paired Hub discovered over _nuka._tcp` | `degraded` + `Hub discovery failed…`：检查 UDP 组播/防火墙，或改用已配对的 URL；`disabled` + `mDNS disabled in config` 表示配置关闭，属预期 |
| `firewall` | 防火墙是否放行配置端口 | `ok` + `Inbound TCP allow rule found`（或防火墙已关） | `degraded` + `No inbound TCP allow rule found…`：放行端口；`…needs administrator permission`：以管理员运行 |
| `lhm_privilege` | （仅 Windows）LibreHardwareMonitor 是否需要管理员 | 无该行（已是管理员） | `degraded` + `LibreHardwareMonitor needs administrator privileges`：以管理员运行 LHM，见 [04-hardware-sensors](04-hardware-sensors.zh-CN.md) |
| 各插件（collectors / servers / ai / weather / media） | 各数据源插件的健康状态 | `health: ok` | `health: error` 或 `disabled`，`detail` 给出原因，按 `doc` 指向的引导处理 |

插件行的 `kind` 是采集器/数据源/配额等的类型，`doc` 直接指向对应引导（如 `07-ai-quota`、`05-server-sources`）。`disabled` 通常表示该集成未配置或缺少可选依赖，属预期；`error` 才是需要处理的失败。

## 常见问题对照表

| 现象 | 常见原因 | doctor 输出 / 命令 | 处理 |
|---|---|---|---|
| 设备屏幕不亮、无画面 | 未刷固件 / 资源包 ABI 不匹配 / 供电不足 | — | 按 [01-flash-firmware](01-flash-firmware.zh-CN.md) 刷入匹配 ABI 的固件与资源包，确认 USB 供电 |
| 刷机时找不到设备 | 未进 ROM 下载模式 / 串口被占用 / 数据线只能充电 | `usb: disabled`（ROM 模式属预期） | 按住 BOOT（GPIO0）再上电/复位；关闭占用串口的 IDE、监视器和配对命令；换数据线，见 [01-flash-firmware](01-flash-firmware.zh-CN.md) |
| `nuka pair` 失败 | 串口被占用 / 设备未配对 / 配对码输错 | `usb: disabled` | 关闭串口监视器后重试；核对设备屏幕六位码；待配对项五分钟失效，输错五次需重新发起，见 [03-pair-device](03-pair-device.zh-CN.md) |
| 设备显示离线、Hub 不推送 | Hub 未启动 / 端口未放行 / 不同网络 | `hub_http: degraded`、`port: error`、`firewall: degraded`、`mdns: degraded` | 启动 Hub、核对端口、放行防火墙、确认同网，见 [02-install-hub](02-install-hub-windows.zh-CN.md) 与 [03-pair-device](03-pair-device.zh-CN.md) |
| AI 卡提示「需重登」 | 上游凭证失效（或历史版本误报 `error` 为 `reauth`） | 插件 `health: ok`，账户 `status` 由 Hub 状态接口给出 | 先到 CPAMP/CPA 确认账号；确需重登则重新 OAuth，否则升级 Hub 到修复版本，见 [07-ai-quota](07-ai-quota.zh-CN.md) |
| AI provider 熔断（`auth_failed`） | 密钥错误，触发熔断防封 IP | 插件 `health: error`，`detail` 含原因 | 用 `nuka secret set` 修正密钥，再点 Web 上的「重试」；**同一 IP 认证失败 5 次会被封 30 分钟**，见 [07-ai-quota](07-ai-quota.zh-CN.md) |
| 硬件传感器字段缺失 | 未装 LHM（Windows）/ lm-sensors（Linux）/ macmon（macOS），或无该硬件 | 对应采集器 `disabled` + 原因 | 按 [04-hardware-sensors](04-hardware-sensors.zh-CN.md) 安装并开启 Web 服务 / 服务 |
| 服务器数据源不更新 | 上游接口/令牌错误，或地址不可达 | 对应数据源插件 `error` + 原因 | 核对上游开放接口与令牌、Hub 配置，见 [05-server-sources](05-server-sources.zh-CN.md) |
| Web 管理页登录失败 | 令牌错误 / 会话过期 | — | 用 `nuka init`/`nuka hub` 启动时打印的管理员令牌登录；丢失则用 `nuka token` 重建，见 [02-install-hub](02-install-hub-windows.zh-CN.md) |
| 公网访问 404 / 403 | 敏感接口按区域隔离 / 缺少 Access JWT | — | 确认 `/hook/*`、`/api/v1/perm/*` 在 public 区域应返回 404；补 Cloudflare Access 登录，见 [09-public-access](09-public-access.zh-CN.md) |

## 获取更多信息

- 各集成的详细排查见对应引导的「常见问题」一节。
- 设备侧日志通过 USB CDC 的 `diag.info` 获取；设备错误页与 CDC `info` 优先于 doctor（doctor 不检查设备 Flash ABI）。
- 系统联调中的已知限制与复现记录见 [系统联调报告](../bench/e2e-report.md)。

## 卸载与回滚

排查无果需要恢复时：Hub 用 `nuka service uninstall` + `uv tool uninstall nuka-hub` 卸载；设备写回 `factory-16m.bin` 恢复原厂（[01-flash-firmware](01-flash-firmware.zh-CN.md)）。这些操作都不擦除 eFuse、不开 Secure Boot 或 Flash 加密。
