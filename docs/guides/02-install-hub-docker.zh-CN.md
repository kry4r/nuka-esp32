# Docker 安装 nuka-hub

适用于 Windows / macOS 的 Docker Desktop，以及 Linux Docker Engine + Compose v2；镜像提供 amd64 和 arm64。运行 Docker 需要相应系统权限。容器中的硬件采集只代表容器，桌面媒体和宿主机传感器请通过宿主机 Agent 提供。

容器不需要宿主机 Python。需要本机 Agent 或 USB CLI 时，按平台安装 uv，再安装原生包：

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

检查 `docker version`、`docker compose version` 均能连接引擎。从仓库复制 `hub/docker-compose.example.yml` 到部署目录并命名为 `compose.yml`；Windows 用 `Copy-Item`，macOS / Linux 用 `cp`。所有后续命令在部署目录执行。将镜像中的 `<owner>` 替换为实际发布组织；尚未发布时，先在仓库根目录运行 `docker build -t nuka-hub:local hub`，再把 compose 的 image 改为 `nuka-hub:local`。

```sh
docker compose run --rm -it nuka-hub init
```

预期显示版本及一次性管理员令牌，并生成 `config/config.toml` 和数据。配置如下，首次验证禁用真实上游；示例地址需要替换：

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

Compose 挂载 `./config:/config` 和 `./data:/data`，环境变量 `NUKA_CONFIG=/config/config.toml`、`NUKA__HUB__DATA_DIR='"/data"'`。`NUKA__` 配置值是 TOML 字面量，路径中的双引号必须保留。

```sh
docker compose up -d
docker compose exec nuka-hub nuka --version
docker compose exec nuka-hub nuka doctor
curl --fail http://localhost:8790/api/v1/health
```

预期版本 `0.1.0`、健康响应 `ok:true` / `proto:1`。浏览器打开 `http://localhost:8790` 并登录，重启容器后仍能登录且设备注册记录保留。仅向局域网开放宿主机 TCP 8790；mDNS 跨容器网络不保证可达，配对时填写明确的局域网地址。

配对使用设备 SoftAP 和 Web 配对码，见[配对引导](03-pair-device.zh-CN.md)。容器不能直接使用桌面 USB；USB 配对优先使用原生 Hub。OTA CLI 在同一容器中执行，以共享令牌数据库和配置，例如 `docker compose exec nuka-hub nuka ota publish /data/nuka-esp32-0.1.0.bin`，固件文件由你事先放入宿主机 data 目录。

容器无系统钥匙串时，显式写入文件密钥：

```sh
docker compose exec -it nuka-hub nuka secret set ai.example --file
docker compose exec nuka-hub chmod 600 /config/secrets.toml
```

交互输入值，配置中仅引用名称；测试期间不配置真实 AI 服务。不要把配置目录提交到 git。

卸载 / 回滚：`docker compose down` 停止并删除容器，保留两个宿主机目录。升级前备份 config 和 data；回滚时将 image 改为之前已发布的版本，再运行 `docker compose up -d`。不要加删除数据卷的参数。

| 现象 / doctor 输出 | 排查 |
|---|---|
| `hub_http: degraded` | 查看 `docker compose logs nuka-hub`，检查挂载、TOML 和监听地址 |
| `port: error`，`Port occupied by another process` | 检查宿主机端口冲突；修改映射和 public_url |
| `mdns: degraded`，`Hub discovery failed; check UDP multicast/firewall or use the paired URL` | 填写宿主机明确的局域网地址，不使用容器私网地址 |
| `firewall: degraded` | 在宿主机检查防火墙，并从第二台局域网机器检查健康接口 |
| `usb: disabled` | 容器未提供 USB 是预期状态；使用 SoftAP / Web 配对 |

多平台镜像依据 [Docker 官方文档](https://docs.docker.com/build/building/multi-platform/)。
