# Server push notifications

Use an installed nuka-hub (Python 3.11+, Windows/macOS/Linux) and a private route from the sending script. Create a notify token with `nuka token create --scope notify --name example-push`; save the returned token once, outside the repository. The commands below apply to a running Hub. Example addresses are documentation placeholders.

All `/hook/*` routes reject public requests with 404, including requests forwarded through Cloudflare to loopback. LAN callers additionally require the setting below. Public generic notifications use `/api/v1/notify`, without a `server` field.

```toml
[security]
allow_lan_hooks = true

[notify]
dedupe_window = "5m"
hook_rate_per_min = 30
hook_burst = 10
keep = 500
```

Restart Hub and run `nuka doctor`; expect `hub_http` health `ok`. Keep `allow_lan_hooks = false` when all senders are local. A `server` value matches a configured server name/alias and updates its alert badge.

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

Each request should return HTTP 202 and `{"nid":"n_…"}`. Verify the notification in the device or local Web notification list. Repeating the same dedupe key within five minutes should not create another visible notification.

## macOS bash

```sh
read -r -s -p 'Notify token: ' NUKA_NOTIFY_TOKEN; printf '\n'
export NUKA_NOTIFY_TOKEN
export NUKA_NOTIFY_URL='http://192.0.2.10:8790/hook/server'
curl --fail -H "Authorization: Bearer $NUKA_NOTIFY_TOKEN" -H 'Content-Type: application/json' --data '{"title":"Example task failed","level":"warn","server":"example-web","source":"script","dedupe_key":"example-task"}' "$NUKA_NOTIFY_URL"
curl --fail -H "Authorization: Bearer $NUKA_NOTIFY_TOKEN" -H 'Content-Type: text/plain' --data-binary $'Example notification\nExample details' "$NUKA_NOTIFY_URL"
curl --fail -G -H "Authorization: Bearer $NUKA_NOTIFY_TOKEN" --data-urlencode 'title=Example' --data-urlencode 'level=info' "$NUKA_NOTIFY_URL"
```

Use bash for the prompt syntax on macOS (`bash` first if the current shell is zsh). Expect a `nid` for all three forms, then confirm the UI list as above.

## Linux bash and scheduled jobs

The macOS bash commands run unchanged on Linux. For cron, save the following as an executable script outside the repository and pass the token in its protected environment:

```sh
#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
curl --fail -H "Authorization: Bearer $NUKA_NOTIFY_TOKEN" -H 'Content-Type: application/json' --data '{"title":"Example scheduled check","source":"cron","dedupe_key":"example-cron"}' "$NUKA_NOTIFY_URL"
```

Example crontab: `0 * * * * /opt/nuka-notify/send.sh`. Supply `NUKA_NOTIFY_TOKEN` and `NUKA_NOTIFY_URL` from an owner-readable configuration outside git, not a committed crontab. After one scheduled run, verify one new notification with `source: cron`.

For systemd, add `OnFailure=nuka-notify@%n.service` in the monitored unit's `[Unit]` section. Define the template separately:

```ini
# SPDX-License-Identifier: AGPL-3.0-or-later
[Unit]
Description=Send nuka failure notification
[Service]
Type=oneshot
EnvironmentFile=/etc/nuka-notify.env
ExecStart=/opt/nuka-notify/send.sh
```

Protect the environment file with mode 600, run `sudo systemctl daemon-reload`, and test only a disposable service. Expect one notification; inspect `journalctl -u nuka-notify@EXAMPLE.service` without printing tokens.

## GitHub Actions

For an Internet-hosted runner use the public generic endpoint, `https://hub.example.com/api/v1/notify`. Store that URL and a notify token as repository secrets `NUKA_NOTIFY_URL` and `NUKA_NOTIFY_TOKEN`; add required Cloudflare Access credentials separately if the service requires them. A self-hosted runner on the private network may instead use `/hook/server`.

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

Expect a 202 response and a generic notification. URLs with `?token=` are supported for URL-only senders but expose the token to access logs; prefer the Authorization header.

## Troubleshooting and rollback

| Diagnostic / response | Action |
|---|---|
| `nuka doctor`: `hub_http degraded`, `Hub not reachable; start nuka hub and check hub.port` | Start Hub and verify its configured port. |
| `nuka doctor`: `port error`, `Port occupied by another process` | Stop the conflicting process or change `hub.port`. |
| `nuka doctor`: `hub_http ok`, but hook returns 404 | Check public/CF headers and `allow_lan_hooks`; doctor reachability does not grant hook access. |
| `hub_http ok`, HTTP 401/403 or 429 | Use a notify-scoped token; for 429 obey `Retry-After` and reduce traffic. |

For exact doctor format, a healthy row is `{"name":"hub_http","kind":"system","health":"ok","detail":"Hub health reachable","doc":"guides/12-troubleshooting"}`. Remove cron/OnFailure/workflow entries, revoke the token with `nuka token revoke <TOKEN_ID>`, restore the previous config, and restart Hub. Delete only the script/template/environment file created here. Confirm the revoked token is rejected.
