# Install nuka-hub with Docker

Use Docker Desktop on Windows / macOS, or Docker Engine with Compose v2 on Linux. Images target amd64 and arm64. You need permission to use the Docker engine. Container hardware collection describes the container; run an Agent on the host for desktop media and host sensors.

Host Python is unnecessary for the container. To use a native Agent or USB CLI, install uv for your platform and then the native package:

```powershell
# Windows PowerShell
winget install --id astral-sh.uv -e
```

```sh
# macOS
brew install uv
# Linux
curl -LsSf https://astral.sh/uv/install.sh | sh
uv tool install --python 3.12 'nuka-hub[all]==0.1.0'
```

Check `docker version` and `docker compose version`. Copy the repository's `hub/docker-compose.example.yml` to a deployment directory as `compose.yml`, using `Copy-Item` on Windows or `cp` on macOS / Linux. Run subsequent commands there. Replace `<owner>` in the image name with the release organization. Before the first release, run `docker build -t nuka-hub:local hub` at the repository root and change the compose image to `nuka-hub:local`.

```sh
docker compose run --rm -it nuka-hub init
```

Initialization creates `config/config.toml` and data and displays an administrator token once. Save that token. Start with this configuration, replacing the example address and leaving real upstream sources disabled:

```toml
[hub]
name = "nuka-hub"
host = "0.0.0.0"
port = 8790
lang = "zh-CN"
public_url = "http://192.0.2.10:8790"

[collectors]
enabled = []

[weather]
enabled = false
```

Compose mounts `./config:/config` and `./data:/data`, setting `NUKA_CONFIG=/config/config.toml` and `NUKA__HUB__DATA_DIR='"/data"'`. `NUKA__` values are TOML literals: preserve the double quotes around the path.

```sh
docker compose up -d
docker compose exec nuka-hub nuka --version
docker compose exec nuka-hub nuka doctor
curl --fail http://localhost:8790/api/v1/health
```

Expect version `0.1.0`, `ok:true` and `proto:1`. Open `http://localhost:8790` and sign in. Restart the container and verify that login and device registration persist. Open host TCP 8790 to the LAN only. Multicast discovery may not cross the container network; enter the host's explicit LAN URL during pairing.

Use device SoftAP plus a Web pairing code, following [pairing](03-pair-device.en.md). Desktop USB is not directly available inside the container; prefer a native Hub for USB pairing. Run OTA publishing inside this container so it shares the token database and configuration: `docker compose exec nuka-hub nuka ota publish /data/nuka-esp32-0.1.0.bin`. Place the firmware in the host data directory first.

Without a system keyring, explicitly use file secrets:

```sh
docker compose exec -it nuka-hub nuka secret set ai.example --file
docker compose exec nuka-hub chmod 600 /config/secrets.toml
```

Enter values interactively and reference names in configuration. Do not configure real AI services during development or commit the deployment directory.

Uninstall / rollback: `docker compose down` removes the container while preserving the two host directories. Back up config and data before upgrading. To roll back, set image to a previous released version and run `docker compose up -d`. Keep data volumes intact.

| Symptom / doctor output | Resolution |
|---|---|
| `hub_http: degraded` | Inspect `docker compose logs nuka-hub`, mounts, TOML and bind address |
| `port: error`, `Port occupied by another process` | Resolve the host port conflict; update mapping and public_url |
| `mdns: degraded`, `Hub discovery failed; check UDP multicast/firewall or use the paired URL` | Use the host's LAN URL, not a container address |
| `firewall: degraded` | Check the host firewall and health from a second LAN machine |
| `usb: disabled` | Expected without USB passthrough; use SoftAP / Web pairing |

Multi-platform images follow [Docker's documentation](https://docs.docker.com/build/building/multi-platform/).
