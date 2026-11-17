# 服务器推送通知

适用于已安装 Python 3.11+、nuka-hub 的 Windows、macOS、Linux，以及能通过私网访问 Hub 的发送端。运行 `nuka token create --scope notify --name example-push` 创建 notify 令牌；令牌只显示一次，保存在仓库之外。以下命令用于已运行的 Hub，地址均为文档占位符。

所有 `/hook/*` 在 public 区域都返回 404，包括 Cloudflare 转发到回环的请求；LAN 还需显式开启下面的选项。公网通用通知使用 `/api/v1/notify`，不带 `server` 字段。

```toml
[security]
allow_lan_hooks = true

[notify]
dedupe_window = "5m"
hook_rate_per_min = 30
hook_burst = 10
keep = 500
```

重启后运行 `nuka doctor`，应看到 `hub_http` 为 `ok`。只有本机发送时保持 `allow_lan_hooks = false`。`server` 按服务器名称或别名匹配，匹配后更新告警角标。

## Windows PowerShell

```powershell
$notifyToken = Read-Host 'Notify token'
$notifyUrl = 'http://192.0.2.10:8790/hook/server'
$headers = @{Authorization = "Bearer $notifyToken"}
$payload = @{title='Example task failed'; level='warn'; server='example-web'; source='script'; dedupe_key='example-task'} | ConvertTo-Json
Invoke-RestMethod -Method Post -Uri $notifyUrl -Headers $headers -ContentType application/json -Body $payload
Invoke-RestMethod -Method Post -Uri $notifyUrl -Headers $headers -ContentType text/plain -Body "Example notification`nExample details"
Invoke-RestMethod -Method Get -Uri "$($notifyUrl)?title=Example&level=info" -Headers $headers
```

三种形式均应返回 HTTP 202 和 `{"nid":"n_…"}`，随后在设备或本机 Web 通知列表确认。同一 dedupe key 在五分钟内重复发送不应新增可见通知。

## macOS bash

```sh
read -r -s -p 'Notify token: ' NUKA_NOTIFY_TOKEN; printf '\n'
export NUKA_NOTIFY_TOKEN
export NUKA_NOTIFY_URL='http://192.0.2.10:8790/hook/server'
curl --fail -H "Authorization: Bearer $NUKA_NOTIFY_TOKEN" -H 'Content-Type: application/json' --data '{"title":"Example task failed","level":"warn","server":"example-web","source":"script","dedupe_key":"example-task"}' "$NUKA_NOTIFY_URL"
curl --fail -H "Authorization: Bearer $NUKA_NOTIFY_TOKEN" -H 'Content-Type: text/plain' --data-binary $'Example notification\nExample details' "$NUKA_NOTIFY_URL"
curl --fail -G -H "Authorization: Bearer $NUKA_NOTIFY_TOKEN" --data-urlencode 'title=Example' --data-urlencode 'level=info' "$NUKA_NOTIFY_URL"
```

上述输入语法使用 bash；当前为 zsh 时先运行 `bash`。三种请求都应有 `nid`，再确认通知列表。

## Linux bash 与定时任务

Linux 可直接使用上面的 macOS bash 命令。cron 可在仓库之外保存以下可执行脚本，通过受保护的环境提供令牌：

```sh
#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
curl --fail -H "Authorization: Bearer $NUKA_NOTIFY_TOKEN" -H 'Content-Type: application/json' --data '{"title":"Example scheduled check","source":"cron","dedupe_key":"example-cron"}' "$NUKA_NOTIFY_URL"
```

crontab 示例为 `0 * * * * /opt/nuka-notify/send.sh`。`NUKA_NOTIFY_TOKEN`、`NUKA_NOTIFY_URL` 从仓库之外、仅拥有者可读的配置提供，不把令牌提交到 crontab。执行一次后应看到 `source: cron` 的一条通知。

systemd 在被监控服务的 `[Unit]` 中加入 `OnFailure=nuka-notify@%n.service`，另建模板：

```ini
# SPDX-License-Identifier: AGPL-3.0-or-later
[Unit]
Description=Send nuka failure notification
[Service]
Type=oneshot
EnvironmentFile=/etc/nuka-notify.env
ExecStart=/opt/nuka-notify/send.sh
```

环境文件设为 600 权限，运行 `sudo systemctl daemon-reload`，只用临时服务验证失败路径。应收到一条通知；用 `journalctl -u nuka-notify@EXAMPLE.service` 检查日志，不打印令牌。

## GitHub Actions

互联网 runner 使用公网通用入口 `https://hub.example.com/api/v1/notify`。把 URL 和 notify 令牌分别放入仓库 secrets `NUKA_NOTIFY_URL`、`NUKA_NOTIFY_TOKEN`；如果启用了 Cloudflare Access，还需独立提供其凭据。私网内的 self-hosted runner 才可使用 `/hook/server`。

```yaml
# SPDX-License-Identifier: AGPL-3.0-or-later
- name: Notify
  env:
    NUKA_NOTIFY_URL: ${{ secrets.NUKA_NOTIFY_URL }}
    NUKA_NOTIFY_TOKEN: ${{ secrets.NUKA_NOTIFY_TOKEN }}
  run: >-
    curl --fail -H "Authorization: Bearer $NUKA_NOTIFY_TOKEN"
    -H 'Content-Type: application/json'
    --data '{"title":"Example job finished","source":"automation"}'
    "$NUKA_NOTIFY_URL"
```

应返回 202 并收到通用通知。只接受 URL 的工具可用 `?token=`，但访问日志会记录令牌，优先用 Authorization 头。

## 常见错误与回滚

| 诊断 / 响应 | 排查 |
|---|---|
| `nuka doctor`：`hub_http degraded`，`Hub not reachable; start nuka hub and check hub.port` | 启动 Hub 并核对端口。 |
| `nuka doctor`：`port error`，`Port occupied by another process` | 结束占用进程或修改 `hub.port`。 |
| `nuka doctor`：`hub_http ok`，hook 却返回 404 | 检查 public/CF 头、`allow_lan_hooks`；可达不等于有 hook 权限。 |
| `hub_http ok`，HTTP 401/403 或 429 | 使用 notify 作用域令牌；429 遵守 `Retry-After` 并降低发送频率。 |

doctor 健康条目示例：`{"name":"hub_http","kind":"system","health":"ok","detail":"Hub health reachable","doc":"guides/12-troubleshooting"}`。回滚时删除新增的 cron、OnFailure、workflow 条目，用 `nuka token revoke <TOKEN_ID>` 撤销令牌，恢复配置并重启。只删除本篇创建的脚本、模板和环境文件，最后确认已撤销令牌不能再发送。
