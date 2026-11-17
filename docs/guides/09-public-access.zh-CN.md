# Tailscale 与公网访问

适用于 Windows、macOS、Linux 的 Hub。先完成本地配对，并保留本地管理员入口；公网用于查看管理页、设备/Agent WS 和通用通知。所有 /hook/*、Web 审批和配对永远不对 public 开放。配置编辑和密钥设置默认禁止公网；维护者批准的D30服务器模式可显式开启远程管理，必须同时通过Access身份与Hub admin会话 / CSRF，见[服务器引导](15-server-deployment.zh-CN.md)。示例域名与令牌必须替换。

## Tailscale

按 [Tailscale 官方安装说明](https://tailscale.com/download)安装并登录。Windows PowerShell、macOS、Linux 均可执行：

```sh
tailscale status
nuka doctor
```

应看到自己的节点已连接。仅把 Hub TCP 监听端口开放给需要的节点；不要把管理端口无鉴权直接暴露互联网。设备不原生运行 Tailscale：由可达的局域网 / 子网路由提供路径，在 USB 配对中填入可达 Hub URL。默认 security.lan_cidrs 包含 Tailscale CGNAT 段；自定义网段应明确配置。

## cloudflared + Access

前置：Cloudflare 管理权限、域名及已安装 cloudflared。Windows PowerShell：

```powershell
cloudflared tunnel login
cloudflared tunnel create nuka-hub
nuka tunnel setup
cloudflared tunnel route dns nuka-hub hub.example.com
cloudflared tunnel --config "<CONFIG_DIR>/cloudflared/config.yml" run nuka-hub
```

macOS：

```sh
cloudflared tunnel login
cloudflared tunnel create nuka-hub
nuka tunnel setup
cloudflared tunnel route dns nuka-hub hub.example.com
cloudflared tunnel --config "<CONFIG_DIR>/cloudflared/config.yml" run nuka-hub
```

Linux：

```sh
cloudflared tunnel login
cloudflared tunnel create nuka-hub
nuka tunnel setup
cloudflared tunnel route dns nuka-hub hub.example.com
cloudflared tunnel --config "<CONFIG_DIR>/cloudflared/config.yml" run nuka-hub
```

create 输出 Tunnel ID 与凭据文件路径；向导填写同一个 ID、路径、hostname 和 Access 参数。应生成 cloudflared/config.yml，入口指向配置中的 Hub 端口，末尾为 http_status:404。向导只写配置，不替你登录或创建云资源。

在 Zero Trust 创建对应 hostname 的 Self-hosted Access 应用。给 Web 用户添加允许策略；给设备/Agent 创建独立 Service Token 与 Service Auth 策略。复制实际团队域名和应用 AUD 到配置，重启 Hub：

```toml
[hub]
public_url = "https://hub.example.com"
port = 8790
[security]
web_approve_permissions = false
allow_lan_hooks = false
[security.cf_access]
enabled = true
team_domain = "access.example.com"
aud = "<ACCESS_AUD>"
require_for_ws = true
```

team_domain 示例只是占位符，必须用自己的 Access 实际团队域名。Hub 验证边缘签名 JWT 的 RS256、issuer、audience 和时效；仍要求 admin/device/agent 相应令牌。设备的 Service Token 经 nuka pair 隐藏交互写入 NVS；Agent 使用 `nuka secret set agent.cf_id` 与 `nuka secret set agent.cf_secret`，配置只放 cf_client_id_secret / cf_client_secret_secret 引用。仅 WSS 对应的 HTTPS origin 会收到这些凭据。

验证：公网健康接口返回 `{ok,ver,proto}`；没有 Access JWT 的其他公网请求返回 403；/hook/server 和 /hook/claude 返回 404。本机可用带 `Cf-Ray: fixture` 的请求验证 public 判定，不需要真实隧道。默认配置页和所有公网审批仍不可用；远程管理例外只按D30的完整身份门槛开放。doctor 的 hub_http 检查本地服务，不代表云端策略已通过。官方步骤见 [Service Tokens](https://developers.cloudflare.com/cloudflare-one/access-controls/service-credentials/service-tokens/)。

## 卸载与回滚

停止 cloudflared，删除专用 DNS 路由、Tunnel、Access 应用与 Service Tokens；保留其他应用。把 cf_access.enabled 设为 false 并重启 Hub，移除不再使用的 public_url，重新配对移除设备端凭据。Tailscale 用其客户端退出登录/卸载；不要改变其他节点 ACL。向导的 config.toml.bak.* 可用于恢复原配置。

## 常见错误

| doctor 输出 / 现象 | 处理 |
|---|---|
| `hub_http: degraded`，`Hub not reachable; start nuka hub and check hub.port` | 先恢复本地 Hub，再检查隧道 service 的端口 |
| `port: error`，`Port occupied by another process` | 修改监听端口，同时重做隧道入口 |
| `mdns: degraded`，`Hub discovery failed; check UDP multicast/firewall or use the paired URL` | 公网不转发 mDNS；配对时提供明确的 WSS URL |
| `firewall: degraded` | 使用 doctor 的本机防火墙提示检查 LAN；cloudflared 只需出站通道，不为公网开放直连端口 |
| 403 / 404 | 403 检查 Access aud/issuer/策略；404 检查是否操作被永久禁止的 hook/审批/配置接口。不要为绕过策略把代理加入 trusted_proxies |

即使 cloudflared 的 TCP 来源是回环地址，CF 头仍强制判为 public。uvicorn 不采信代理头，真实 TCP 来源由区域判定层统一处理。
