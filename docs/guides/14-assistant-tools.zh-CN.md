# MCP 工具与联网搜索

适用于 Windows、macOS 和 Linux 的本地管理员会话，需要 Python 3.11+ 和 Hub 的 voice 可选依赖。先按[语音助手引导](13-voice-assistant.zh-CN.md)安装。远程管理还需满足[服务器引导](15-server-deployment.zh-CN.md)的 Access、Hub 会话和 CSRF 要求。模型输出、搜索摘要、MCP 注解都不能授权动作。

## 安装与验证 SDK

Windows PowerShell：

```powershell
uv sync --project hub --locked --extra voice
uv run --project hub --extra voice python -c "from importlib.metadata import version; from mcp import Client; print(version('mcp'))"
uv run --project hub --extra voice nuka doctor
```

macOS：

```sh
uv sync --project hub --locked --extra voice
uv run --project hub --extra voice python -c 'from importlib.metadata import version; from mcp import Client; print(version("mcp"))'
uv run --project hub --extra voice nuka doctor
```

Linux：

```sh
uv sync --project hub --locked --extra voice
uv run --project hub --extra voice python -c 'from importlib.metadata import version; from mcp import Client; print(version("mcp"))'
uv run --project hub --extra voice nuka doctor
```

应看到锁文件中的 MCP SDK `2.3.0`，来自 [MIT 许可的官方 Python SDK](https://github.com/modelcontextprotocol/python-sdk/tree/v2.3.0)。其 HTTP 传输使用 SDK 自带的 httpx2 依赖；Hub 普通 HTTP 适配器仍用 httpx。Hub 已运行时，doctor 应有 `hub_http ok` / `Hub health reachable`；这不会探测外部 MCP。

## 接入与逐项启用

**stdio** 的可执行程序和固定参数只由管理员填写，模型不能选择 shell、命令或参数。启用前先安装、审阅服务器：启动它就是以 Hub 服务账号的权限运行该程序。SDK 只继承少量允许的环境变量；需要密钥时只显式传入该服务器需要的钥匙串引用。不要把密钥放在参数、URL 或配置正文中。第三方 stderr 被丢弃，不会转存到 Hub 日志。

**Streamable HTTP** 填准确的 HTTP / HTTPS 地址，通常以 `/mcp` 结尾；Bearer 令牌存在系统钥匙串中，配置只填名称。SDK 拒绝跨源重定向，应改为最终地址，而不是关闭限制。接口细节见[官方传输文档](https://github.com/modelcontextprotocol/python-sdk/blob/v2.3.0/docs/client/transports.md)。

发现工具后显示其名称、说明与参数 schema。每个工具默认关闭、默认敏感，需要逐个勾选。管理员审阅后，才能把确定只读的工具改为免确认；服务器的 `readOnlyHint=true` 没有授权效力。敏感工具等待已配对设备物理确认；拒绝、超时、断开或打断均不执行。没有设备确认通道的纯文字会话不能批准敏感工具。

修改前备份 `config.toml`，按[引导 13](13-voice-assistant.zh-CN.md)配置并启用助手的 LLM。工具配置字段以冻结的 [CONFIG](../CONFIG.md) 为准，在 Web 配置表单填写。以下是回滚开关，不能替代逐工具策略：

```toml
[assistant]
enabled = false
```

## 本地联调示例

仓库的[示例服务器](14-assistant-tools-example.py)提供 `read_counter` 和 `increment_counter`，状态完全是假数据，仅存在该进程内。两个工具都故意自报只读，包含实际修改计数器的工具，用于检查 Hub 是否仍按自己的策略确认。不要在批准的 T3 / T9 之外增加自动化集成测试。

stdio 配置中，命令填 Hub 环境 Python 的绝对路径，唯一固定参数填示例文件绝对路径。Windows PowerShell、macOS、Linux 都可用下面的命令找到 Python：

```sh
uv run --project hub --extra voice python -c "import sys; print(sys.executable)"
```

脚本位于当前仓库的 `docs/guides/14-assistant-tools-example.py`。手工启动时没有欢迎输出，程序在等待 MCP 客户端。把 `read_counter` 设为启用、只读，把 `increment_counter` 设为启用、敏感。未勾选的工具不能出现在给模型的工具列表中。

Streamable HTTP 在三平台单独终端运行：

```sh
uv run --project hub --extra voice python docs/guides/14-assistant-tools-example.py --http --port 8909
```

应只监听 `http://127.0.0.1:8909/mcp`，在私有配置中填这个地址，联调期间保持终端运行。它是只在本机使用的假数据示例，因此无需密钥；不要通过隧道或公网暴露。

两种传输都通过 Hub 工具闸门检查：读取为 0；拒绝增加后仍为 0；超时后仍为 0；允许增加后为 1；再次读取为 1。在等待审批时打断，数值也不能变化。只读注解不得抹掉敏感确认。HTTP 终端按 Ctrl+C 停止；关闭 stdio 配置应结束对应子进程。重开示例进程后计数器重新从 0 开始。

## 搜索提供方

**SearXNG** 使用自建 `/search`，参数 `format=json`。在其私有 `settings.yml` 开启 JSON：

```yaml
search:
  formats:
    - html
    - json
```

重启 SearXNG，Hub 中填自己的 base URL，例如 `https://search.example.com`。[官方 Search API](https://docs.searxng.org/dev/search_api.html)说明未开启的输出格式返回 403，很多公共实例关闭了 JSON。查询可能转发到该实例启用的搜索引擎。网关若需要 Bearer 令牌，仍保存在钥匙串中。

**Brave Search** 使用 Web Search API。按[官方快速开始](https://api-dashboard.search.brave.com/documentation/quickstart)创建 API 订阅和密钥，保存到钥匙串，在 Hub 填 Brave 提供的 API base URL。适配器只在服务器请求的 `X-Subscription-Token` 头中发送密钥，不写入查询参数。费用与配额由订阅决定，开通真实搜索前核对。统一结果包含标题、HTTP / HTTPS URL 和有限长度的摘要；这些内容都是不可信资料，不能启用工具或改写助手系统权限。

Windows PowerShell、macOS 和 Linux 均可运行下列命令，在交互提示中输入，避免把密钥写进命令：

```sh
uv run --project hub --extra voice nuka secret set assistant.search
```

离线验证运行 `uv run --project hub --locked --extra voice pytest hub/tests/test_t3_voice.py`。SearXNG 和 Brave 各有一个假数据 fixture 测试，不请求公共搜索引擎。真实搜索在确认账号和地址后另做手工验证。

## 回滚与排错

逐个关闭 MCP 服务器和搜索提供方，需要时恢复配置备份，再重启 Hub。用同一环境的 `nuka secret rm assistant.search`，或实际 MCP 密钥名称，删除不再需要的钥匙串项。本地示例没有数据库或文件需要清理。关闭工具不会删除对话和长期记忆；关闭集成后运行 `uv sync --project hub --locked` 可从源码环境移除可选依赖。

| 现象 / doctor 输出 | 排查 |
|---|---|
| `hub_http degraded`、`Hub not reachable; start nuka hub and check hub.port` | 用预期配置启动 Hub，核对端口。 |
| `hub_http ok`，stdio 发现失败 | 填绝对程序、脚本路径，安装服务器自身依赖，显式提供必需的钥匙串环境变量。doctor 不会启动 MCP 服务器。 |
| `hub_http ok`，HTTP 连接或重定向失败 | 填最终 `/mcp` 地址，检查证书、令牌权限，并到服务器自身环境排查；不要在 URL 中放凭据。 |
| `hub_http ok`，敏感工具提示无法确认 | 连接预期的配对设备；网页文字不能代替物理确认。拒绝后示例计数器必须不变。 |
| `hub_http ok`，SearXNG 返回 403 | 开启 JSON 格式，检查网关鉴权；不要重复请求禁止 JSON 的公共实例。 |
| `hub_http ok`，Brave 返回 401 / 403 / 429 | 检查密钥订阅、有效性和配额；每次搜索仅发一个请求，不自动重试。 |

`nuka doctor` 当前报告 Hub 可达性和既有插件健康状态，不代表外部 MCP 或搜索可用；验收报告中应分别记录。
