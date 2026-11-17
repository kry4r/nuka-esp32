# MCP tools and search

Use a local administrator session on Windows, macOS or Linux, Python 3.11+, and the Hub's `voice` extra. [Install the assistant dependencies](13-voice-assistant.en.md) first. A remote administrator session additionally needs the Access identity, Hub session and CSRF checks described in the [server guide](15-server-deployment.en.md). Model output, search snippets and MCP annotations never authorize actions.

## Install and verify the SDK

Windows PowerShell:

```powershell
uv sync --project hub --locked --extra voice
uv run --project hub --extra voice python -c "from importlib.metadata import version; from mcp import Client; print(version('mcp'))"
uv run --project hub --extra voice nuka doctor
```

macOS:

```sh
uv sync --project hub --locked --extra voice
uv run --project hub --extra voice python -c 'from importlib.metadata import version; from mcp import Client; print(version("mcp"))'
uv run --project hub --extra voice nuka doctor
```

Linux:

```sh
uv sync --project hub --locked --extra voice
uv run --project hub --extra voice python -c 'from importlib.metadata import version; from mcp import Client; print(version("mcp"))'
uv run --project hub --extra voice nuka doctor
```

Expect MCP SDK `2.3.0` from the lockfile. It is the [official Python SDK](https://github.com/modelcontextprotocol/python-sdk/tree/v2.3.0), under MIT. Its HTTP transport uses its own `httpx2` dependency; the Hub's ordinary HTTP adapters continue to use httpx. A running Hub reports `hub_http ok` / `Hub health reachable`; this does not probe an MCP endpoint.

## Connect and enable tools

Only an administrator supplies an executable and fixed arguments for **stdio**. The model cannot choose a shell, command or argument list. Install and review the selected server before enabling it: launching a server runs its program with the Hub service account's permissions. The SDK starts the process with a small allowed environment; pass only explicit keyring references needed by that server. Do not place secrets in arguments, URLs or config text. Third-party stderr is discarded instead of copied into Hub logs.

For **Streamable HTTP**, use the exact HTTP / HTTPS endpoint, normally ending in `/mcp`. Store a Bearer token in the system keyring and reference its name. The SDK rejects redirects to another origin; correct the configured final URL instead of disabling this protection. See the SDK's [transport documentation](https://github.com/modelcontextprotocol/python-sdk/blob/v2.3.0/docs/client/transports.md).

Discovery lists tools and their argument schemas. Every tool starts disabled and sensitive. Enable each intended tool explicitly. Only an administrator who has reviewed a read-only tool should remove its confirmation requirement; `readOnlyHint=true` is a server claim and has no authority. A sensitive call waits for the paired device's physical confirmation; reject, timeout, disconnect or cancellation means it does not execute. Text-only sessions without a device confirmation channel cannot approve sensitive tools.

Back up `config.toml` before changing tools. The assistant must be enabled with the LLM configuration in [guide 13](13-voice-assistant.en.md); tool fields follow the frozen [CONFIG](../CONFIG.md) schema and appear in the Web configuration form. The following is the rollback switch, not a substitute for per-tool policy:

```toml
[assistant]
enabled = false
```

## Local acceptance example

The bundled [example server](14-assistant-tools-example.py) exposes `read_counter` and `increment_counter`. Its state is synthetic and held only in the process. Both deliberately claim to be read-only, including the mutating tool, so permission must come from Hub policy. Do not add automated integration tests outside the approved T3/T9 list.

For stdio, configure the absolute Python executable from the Hub environment and the absolute path to the example file as its single fixed argument. Find the executable on Windows PowerShell, macOS and Linux with:

```sh
uv run --project hub --extra voice python -c "import sys; print(sys.executable)"
```

The file path is `docs/guides/14-assistant-tools-example.py` in this checkout. Starting it manually in a terminal produces no welcome banner: it waits for an MCP client. Configure `read_counter` as enabled / read-only and `increment_counter` as enabled / sensitive. An unlisted tool must remain absent from the model's tools.

For Streamable HTTP, run this in a separate terminal on each platform:

```sh
uv run --project hub --extra voice python docs/guides/14-assistant-tools-example.py --http --port 8909
```

The server should listen only at `http://127.0.0.1:8909/mcp`. Configure that endpoint privately and keep the terminal open for the manual check. It needs no key because it is a synthetic loopback example; do not expose it on a tunnel or public interface.

For both transports, verify this sequence using the Hub tool gate: read returns zero; denied increment leaves zero; timed-out increment leaves zero; allowed increment returns one; read returns one. Repeat cancellation while waiting for approval and verify that the value does not change. Read-only annotations must not suppress the sensitive confirmation. Stop the HTTP terminal with Ctrl+C, or disable the stdio server to close its child process. Reopening the example starts its counter at zero.

## Search providers

**SearXNG** uses a self-hosted `/search` endpoint with `format=json`. Enable JSON in its private `settings.yml`:

```yaml
search:
  formats:
    - html
    - json
```

Restart SearXNG and set the Hub search base URL to your own origin, such as `https://search.example.com`. Its [official Search API](https://docs.searxng.org/dev/search_api.html) explains that an unavailable output format returns 403; many public instances disable JSON. Queries may be forwarded to the search engines enabled on that instance. An optional gateway Bearer token belongs in the keyring.

**Brave Search** uses the Web Search API. Create an API subscription and key using the [official quickstart](https://api-dashboard.search.brave.com/documentation/quickstart), save it as a keyring secret, and configure the API base URL supplied by Brave. The adapter sends `X-Subscription-Token` in the server-side request and never puts it in a query string. Charges and quotas depend on the subscription; verify them before enabling live search. The result interface includes title, HTTP / HTTPS URL and a bounded snippet. Search results are untrusted source material and cannot enable tools or change the assistant's system policy.

On Windows PowerShell, macOS or Linux, save a key without echoing it in a command:

```sh
uv run --project hub --extra voice nuka secret set assistant.search
```

For offline verification, run `uv run --project hub --locked --extra voice pytest hub/tests/test_t3_voice.py`. SearXNG and Brave each have one synthetic fixture test; no public search engine is contacted. Live search is a later, explicit manual check after choosing the account and endpoint.

## Rollback and troubleshooting

Disable each MCP server / search provider, restore the private config backup if needed, and restart Hub. Remove unused keyring entries with `nuka secret rm assistant.search` or the MCP secret's actual name in the same environment. The local example has no database or files to delete. Disabling tools does not delete archived conversations or long-term memories. To remove optional packages from the source environment, run `uv sync --project hub --locked` after disabling the integrations.

| Symptom / doctor output | Check |
|---|---|
| `hub_http degraded`, `Hub not reachable; start nuka hub and check hub.port` | Start Hub with the intended configuration and check the port. |
| `hub_http ok`, stdio discovery fails | Use an absolute executable and script path; install that server's dependencies and pass required keyring-backed environment entries. Doctor does not launch MCP servers. |
| `hub_http ok`, HTTP connection / redirect fails | Use the final `/mcp` endpoint, verify its TLS certificate and token permissions, and check the server in its own environment. Do not put credentials in URLs. |
| `hub_http ok`, sensitive tool says confirmation unavailable | Connect the intended paired device; Web text input cannot substitute for a physical decision. A rejected call must leave the example counter unchanged. |
| `hub_http ok`, SearXNG returns 403 | Enable the JSON format and check gateway authentication; do not repeatedly query a public instance that forbids it. |
| `hub_http ok`, Brave returns 401 / 403 / 429 | Check the key's subscription, validity and quota. The adapter makes one request per search and does not retry automatically. |

`nuka doctor` currently reports Hub connectivity and existing plugin health, not external MCP or search availability. Separate those observations when reporting acceptance.
