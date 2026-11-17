# Ubuntu 服务器、Pi 与永久记录

适用于 Ubuntu 24.04 或更新版本，需要 SSH / sudo、Python≥3.11、Node≥22.19，以及域名和 Cloudflare 管理权限。本机仍需运行 Hub 给 USB 设备配网；远端服务器无法枚举电脑 USB。服务器使用独立 `nuka` 服务账号，业务端口仅监听回环，数据保存在 `/var/lib/nuka`。

## 构建与上传

Windows PowerShell、macOS 和 Linux 在项目根目录使用相同命令；先把 SSH 别名和目标目录替换为自己的值：

```sh
uv sync --project hub --locked
uv build --project hub --wheel --out-dir build/server
uv export --project hub --locked --no-dev --no-emit-project --output-file build/server/requirements.txt
scp build/server/*.whl build/server/requirements.txt hub/deploy/run-hub.sh hub/deploy/nuka-hub.service hub/deploy/config.example.toml <SSH_HOST>:<UPLOAD_DIR>/
ssh <SSH_HOST>
```

应看到 wheel、requirements 和三个部署文件已上传。requirements 来自 uv.lock，不读取或上传本机 config.toml、钥匙串、local、备份和录音。Pi 的脚本、package.json / lock 随 wheel 分发，node_modules 不进入 wheel。

## 服务与钥匙串

以下命令在服务器上执行。新建服务账号和目录时先检查同名账号 / 路径没有其他用途；已有部署保留数据与钥匙串，不重新生成解锁凭据。

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

从 [Node 官方下载页](https://nodejs.org/en/download)选择服务器架构的 LTS 包，核验该版本的 SHA-256 后安装到 `/opt/nuka/node`。不要使用本机 Windows 的 Node 可执行文件。以下命令应显示≥22.19的版本，并在 wheel 安装位置装入锁定的 Pi 依赖：

```sh
/opt/nuka/node/bin/node --version
PI_DIR=$(/opt/nuka/venv/bin/python -c 'from nuka_hub.assistant.pi_engine import ENGINE; print(ENGINE.parent)')
sudo env PATH=/opt/nuka/node/bin:/usr/bin:/bin /opt/nuka/node/bin/npm --prefix "$PI_DIR" ci --ignore-scripts --no-audit --no-fund --registry=https://registry.npmjs.org
sudo install -m 755 <UPLOAD_DIR>/run-hub.sh /opt/nuka/run-hub.sh
sudo install -m 644 <UPLOAD_DIR>/nuka-hub.service /etc/systemd/system/nuka-hub.service
sudo install -m 600 -o nuka -g nuka <UPLOAD_DIR>/config.example.toml /etc/nuka/config.toml
```

服务使用私有 D-Bus 和 GNOME Secret Service 保存模型密钥。systemd 加密凭据只保存随机的钥匙串解锁口令；模型密钥仍在系统钥匙串中。首次部署生成一次：

```sh
sudo install -d -m 700 /etc/credstore.encrypted
python3 -c 'import secrets,sys; sys.stdout.write(secrets.token_urlsafe(32))' | sudo systemd-creds encrypt --with-key=host --name=keyring-password - /etc/credstore.encrypted/nuka-keyring-password
sudo systemctl daemon-reload
sudo systemctl enable --now nuka-hub
sudo systemctl is-active nuka-hub
curl --fail http://127.0.0.1:8790/api/v1/health
```

应显示 `active` 及包含 `ok:true`、版本和 proto 的健康响应；`ss -ltn` 中该端口必须只有回环监听。模型密钥在后台设置时不回显。钥匙串解锁凭据和数据目录必须一起备份；更换服务器时需要重新封装该主机绑定凭据。

## 认证与配置

先在服务器创建 admin 令牌（明文只显示一次），通过 SSH 转发完成首次本地登录：

```sh
sudo -u nuka /opt/nuka/venv/bin/nuka token create --scope admin --name server-admin --config /etc/nuka/config.toml
```

Windows PowerShell、macOS、Linux 均可在本机运行：

```sh
ssh -N -L 18790:127.0.0.1:8790 <SSH_HOST>
```

浏览器打开本机转发端口并用 admin 令牌登录。按 [公网访问引导](09-public-access.zh-CN.md)添加独立隧道路由和 Self-hosted Access 应用；保留已有入口和末尾404规则。Access 配置完成后重启服务，才能开启公网配置：

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

按实际模型修改容量。最近20轮控制上下文，完整记录没有自动过期。关闭记录停止新写入，删除会话还会删除由该会话消息产生的记忆；文件另行删除。上传默认限额64MiB，只以附件方式下载，不在服务器执行。Vault 不可上传到 Hub。

验证时分别检查：未登录请求被拒绝；Access 已通过但没有 Hub 会话仍不能操作；写请求缺 CSRF 被拒绝；公网 hook / Web 审批仍404。`nuka doctor` 的 `hub_http` 只检查本地服务，不能替代这些云端检查。真实模型联调单独手工执行；自动化测试只用假数据。

## 备份、回滚与卸载

数据库备份必须通过 SQLite 在线 backup API 创建一致快照，并一起保存附件目录和服务器钥匙串；不能直接复制写入中的 sqlite 文件。恢复前停止 nuka 服务，核验快照完整性与附件清单。Pi / Hub 升级失败时停止服务、装回上一 wheel 和对应锁定依赖，保留数据和钥匙串；不要重建 admin 令牌或解锁凭据。

卸载先撤销专用 Access / DNS 路由，再执行 `sudo systemctl disable --now nuka-hub`，移除 nuka 专用 unit 并 daemon-reload。保留数据和钥匙串供恢复；不删除其他隧道入口、系统账号或服务。

## 常见错误

| 现象 / doctor 线索 | 排查 |
|---|---|
| `hub_http: degraded` / `Hub not reachable; start nuka hub and check hub.port` | 检查 `systemctl status nuka-hub`、监听端口与 nuka 日志，先恢复回环服务 |
| `port: error` / `Port occupied by another process` | 为 nuka 选择空闲回环端口，同时修改隧道 service；不要停掉不属于 nuka 的服务 |
| 助手503，doctor 的 hub_http 正常 | 检查 Node版本、wheel安装位置的 npm ci、助手配置；不要把 node_modules 放进源码提交 |
| 密钥保存失败，doctor 的 hub_http 正常 | 检查私有 D-Bus、GNOME钥匙串是否解锁、systemd加密凭据是否来自本机；不改成明文密钥配置 |
| 公网配置404或403，doctor 的 hub_http 正常 | 检查 remote_admin、Access应用域名、AUD、身份策略及admin会话；不把CF流量伪装成lan |
| 服务器看不到USB，doctor 的 usb 为 disabled | 正常；回到插着设备的本机Hub配网，Wi-Fi密码仍只走USB |
