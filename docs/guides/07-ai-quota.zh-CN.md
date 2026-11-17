# AI 配额提供者

CPA（[CLIProxyAPI](https://github.com/router-for-me/CLIProxyAPI)）负责代理 AI 账号。CPAMP（[CPA Manager Plus](https://github.com/seakee/CPA-Manager-Plus/tree/v1.14.7)）另有 Manager Server，提供配额快照、响应头快照和聚合用量。nuka-hub 支持两者，管理密钥不会发送到设备或 Web。适用于 Windows、macOS、Linux，要求 Python 3.11+、已安装 Hub，以及配置上游服务的权限。

开发和测试只能使用 fixture 或本地假服务，不得用生产实例验证错误密钥。CPA 同一 IP 认证失败五次会被封禁 30 分钟。nuka-hub 第一次收到 401/403 就停止全部请求，包括 api-call 包装体里的失败。先修正配置或密钥，再点击 Web 的“重试”；重试仅发送一个 L1 请求。

## 准备上游

直连 CPA 时，在其本机配置管理接口；仅当 Hub 位于另一台机器时开启远程访问，并用网络策略限制来源：

```yaml
remote-management:
  allow-remote: true
  secret-key: "<YOUR_TOKEN>"
```

用原有服务管理器重启 CPA，只用确认正确的密钥在本机管理页验证一次，不循环尝试密钥。所需只读接口是 `/v0/management/auth-files`，v1 固定使用这个前缀。CPAMP 使用 Manager Server 自己的管理员密钥，从其部署配置和管理界面获取；该密钥与 CPAMP 持有的 CPA 管理密钥不同。部署步骤见[上游说明](https://github.com/seakee/CPA-Manager-Plus/tree/v1.14.7)。

## 分平台存储密钥

Windows PowerShell：

```powershell
nuka secret set ai.example
nuka doctor
```

macOS：

```sh
nuka secret set ai.example
nuka doctor
```

Linux：

```sh
nuka secret set ai.example
nuka doctor
```

隐藏提示不应回显密钥。钥匙串服务名为 `nuka-hub`；不要把值写入配置、源码、命令或日志。无钥匙串时可用 `nuka secret set ai.example --file`，其 `secrets.toml` 必须位于仓库之外，设为仅拥有者可读。服务也可以从受保护的环境获得 `NUKA_SECRET_AI_EXAMPLE`。

## 配置与验证

先备份配置，再加入：

```toml
[ai]
stale_after = "30m"
manual_refresh_per_min = 1
label_mode = "masked_email"

[[ai.provider]]
type = "cpamp"
name = "example"
base_url = "https://cpamp.example.com"
key_secret = "ai.example"
passive_interval = "60s"
snapshot_interval = "120s"
active_enabled = true
active_min_interval = "600s"
max_rpm = 20
```

直连 CPA 改为 `type = "cpa"`、`base_url = "https://cpa.example.com"`，只有 L1/L3。重启 `nuka hub`，从本机 Web 管理页或管理员接口 `GET /api/v1/plugins`、`GET /api/v1/ai` 检查运行状态。`nuka doctor` 在 Hub 运行时只读当前插件健康快照，不另发上游请求；Hub 未运行时只做可用性、密钥存在性探测，此时 `ok` 不代表上游请求已经成功。初步探测输出示例：

```json
{"name":"example","kind":"ai","health":"ok","detail":"","doc":"07-ai-quota"}
```

应看到 `a***@example.com` 等脱敏标签，以及含 `used_pct`、`observed_at`、`source` 的窗口，provider 状态为 `ok`。窗口为空表示上游没有可用读数，不表示配额使用为零。L1 轮询状态与 signals；CPAMP 的 L2 读取快照和用量。只有保留的明细能证明当天完整失败数时，才发送完整的当日请求/token/失败汇总。没有上游时间戳的 L1 读数在数值不变时保留首次观察时间，最终会标记 stale。

L3 只在该账号所有窗口过期或用户刷新时运行；手动刷新也遵守单账号最小间隔。配方只允许三个上游 URL：Codex `wham/usage`、Claude `api/oauth/usage`、Antigravity `retrieveUserQuotaSummary`。`$TOKEN$` 由上游 Manager 替换，原始 body 仅瞬时解析，不存储。写管理接口与 usage queue 都不允许访问。

## 常见问题与回滚

| `nuka doctor` / 运行中 provider 状态 | 排查 |
|---|---|
| `example disabled`，`AI management secret is missing` | 在 Hub 服务相同账号/环境中存储 `ai.example`，再重启。 |
| `hub_http degraded`，`Hub not reachable; start nuka hub and check hub.port` | 启动 Hub；doctor 本身不轮询配额。 |
| 初步探测 `example ok`，运行中 `auth_failed` | 检查密钥类别、上游远程管理权限和 URL；修正后只点击一次重试。 |
| 初步探测 `example ok`，运行中 `backoff` | 检查上游限流与可用性；429、5xx、超时按 1 秒至 30 分钟退避。 |

恢复原来的 `[ai]` 配置或移除 provider，重启 Hub。用 `nuka secret rm ai.example` 删除不用的密钥，移除服务的对应环境变量，并在上游撤销专用密钥。如果只为本集成开启 CPA 远程管理，则恢复原来的开放范围。确认 provider 不再列出，其他账号不受影响。
