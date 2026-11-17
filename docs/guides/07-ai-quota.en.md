# AI quota providers

CPA ([CLIProxyAPI](https://github.com/router-for-me/CLIProxyAPI)) proxies AI accounts. CPAMP ([CPA Manager Plus](https://github.com/seakee/CPA-Manager-Plus/tree/v1.14.7)) adds a Manager Server with quota/header snapshots and aggregated usage. nuka-hub reads either provider; it never sends an AI management key to a device or Web page. Use Python 3.11+ on Windows, macOS, or Linux, an installed Hub, and permission to configure the chosen provider.

Development and tests must use fixtures or a local fake server. Do not use a production provider to test authentication failure. CPA bans an IP for 30 minutes after five authentication failures. nuka-hub stops every request immediately on the first 401/403, including a wrapped api-call response. Correct the configuration/key before using Web **Retry**, which sends one L1 request.

## Prepare the upstream

For direct CPA, configure its management section locally. Enable remote management only when Hub is on another machine and restrict network access:

```yaml
remote-management:
  allow-remote: true
  secret-key: "<YOUR_TOKEN>"
```

Restart CPA using its normal service manager. Verify its local management page works once using the intended key; do not run repeated key-guessing commands. The required read endpoint is `/v0/management/auth-files`. v1 deliberately uses that prefix. For CPAMP, obtain the Manager Server's administrator key from its deployment configuration and management UI; its key is distinct from the CPA key it stores. Follow the [upstream deployment instructions](https://github.com/seakee/CPA-Manager-Plus/tree/v1.14.7).

## Store the secret on each platform

Windows PowerShell:

```powershell
nuka secret set ai.example
nuka doctor
```

macOS:

```sh
nuka secret set ai.example
nuka doctor
```

Linux:

```sh
nuka secret set ai.example
nuka doctor
```

The hidden prompt must not echo the secret. The key is stored under service `nuka-hub`; do not paste it into config, source, commands, or logs. On a machine without a keyring use `nuka secret set ai.example --file`; its `secrets.toml` stays outside git and needs owner-only permissions. A service may instead receive `NUKA_SECRET_AI_EXAMPLE` from its secret environment.

## Configure and verify

Back up the config, then add:

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

For direct CPA change `type = "cpa"` and `base_url = "https://cpa.example.com"`; it has L1/L3 only. Restart `nuka hub`, then inspect the local Web management page or administrator-only `GET /api/v1/plugins` and `GET /api/v1/ai`. With the Hub running, `nuka doctor` reads its plugin-health snapshot without sending extra upstream requests. With the Hub stopped it checks availability and secret presence only; that preliminary `ok` does not prove an upstream request succeeded. Example doctor row:

```json
{"name":"example","kind":"ai","health":"ok","detail":"","doc":"07-ai-quota"}
```

Expect masked account labels such as `a***@example.com`, windows with `used_pct`, `observed_at`, and `source`, and provider `health: ok`. Empty windows mean no usable upstream measurement, not zero usage. L1 polls status/signals; CPAMP L2 reads snapshots and usage. A complete daily request/token/failure summary is emitted only when the retained detail records establish that day's failure count. An unchanged undated L1 signal retains its first-observed time, so it eventually becomes stale.

L3 runs only when all account windows are stale or the user refreshes, and respects the per-account minimum even on manual refresh. The shipped recipe permits exactly three upstream URLs: Codex `wham/usage`, Claude `api/oauth/usage`, and Antigravity `retrieveUserQuotaSummary`. The Manager replaces `$TOKEN$` server-side. Raw bodies are parsed transiently and never stored. No write management API or usage queue is allowed.

## Troubleshooting and rollback

| `nuka doctor` / running provider state | Action |
|---|---|
| `example disabled`, `AI management secret is missing` | Store `ai.example` under the same account/environment as the Hub service, then restart. |
| `hub_http degraded`, `Hub not reachable; start nuka hub and check hub.port` | Start the Hub; doctor alone does not poll quota data. |
| Preliminary `example ok`, running provider `auth_failed` | Fix key type, upstream remote-management permission, and URL; use one Web Retry after fixing them. Never repeatedly retry a rejected key. |
| Preliminary `example ok`, running provider `backoff` | Check upstream rate limit/availability; 429, 5xx, or timeout backs off from 1 second up to 30 minutes. |

Restore the previous `[ai]` config/remove the provider and restart Hub. Remove the unused key with `nuka secret rm ai.example`, remove any service secret environment entry, and revoke the dedicated key upstream. Restore the previous CPA management exposure if enabled only for this integration. Verify the provider is no longer listed and other accounts remain unaffected.
