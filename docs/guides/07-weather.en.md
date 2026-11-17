# Weather

On Windows, macOS, or Linux, an installed nuka-hub (Python 3.11+) can read [Open-Meteo](https://open-meteo.com/en/docs) without an API key. Choose explicit coordinates or a city; coordinates avoid an extra geocoding request. The machine needs outbound HTTPS. Development uses the synthetic weather fixture and must not contact the real weather API.

Back up `config.toml`, then add the same snippet on all three platforms:

```toml
[weather]
enabled = true
provider = "open_meteo"
lat = 0.0
lon = 0.0
units = "metric"
interval = "15m"
```

`0.0, 0.0` is a documentation example; enter your own location in your private config. Alternatively omit both coordinates and set `city`; Hub resolves it once per process. Protocol temperature fields always contain Celsius; `imperial` changes the formatted wind-speed unit.

Windows PowerShell:

```powershell
nuka doctor
nuka hub
```

macOS:

```sh
nuka doctor
nuka hub
```

Linux:

```sh
nuka doctor
nuka hub
```

Doctor should emit `{"name":"weather","kind":"weather","health":"ok","detail":"","doc":"07-weather"}`. After the first scheduled sample, use the local Web administrator session to inspect `GET /api/v1/state`: expect `weather.now`, three forecast days, and an UTC `updated` timestamp. The Weather card should show the configured location and day/night icon. While Hub runs, Doctor reads `/api/v1/plugins` for sample health. Its offline availability probe does not fetch the upstream API.

| Problem / doctor result | Action |
|---|---|
| `weather disabled`, `Weather is not configured` when probing a disabled plugin | Set `enabled = true` and explicit coordinates/city. Disabled weather is normally omitted from doctor. |
| Config error `weather requires explicit lat/lon or city` | Provide both coordinates or a nonempty city, then run doctor again. |
| `hub_http degraded`, `Hub not reachable; start nuka hub and check hub.port` | Start Hub at the configured port. |
| Preliminary `weather ok`, plugin later `error` | Check outbound HTTPS, location spelling, and upstream response shape; missing data is not replaced with invented weather. |

To roll back set `enabled = false` or restore the backup and restart Hub. No upstream account, key, or service needs removal. Confirm weather disappears from the configured plugin list and the card shows its unconfigured state.
