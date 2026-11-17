# 从源码构建

适用于贡献者：Windows PowerShell、macOS 或 Linux；准备 Git、Python 3.11+、uv、Node.js 20+、CMake、Ninja 和 ESP-IDF **v5.5.6**。不复用原厂固件。上游环境安装见 [ESP-IDF 官方文档](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/get-started/)。Windows 需要 Visual Studio C++ 工具，macOS 需要 Xcode 命令行工具；Linux 需要本机 C 编译器与窗口系统开发头文件。模拟器的 CMake 会获取固定版本的 SDL2 等源码，也可指定本地源码离线构建。

## 安装工具链

在独立工作目录克隆工具链：

```sh
git clone --recursive --branch v5.5.6 https://github.com/espressif/esp-idf.git esp-idf
```

Windows PowerShell：

```powershell
cd esp-idf
.\install.ps1 esp32s3
.\export.ps1
```

macOS / Linux：

```sh
cd esp-idf
./install.sh esp32s3
. ./export.sh
```

回到项目根目录，运行 `idf.py --version`，应该显示 v5.5.6。下载慢时可使用 IDF 工具自身支持的官方资产镜像，仍由工具检查文件 SHA-256，不跳过证书或校验。

## 生成资源并构建


```sh
npm ci --prefix firmware/tools --ignore-scripts
uv run --with-requirements firmware/tools/requirements.txt python firmware/tools/gen_fonts.py
python firmware/tools/gen_strings.py
python firmware/tools/gen_theme.py
python firmware/tools/pack_assets.py --version 0.1.0
idf.py -C firmware set-target esp32s3
idf.py -C firmware build
```

应该生成 `build/nukapack.bin`、旁边的版本/ABI/大小/SHA-256 JSON，以及 `firmware/build/nuka-esp32.bin`。发布时将 `--version` 改为资源包实际版本，该值会写入包内并由设备上报。完整中英字体 pack 当前约 2.32 MB，构建器硬限 3 MiB；实际大小以当次输出为准。源字体和 OFL 许可证在 assets_src/fonts；输出在 git 忽略的 build。`gen_strings.py --check` 和 `gen_theme.py --check` 应通过。所有字号数字使用等宽数字字形。

只构建英文资源时，Windows 在上述命令前运行 `$env:NUKA_LANG_PACK='en-only'`；macOS/Linux 运行 `export NUKA_LANG_PACK=en-only`。恢复双语时清除该环境变量并重建。资源包与固件必须使用相同 ABI。首次刷机完整备份 16 MiB，再按[刷机引导](01-flash-firmware.zh-CN.md)逐分区写入；禁止整片擦除和 eFuse 烧录。

## SDL 与 Hub

Windows 在 VS C++ 工具终端、macOS/Linux 在普通终端运行：

```sh
cmake -S firmware/sim -B build/sim
cmake --build build/sim
```

运行 `build/sim/nuka_sim`（Windows 按构建器目录运行 nuka_sim.exe），应该看到 480×480 功能界面。模拟器从 protocol/examples 加载数据；参数、截图方法和平台依赖见[模拟器说明](../../firmware/sim/README.md)。设备与 Hub 的实际连接另用 demo Hub 验证。

Hub 开发在三个平台相同：

```sh
cd hub
uv sync --all-extras --locked
uv run nuka init
uv run nuka hub --demo
```

另一个终端执行 `uv run nuka sim-device --duration 60`，应收到持续的 VM 帧；通过现有 T6 校验捕获。开发阶段外部集成只用 fixture，不访问真实 CPA/CPAMP/探针/天气。

config.toml 示例（不含任何凭据）：

```toml
[hub]
name = "<HUB_NAME>"
host = "0.0.0.0"
port = 8790
```

运行 `uv run pytest` 只执行 T1–T6；固件只执行 T7/T8，其他验收靠任务卡手工步骤。运行 `uv run ruff check .`、`uv run ruff format --check .`；回到根目录运行 `python scripts/check_no_personal.py` 与 `python scripts/check_license.py`。

## 卸载与回滚

退出模拟器、停止开发 Hub，保留个人配置与加密备份。若装过服务，先执行 `nuka service uninstall`；若装过 uv 全局工具，用 `uv tool uninstall nuka-hub`。工作树可切回已知提交后重新生成资源；不要在错误的 ABI 配对上继续刷写。

## 常见问题与诊断

- 找不到 idf.py：在当前终端重新执行 export 脚本，检查版本；`doctor` 检查运行中的 Hub/设备，不负责安装编译器。
- 资源大于 3 MiB：检查是否误把额外字号或完整汉字集放入 pack；生成器会直接失败，不截断资源。可使用 en-only。设备英文 ABI 错误页不由 doctor 代替校验。
- SDL2 构建失败：检查窗口系统开发头文件，并从匹配架构的本机工具终端构建；源码下载失败时按模拟器说明提供本地依赖。这是桌面构建问题。
- Hub 没启动：`doctor` 输出 `hub_http: degraded` 与 `Hub not reachable; start nuka hub and check hub.port`；端口冲突输出 `port: error` 与 `Port occupied by another process`。
- USB 设备未枚举：`usb: disabled` 与 `No nuka USB CDC device; check USB cable and native USB mode`，先检查数据线、下载模式和占用者。
