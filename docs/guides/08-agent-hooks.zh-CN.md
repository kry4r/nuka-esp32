# AI CLI hooks

适用于已安装 Claude Code、Codex 或 Gemini CLI 的 Windows、macOS、Linux 用户。Hub 必须在本机或可信 LAN 中运行；不能通过公网隧道调用 hooks。v1 中 Claude 支持设备审批，Codex/Gemini 只同步状态。开发验证必须传 `--home` 临时目录，不能安装到维护者真实配置。

## 安装与验证

Windows PowerShell：

```powershell
nuka hooks install claude
nuka hooks install codex
nuka hooks install gemini
nuka doctor
```

macOS：

```sh
nuka hooks install claude
nuka hooks install codex
nuka hooks install gemini
nuka doctor
```

Linux：

```sh
nuka hooks install claude
nuka hooks install codex
nuka hooks install gemini
nuka doctor
```

仅执行已经安装的 CLI 对应行。安装器先备份原始字节，再合并 hooks；应看到 `Hooks installed; original file backed up.`。Codex 中打开 `/hooks` 审阅并授信。发起一个普通 CLI 任务，Web 状态页应出现会话；Claude 的 PermissionRequest 显示在设备，首次允许/拒绝生效。设备离线或 Hub 超时返回空决定，由终端继续审批，不默认允许。

配置片段：

```toml
[agents]
enabled_sources = ["claude", "codex", "gemini"]
permission_timeout = "60s"
show_project = "basename"
collect_last_message = true
[security]
allow_lan_hooks = false
web_approve_permissions = false
```

Claude 手动配置示例（其余非审批事件 timeout 使用 2 秒）：

```json
{"hooks":{"PermissionRequest":[{"hooks":[{"type":"http","url":"http://192.0.2.10:8790/hook/claude","timeout":90,"headers":{"Authorization":"Bearer $NUKA_HOOK_TOKEN"},"allowedEnvVars":["NUKA_HOOK_TOKEN"]}]}]}}
```

不要覆盖既有 settings.json。跨机器时在 Hub 开启 allow_lan_hooks，在 CLI 所在机器设置 agent.hub_url 指向同一可信 LAN Hub，并运行 `nuka token create --scope hook --name example`；通过进程环境 NUKA_HOOK_TOKEN 提供只显示一次的令牌。Claude HTTP hook 读取该变量；Codex/Gemini 的 nuka-hook 转发器从同一变量取值。令牌不放进命令行或 hook JSON。Cloudflare 头会使回环请求成为 public，因此隧道 hooks 永远返回 404。

Windows 默认使用 PowerShell，不需要经由 POSIX shell；Codex/Gemini 安装的命令是不含 shell 语法的 `nuka-hook <source> <event>`，在 PowerShell 中直接可用。设置令牌时使用 PowerShell 语法：

```powershell
$env:NUKA_HOOK_TOKEN = '<YOUR_TOKEN>'
```

macOS / Linux 使用 `export NUKA_HOOK_TOKEN='<YOUR_TOKEN>'`。

## 卸载与回滚

三个平台均执行：

```sh
nuka hooks uninstall claude
nuka hooks uninstall codex
nuka hooks uninstall gemini
```

应看到 `Original hook configuration restored.`；与安装前备份做字节 diff 应完全一致。卸载后若不再使用跨机器令牌，在管理页撤销。安装期间请不要另行编辑同一个 CLI 配置文件，卸载会恢复安装前的版本。

## 常见错误

| doctor 输出 / 现象 | 处理 |
|---|---|
| `hub_http: degraded`，`Hub not reachable; start nuka hub and check hub.port` | 启动正确配置的 Hub；转发器断连时安静退出，不阻塞 CLI |
| `port: error`，`Port occupied by another process` | 修改端口并重新安装 hooks，使 URL 同步 |
| `firewall: degraded`，`No inbound TCP allow rule found for the configured port` | 只给可信 LAN 放行端口；本机回环无需开启 LAN hooks |
| HTTP 404 / 401 | 404 检查 public 区域或未启用 LAN hooks；401 检查 hook 令牌作用域与环境变量。doctor 不执行真实 CLI 权限请求 |

Claude 事件/HTTP 语义见[官方 hooks 文档](https://code.claude.com/docs/en/hooks)。Codex/Gemini 的审批能力调研在 [bench 记录](../bench/codex-gemini-approval.md)，v1 不自行扩展契约。
