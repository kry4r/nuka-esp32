# 天气

Windows、macOS、Linux 上已安装的 nuka-hub（Python 3.11+）可读取 [Open-Meteo](https://open-meteo.com/en/docs)，无需 API 密钥。明确指定坐标或城市，使用坐标可以省去地理编码请求。机器需要出站 HTTPS。开发只用假天气 fixture，不访问真实天气 API。

先备份 `config.toml`，三平台均使用以下片段：

```toml
[weather]
enabled = true
provider = "open_meteo"
lat = 0.0
lon = 0.0
units = "metric"
interval = "15m"
```

`0.0, 0.0` 只是文档示例，请在私有配置中填写自己的位置。也可以省略两个坐标，改用 `city`，每次进程启动后仅地理编码一次。协议中的温度始终为摄氏度；`imperial` 影响风速格式。

Windows PowerShell：

```powershell
nuka doctor
nuka hub
```

macOS：

```sh
nuka doctor
nuka hub
```

Linux：

```sh
nuka doctor
nuka hub
```

doctor 应输出 `{"name":"weather","kind":"weather","health":"ok","detail":"","doc":"07-weather"}`。首次采样后，从本机 Web 管理员会话查看 `GET /api/v1/state`，应有 `weather.now`、未来三天、UTC 的 `updated` 时间戳。天气卡应显示位置和昼夜图标。Hub 运行时 doctor 只读 `/api/v1/plugins` 的采样健康；未运行时只初步探测，不访问上游。

| 问题 / doctor 结果 | 排查 |
|---|---|
| 单独探测禁用插件时 `weather disabled`，`Weather is not configured` | 设置 `enabled = true` 并明确填写坐标或城市。正常 doctor 不列出已禁用天气。 |
| 配置错误 `weather requires explicit lat/lon or city` | 同时提供两个坐标，或提供非空城市名，再运行 doctor。 |
| `hub_http degraded`，`Hub not reachable; start nuka hub and check hub.port` | 在配置的端口启动 Hub。 |
| 初步 `weather ok`，随后插件为 `error` | 检查出站 HTTPS、城市拼写和响应形状；缺失数据不会用虚构天气替代。 |

回滚时设 `enabled = false` 或恢复备份，重启 Hub。没有需要删除的上游账号、密钥或服务。确认配置的插件列表不再包含天气，卡片呈现未配置状态。
