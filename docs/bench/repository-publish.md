# 仓库整理与首次推送验收

日期：2026-11-16。维护者已授权整理当前代码并新建公开 GitHub 仓库。本次对应 C-03 的仓库整理与提交检查，保留现有固件、Hub、协议、资源和文档的开发快照；不将既有任务卡或整机里程碑标记为完成。

整理内容：更新根目录和固件 README 的入口及开发状态，补充交接记录的时间语境，移除交接文档中的私人开发会话标识。扩充编译、链接及 CMake 产物的忽略规则，从版本控制中移除 CMake 自动复制的 firmware/main/idf_component.yml，保留其源文件 firmware/idf_component.yml；保留本地生成文件以供构建。契约改动：否。新增依赖：无。

| 验收项 | 状态 | 方法与结果 |
|---|---|---|
| 公开文件范围 | 通过 | 核对 Git 候选清单；排除 local/、备份、配置、密钥、Vault 文件、SDK 组件、缓存及构建产物。提交作者使用通用项目身份，避免公开本机 Git 配置中的个人邮箱。 |
| 零硬编码 | 通过 | `python scripts/check_no_personal.py`：零失败。 |
| 源文件许可证 | 通过 | `python scripts/check_license.py`：343 个源文件，零失败。 |
| 协议 T6 | 通过 | `scripts/.venv/Scripts/python.exe scripts/check_protocol.py`：82 个 schema、171 个 payload，零失败。 |
| 主题及翻译 | 通过 | `python firmware/tools/gen_theme.py --check`、`python firmware/tools/gen_strings.py --check`；268 个翻译键匹配。 |
| 依赖锁定 | 通过 | Hub 和 scripts 均通过 `uv lock --check --offline --project <目录>`，未更改依赖。 |
| Hub T1–T6 / T9 | 通过 | 在 hub/ 下运行 `uv run --locked --offline pytest -q`：105 passed；T4 拒绝非法表达式产生一条预期 warning。使用 fixture 和本机 demo，不连接真实上游。 |
| Hub 格式与 lint | 通过 | 在 hub/ 下运行 `uv run --locked --offline ruff check .` 和 `uv run --locked --offline ruff format --check .`；70 个文件格式正确。 |
| 固件构建 | 通过 | 现有 Windows ESP-IDF 5.5.6 环境执行 `idf.py -C firmware build`，应用大小 0x1c2de0，应用分区余量 56%；本次为增量构建。 |
| Vault T7 / 解析 T8 | 通过 | WSL 中分别构建 build/vault-t7、build/proto-t8，再运行 `ctest --test-dir <目录> --output-on-failure`；两个既有测试目标均通过。 |
| SDL 构建与运行 | 通过 | Windows 构建 `cmake --build build/sim-win --config Debug --target nuka_sim --parallel 8`；dummy 驱动加载已有 ABI-1 资源包，Clock 正常态运行 12 帧并成功截图，产物保留在被忽略的 build/。 |
| 文档图片 | 通过 | 目视核对现有 8 份截图或动图预览，内容使用示例数据。 |
| 真机、功耗、浸泡与跨平台全量验收 | 跳过 | 本次范围为仓库整理与推送；既有报告中的待验收项继续保留。 |

本地检查不能替代远程 GitHub Actions；推送后的 workflow 结果以仓库 Actions 页面为准。后续功能仍按 PLAN / PLAN-v2 对应任务卡推进，无需其他轨道为本次整理修改契约。
