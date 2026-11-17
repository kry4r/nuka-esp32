# Ubuntu server, Pi and permanent history

Use Ubuntu 24.04 or newer with SSH / sudo, Python≥3.11, Node≥22.19, and a domain managed by Cloudflare. Keep a local Hub for USB provisioning: the remote server cannot enumerate USB devices attached to your computer. The service uses a dedicated `nuka` account, loopback networking and `/var/lib/nuka` for data.

## Build and transfer

These commands work in Windows PowerShell, macOS and Linux from the repository root. Replace the SSH alias and upload directory:

```sh
uv sync --project hub --locked
uv build --project hub --wheel --out-dir build/server
uv export --project hub --locked --no-dev --no-emit-project --output-file build/server/requirements.txt
scp build/server/*.whl build/server/requirements.txt hub/deploy/run-hub.sh hub/deploy/nuka-hub.service hub/deploy/config.example.toml <SSH_HOST>:<UPLOAD_DIR>/
ssh <SSH_HOST>
```

Verify that the wheel, requirements and deployment files arrived. The wheel includes Pi scripts and npm lockfiles, and excludes node_modules. Do not transfer local configuration, credentials, private notes, Flash backups or recordings.

## Service and system keyring

Run on the server. Check that the account and paths have no other owner before first installation. Preserve existing data and the keyring when updating:

```sh
sudo apt-get install --no-install-recommends python3-venv gnome-keyring
sudo useradd --system --home-dir /var/lib/nuka --create-home --shell /usr/sbin/nologin nuka
sudo install -d -m 755 /opt/nuka
sudo install -d -m 700 -o nuka -g nuka /etc/nuka /var/lib/nuka
sudo python3 -m venv /opt/nuka/venv
sudo /opt/nuka/venv/bin/pip install uv==0.9.28
sudo /opt/nuka/venv/bin/uv pip sync --python /opt/nuka/venv/bin/python --require-hashes <UPLOAD_DIR>/requirements.txt
sudo /opt/nuka/venv/bin/uv pip install --python /opt/nuka/venv/bin/python --no-deps <UPLOAD_DIR>/<WHEEL_FILE>
```

Download an LTS archive for the server architecture from [Node's official site](https://nodejs.org/en/download), verify its release SHA-256, and install it under `/opt/nuka/node`. Verify the version and install Pi from the packaged lockfile:

```sh
/opt/nuka/node/bin/node --version
PI_DIR=$(/opt/nuka/venv/bin/python -c 'from nuka_hub.assistant.pi_engine import ENGINE; print(ENGINE.parent)')
sudo env PATH=/opt/nuka/node/bin:/usr/bin:/bin /opt/nuka/node/bin/npm --prefix "$PI_DIR" ci --ignore-scripts --no-audit --no-fund --registry=https://registry.npmjs.org
sudo install -m 755 <UPLOAD_DIR>/run-hub.sh /opt/nuka/run-hub.sh
sudo install -m 644 <UPLOAD_DIR>/nuka-hub.service /etc/systemd/system/nuka-hub.service
sudo install -m 600 -o nuka -g nuka <UPLOAD_DIR>/config.example.toml /etc/nuka/config.toml
```

The service runs GNOME Secret Service on a private D-Bus. Model secrets stay in that system keyring. An encrypted systemd credential unlocks the keyring; generate it only once on first installation:

```sh
sudo install -d -m 700 /etc/credstore.encrypted
python3 -c 'import secrets,sys; sys.stdout.write(secrets.token_urlsafe(32))' | sudo systemd-creds encrypt --with-key=host --name=keyring-password - /etc/credstore.encrypted/nuka-keyring-password
sudo systemctl daemon-reload
sudo systemctl enable --now nuka-hub
sudo systemctl is-active nuka-hub
curl --fail http://127.0.0.1:8790/api/v1/health
```

Expect `active` and health JSON containing `ok:true`, version and proto. Use `ss -ltn` to verify that the service port binds only to loopback. Back up the keyring and its unlock credential with application data. Host-encrypted credentials need re-encryption when migrating to another server.

## Authentication and configuration

Create an admin token on the server; its plaintext appears once:

```sh
sudo -u nuka /opt/nuka/venv/bin/nuka token create --scope admin --name server-admin --config /etc/nuka/config.toml
```

On Windows PowerShell, macOS or Linux, open a private forward for initial setup:

```sh
ssh -N -L 18790:127.0.0.1:8790 <SSH_HOST>
```

Open the forwarded local port in your browser and log in. Follow the [public access guide](09-public-access.en.md) to add a dedicated Tunnel route and Self-hosted Access application. Preserve other routes and the final404 rule. Once Access is configured, use your real values and restart the Hub:

```toml
[hub]
host = "127.0.0.1"
port = 8790
data_dir = "/var/lib/nuka"
public_url = "https://nuka.example.com"

[security]
remote_admin = true
web_approve_permissions = false
allow_lan_hooks = false

[security.cf_access]
enabled = true
team_domain = "access.example.com"
aud = "<ACCESS_AUD>"
require_for_ws = true

[assistant]
enabled = true

[assistant.llm]
base_url = "https://llm.example.com/v1"
model = "<MODEL>"
key_secret = "assistant.llm"
context_window = 32768

[assistant.history]
enabled = true
context_turns = 20
```

Set the context capacity for your model. Twenty turns limits model input; the full archive does not expire automatically. Disabling history stops new writes. Deleting a session also deletes memories sourced from its messages; attachments are deleted separately. Uploads default to64MiB and download as attachments without execution. Vault data never enters the Hub.

Verify that missing Hub authentication, missing CSRF on writes, and missing Access identity are rejected independently. Public hooks and Web approval must still return404. `nuka doctor` checks local `hub_http`; it does not verify Cloudflare policies. Real model checks are separate manual acceptance, while automated tests use fixtures.

## Backup, rollback and removal

Create consistent SQLite snapshots through the online backup API and include the attachment directory and keyring. Do not copy a live database file. Stop nuka before restoring and check database integrity and the attachment manifest. Roll back by stopping the service and reinstalling the previous wheel and its locked dependencies; preserve the database, tokens and unlock credential.

Remove only nuka's DNS / Access route, then run `sudo systemctl disable --now nuka-hub`. Remove the nuka unit and daemon-reload. Keep data and the keyring for recovery. Leave other tunnel routes, accounts and services intact.

## Troubleshooting

| Symptom / doctor output | Action |
|---|---|
| `hub_http: degraded` / `Hub not reachable; start nuka hub and check hub.port` | Check service status, listening port and nuka logs; recover the loopback service first |
| `port: error` / `Port occupied by another process` | Choose a free loopback port and update its tunnel route; leave unrelated services running |
| Assistant503 while hub_http is healthy | Check Node version, npm ci in the installed wheel directory and assistant settings |
| Saving a secret fails while hub_http is healthy | Check the private D-Bus, unlocked keyring and host-bound systemd credential; keep model keys out of plaintext configuration |
| Public configuration404 /403 while hub_http is healthy | Check remote_admin, Access hostname, audience, policy and Hub session; preserve public zone detection |
| USB is disabled on the server | Expected; provision through the local Hub connected to the device, including Wi-Fi password changes |
