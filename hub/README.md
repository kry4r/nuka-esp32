# nuka-hub

nuka-esp32 的跨平台伴侣服务，负责指标采集、服务器与 AI 配额、CLI 会话和审批、通知、媒体、天气及设备管理，通过 WebSocket 向设备推送视图模型。默认使用简体中文；Web 管理页提供中文和英文。

需要 Python 3.11+ 和 uv。在本目录运行：

```sh
uv sync --all-extras
uv run nuka hub --demo
```

用启动时显示的管理员令牌登录 `http://localhost:8790`。另开终端运行 `uv run nuka sim-device --token <YOUR_TOKEN>`，填入 demo 显示的设备令牌。`--demo-state loading|offline|unconfigured` 可以切换卡片状态；demo 使用合成数据。

实际部署运行 `uv run nuka init`、`uv run nuka doctor`，再启动 `uv run nuka hub`。配置路径可以通过 `--config` 或 `NUKA_CONFIG` 指定。密钥通过 `nuka secret set <name>` 隐藏输入，配置仅保存密钥名称。

安装与集成步骤见仓库的 [安装引导](../docs/guides/)，配置字段见 [CONFIG](../docs/CONFIG.md)，设备和 Web 接口见 [PROTOCOL](../docs/PROTOCOL.md)。[M3](../docs/bench/be-m3.md) 和 [M5](../docs/bench/be-m5.md) 记录已执行的验收及环境限制。

```sh
uv run pytest
uv run ruff check .
uv run ruff format --check .
```

包名 `nuka-hub`，导入名 `nuka_hub`，命令 `nuka`，hook 转发器 `nuka-hook`，环境变量前缀 `NUKA_`，钥匙串服务名 `nuka-hub`。采用 [AGPL-3.0-or-later](LICENSE)；依赖及外部工具许可证见 [THIRD_PARTY](../THIRD_PARTY.md)。
