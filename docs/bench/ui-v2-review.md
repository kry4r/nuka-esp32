# PLAN-v2 设备 UI 交付与验收记录

2026-10-24，任务卡 FW2-01、FE2-02、FW2-03、FE2-04。以下结论限于实现、SDL 构建及手工重放；不代表 M9 或实机评审通过。

## 验收状态

| 卡 / 验收项 | 状态 | 证据或原因 |
|---|---|---|
| FW2-01：10 个页面的居中标题、中英文字及截断布局 | 通过（SDL） | [20 页截图](ui-v2-titles.png)，标题组固定在物理中心，左右按实际宽度预留，内容距离屏幕边缘 ≥48 px |
| FW2-01：L1 返回箭头随标题居中、主页两面不显示标题 | 通过（SDL） | AI L1 与主页截图 |
| FW2-01：实机圆角没有裁切 | 跳过 | 需要实机照片 |
| FE2-02：正常、冷却、重登、熔断与多账号 | 通过（SDL fixture） | [状态截图](ui-v2-ai.png)，BOOT 长按切换账号；breaker_open 与账号 status 独立 |
| FE2-02：数值变化局部动画 | 通过（实现 / SDL） | 主百分比插值与三个统计值局部滑入均用 ANIM_BASE；未变化的值不启动动画 |
| FE2-02：L1 迷你双环、恢复时间、刷新 | 通过（SDL） | AI L1 截图；沿用 Hub 每分钟一次的 ai.refresh 限流 |
| FE2-02：按模型 Top 3 | 跳过 | 现有 VM 没有该数据，按任务卡不自行加字段 |
| FE2-02：demo Hub 到设备联调、实机评审 | 跳过 | 本轮截图来自合法协议 fixture；尚缺 demo 联调与实机反馈 |
| FW2-03：主页两面、其他卡返回主页、L1 返回 | 通过（SDL） | 内部 Key3 手工重放，保留 clock 作为两面共同的 App ID |
| FW2-03：Key3 双击跳 Buddy | 通过（SDL） | 两次 100 ms 按住，最终 card=buddy，没有 voice.start |
| FW2-03：按下预录事件、400 ms 确认、松开结束 | 通过（SDL 事件） | 200 ms：prepare/cancel 后短按切面；500 ms：prepare/start/end |
| FW2-03：30 s 自动结束 | 通过（SDL 事件） | 持续按住 31 s，prepare 在 340 ms、start 在 748 ms、end 在 30359 ms，只出现一次 end；相对按下 30.019 s 结束 |
| FW2-03：回答期间短按打断、按住开始新一轮 | 通过（SDL 事件） | 预置 speaking 后短按为 stop/prepare/cancel 并停在 idle；长按为 stop/prepare/start/end |
| FW2-03：Vault / 审批 / 恢复菜单禁用语音 | 通过（SDL） | 500 ms 手工重放没有 voice.prepare 或 voice.start；Vault 原按键功能保留 |
| FW2-03：快捷面板勿扰 | 通过（实现） | 复用已有快捷面板勿扰开关，移除默认 Key3 长按的勿扰映射；可配置旧快捷动作 |
| FW2-03：真实采音与上传 | 跳过 | 本卡只发内部事件，FW2-06 接入音频 |
| FW2-03：实机触摸与三键逐项走查 | 跳过 | 需要实机操作 |
| FE2-04：14 种状态、局部过渡、眨眼、视线和字幕入口 | 通过（SDL 预览） | [静态图](ui-v2-faces.png) / [状态录屏](ui-v2-face-motion.gif)；预览音量为合成包络 |
| FE2-04：隐藏 / 熄屏 / 低电暂停定时器 | 通过（SDL） | [调度记录](face-perf.md) |
| FE2-04：设备 FPS、CPU、功耗和实机评审 | 跳过 | 原因和所需证据见 face-perf.md |

## 手工验证方法

模拟器没有访问真实服务，也没有向桌面注入鼠标或键盘。以 SDL 的 dummy 视频驱动启动，再用内部 --presses 重放按键。构建命令为 cmake --build build/sim-win --config Debug --target nuka_sim --parallel 4。示例：

```text
nuka_sim --assets build/nukapack-current.bin --enable-assistant --presses 3:200 --frames 90
nuka_sim --assets build/nukapack-current.bin --enable-assistant --presses 3:500 --frames 100
nuka_sim --assets build/nukapack-current.bin --enable-assistant --open permission --presses 3:500 --frames 100
nuka_sim --assets build/nukapack-current.bin --face assistant --pose idle --battery 5 --frames 180
```

41 个静态捕获完成并检查三张拼图；14 个状态各自录制为 GIF，再合成供审阅的拼图。所有样例账号、主机与网络均为仓库假数据；多账号和时间倒计时变体只存忽略目录，没有新增自动化测试。

构建中首次因 Visual Studio 在 CMake 重配前已加载旧源文件列表，仍引用已删除的 pet_view.c 而失败；重新构建后通过。默认旧 build/nukapack.bin 缺少当前资源版本条目，按解析器要求改用有效的 build/nukapack-current.bin，没有跳过资源校验。

## 实现选择与轨道配合

- 主页在跨卡片导航时记住上次显示面；重新启动使用既有 NVS/cfg.home_face 默认面。短按切面不写 Flash、不设置 local_override，Hub 仍能修改默认面。
- 原像素桌宠视图已删除，pet_app.c 仅保留本地情绪与手势逻辑，VM_PET 订阅保留；独立 pet 卡不再出现在 registry。
- C2-01 由契约轨道单独交付：home_face、volume、assistant、key3_long=talk 和 ai.breaker_open。UI 没有自行添加网络字段。
- FW 轨道持有 store、hub_client 与构建表，负责新配置接收、旧 pet-only 卡列表的存量迁移及真实硬件。UI 已向 FW 提供 home_app.h、face_view.h 和 NUKA_VOICE_* 内部事件。
- 字幕入口当前只承载短本地提示；FW2-06 应按 C2-02 的最终长度上限对齐缓冲并保持 UTF-8 完整。真实录音、网络语音会话、功放与屏幕会话锁均不在本轮 UI 交付中冒充完成。
- git 仓库没有首个提交。本轮未提交、未推送，也没有运行远端 CI。

当前无需为 UI 实现新增维护者决策；剩余事项是实机视觉反馈、性能证据及 FW2-06 联调。

本轮最终检查：SDL Debug 构建通过；268 个中英翻译键一致；零个人值检查 671 个文本文件、0 处问题；SPDX 检查 334 个源文件、0 处问题。文件数随并行轨道继续工作而变化。
