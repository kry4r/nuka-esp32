# BE M3 验收记录

日期：2026-10-18。环境：Windows、CPython 3.12.12、uv；所有上游为 fixture 或关闭。未连接真实 CPA / CPAMP、探针或天气服务，未修改用户实际 hooks。

| 任务卡 | 验收 | 结果 |
|---|---|---|
| HUB-01 | 包、extras、nuka / nuka-hook、全部子命令 | 通过：本机安装后 CLI 可导入并列出子命令。macOS/Linux 干净安装交发布 CI 验证，未在本机声称通过 |
| HUB-02 | CONFIG 原样加载、未知键提示、环境覆盖、局部热重载 | 通过：完整示例含 6 个 source；拼错 port 提示行号及候选键；NUKA__HUB__PORT 覆盖生效；修改 notify 不更换运行中的 psutil Task |
| HUB-03 | 插件隔离和重试时间 | 通过：故意失败插件为 error，detail 含 next retry；健康插件持续采样 |
| HUB-04 | T1 区域矩阵与鉴权 | 通过：31 项，包括所有矩阵行、CF 头回环 public、HTTP 404、CSRF、非法可信转发地址 400 |
| HUB-05 | 表结构、WAL、小时清理 | 通过：所需表齐全；PRAGMA 返回 wal；旧数据聚合为一个 5 分钟点，40 天旧点删除；后台任务每 3600 秒调用 |
| HUB-06 | 握手、订阅、限流、唤醒、关闭码、截断 | 通过：真实 TCP WS，计时见下表；重复连接 4008、撤销令牌 4003；列表按上限截断并记录 more，UTF-8 帧不超过 8192 字节 |
| HUB-07 | demo、sim-device、T6 | 通过：demo 四态命令可选；真实 60 秒 T6 包含 10 个 VM 主题与审批往返；REST 与设备共享 StateStore |

真实 WS 默认节流观测（单位秒，16.5 秒窗口，初始快照不计）：

| 场景 | 实际 |
|---|---|
| Hosts 当前卡 | 1.03、2.05、3.08…16.44 |
| Hosts 非当前卡 | 5.01、10.01、15.05 |
| Servers 当前卡 | 5.01、10.01、15.05 |
| Servers 非当前卡 | 15.05 |
| AI | 10.01（变化驱动，最小 10 秒） |
| Host detail | 0.05、5.08、10.08、15.09 |
| 低电 Hosts | 5.01、10.01、15.05 |
| 熄屏 / 唤醒 | 熄屏零 VM；唤醒后立即收到全部 8 个基础 VM 快照 |
| 审批 / 通知 | 60 秒 T6 实际收到 perm.req、perm.done、notify.push；事件队列不受周期节流影响 |

验证命令：

```sh
hub/.venv/Scripts/python.exe -m pytest hub/tests/test_t1_zones.py hub/tests/test_t2_ai.py hub/tests/test_t3_adapters.py hub/tests/test_t4_alerts.py hub/tests/test_t5_approval.py -q
hub/.venv/Scripts/python.exe -m pytest hub/tests/test_t6_demo.py -q
ruff check hub
ruff format --check hub
```

结果：T1–T5 共 74 passed；T6 为 1 passed（63.56 秒）。T4 的无效表达式案例产生预期的未知字段 warning。另一次 60 秒抓帧得到 59 帧、16 种消息类型，统一校验器报告 65 schemas / 59 payloads / 0 failures。原始抓帧和计时记录放在被忽略的 build/bench-be/ 下，不含凭据。

边界与限制：尚未完成真机配对和 Vault 往返；这属于 HUB-23/HUB-30 与 FW 的 M4/M6 联调。未把跨平台媒体控制、服务重启验收或发布 runner 结果写成通过。M5 继续推进，视觉打磨留给 FE。
