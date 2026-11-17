# nuka-esp32

面向 Waveshare ESP32-S3-Touch-AMOLED-2.16 的全新设备固件，以及跨平台伴侣服务 **nuka-hub**。

开发约束见 [AGENTS.md](AGENTS.md)，需求见 [SPEC](docs/SPEC.md)，任务卡见 [PLAN](docs/PLAN.md)。设备、Hub 和 Web 只以 [PROTOCOL](docs/PROTOCOL.md)、[CONFIG](docs/CONFIG.md) 和 [protocol/](protocol/) 为契约。

当前为开发版本：固件、Hub、Web 管理页和 SDL 模拟器已有实现，第二阶段的按键语音助手与设备 UI 按 [PLAN-v2](docs/PLAN-v2.md) 推进。真机、跨平台和系统联调仍有待验收项，详见 [进度记录](docs/bench/progress.md) 和 [交接清单](docs/bench/handoff.md)。

nuka-hub 负责本机与多机指标、服务器监控、AI 配额、CLI 会话和审批、通知、媒体及天气采集，通过 WebSocket 把视图模型交给设备。默认使用简体中文；Web 管理页提供设备、配置、密钥、令牌、审批和通知管理。

先体验合成数据：

```sh
cd hub
uv sync --all-extras
uv run nuka hub --demo
```

用启动时显示的管理员令牌登录本机 `http://localhost:8790`。另开终端运行 `uv run nuka sim-device --token <YOUR_TOKEN>`，令牌使用 demo 启动时显示的 device 令牌。demo 不连接任何真实上游。

实际部署先运行 `uv run nuka init` 和 `uv run nuka doctor`，再运行 `uv run nuka hub`。安装方式见 [Windows](docs/guides/02-install-hub-windows.zh-CN.md)、[macOS](docs/guides/02-install-hub-macos.zh-CN.md)、[Linux](docs/guides/02-install-hub-linux.zh-CN.md)、[Docker](docs/guides/02-install-hub-docker.zh-CN.md)；配对见 [设备配对](docs/guides/03-pair-device.zh-CN.md)。引导均附英文版本。

| 目录 | 内容 / 负责轨道 |
|---|---|
| `firmware/` | ESP-IDF 5.5 / LVGL 9 固件，FW |
| `hub/` | Python 3.11+ 的 nuka-hub，BE |
| `protocol/` | JSON Schema、帧样例、WMO 映射，C |
| `design/` | 共用设计 token，C；M7 视觉优化由 FE 负责 |
| `i18n/` | 中英文字符串，FW |
| `docs/guides/` | 随集成交付的中英文安装引导 |
| `scripts/` | 契约、许可证和零硬编码检查，C |
| `site/` | 网页刷机页，FW |

契约工具依赖由 uv 锁定：

```sh
uv sync --project scripts --locked
uv run --project scripts python scripts/check_no_personal.py
uv run --project scripts python scripts/check_license.py
uv run --project scripts python scripts/check_protocol.py
python firmware/tools/gen_theme.py
```

Hub 验证命令：

```sh
cd hub
uv run pytest
uv run ruff check .
uv run ruff format --check .
```

固件构建方法见 [从源码构建](docs/guides/11-build-from-source.zh-CN.md)，桌面预览见 [SDL 模拟器说明](firmware/sim/README.md)。本地验证记录见 [仓库整理验收](docs/bench/repository-publish.md)、[M3](docs/bench/be-m3.md)、[适配器验收](docs/bench/be-adapters.md)、[系统联调](docs/bench/e2e-report.md) 和 [第二阶段 UI 评审](docs/bench/ui-v2-review.md)；实际验收状态以对应报告为准。

项目采用 [AGPL-3.0-or-later](LICENSE)，依赖见 [THIRD_PARTY.md](THIRD_PARTY.md)。[发布工作流](.github/workflows/release.yml) 会根据 GitHub 仓库元数据填入安装包的源代码地址。
