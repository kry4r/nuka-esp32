# 第三方依赖与许可证

仓库原创内容采用 AGPL-3.0-or-later。下表只列当前已引入的工具和依赖；FW / BE 引入实际组件、字体或 Web vendor 时须在同一 PR 中补充，不能仅凭 SPEC 中的候选清单视为已审核。

| 依赖 | 版本 / 锁定位置 | 许可证 | 用途 / 上游 |
|---|---|---|---|
| jsonschema | 4.26.0，`scripts/uv.lock` | MIT | T6，[上游](https://github.com/python-jsonschema/jsonschema) |
| attrs | 26.1.0，`scripts/uv.lock` | MIT | jsonschema 依赖，[上游](https://github.com/python-attrs/attrs) |
| jsonschema-specifications | 2025.9.1，`scripts/uv.lock` | MIT | 标准 metaschema，[上游](https://github.com/python-jsonschema/jsonschema-specifications) |
| referencing | 0.37.0，`scripts/uv.lock` | MIT | 本地 schema 引用，[上游](https://github.com/python-jsonschema/referencing) |
| rpds-py | 2026.9.1，`scripts/uv.lock` | MIT | referencing 依赖，[上游](https://github.com/crate-py/rpds) |
| typing-extensions | 4.16.0，`scripts/uv.lock` | PSF-2.0 | Python 类型支持，[上游](https://github.com/python/typing_extensions)；PSF 为宽松、GPL 兼容许可证 |
| rfc3339-validator | 0.1.4，`scripts/uv.lock` | MIT | 使 T6 实际验证 date-time 格式，[上游](https://github.com/naimetti/rfc3339-validator) |
| rfc3986-validator | 0.1.1，`scripts/uv.lock` | MIT | 使 T6 实际验证 URI 格式，[上游](https://github.com/naimetti/rfc3986-validator) |
| six | 1.17.0，`scripts/uv.lock` | MIT | rfc3339-validator 依赖，[上游](https://github.com/benjaminp/six) |
| uv / setup-uv | CI action v6 | MIT / Apache-2.0；action MIT | 环境与锁文件，[uv](https://github.com/astral-sh/uv)、[action](https://github.com/astral-sh/setup-uv) |
| actions/checkout | v4 | MIT | [CI checkout](https://github.com/actions/checkout) |
| actions/setup-python | v5 | MIT | [CI Python](https://github.com/actions/setup-python) |
| ESP-IDF Docker image | release-v5.5；release-v6.0 实验性编译 | ESP-IDF 为 Apache-2.0；随附工具各保留原许可证 | [官方镜像文档](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-guides/tools/idf-docker-image.html)，仅构建环境，尚未引入固件组件 |

本地验收还使用 Ruff 0.17.1（MIT，[上游](https://github.com/astral-sh/ruff)）及 actionlint 1.7.12（MIT，[上游](https://github.com/rhysd/actionlint)）；它们不作为产品依赖分发。T6 只安装两个格式检查插件，未启用会引入更多包的完整 format extras。

AGPL-3.0 许可证全文取自 [SPDX 的标准许可证文本](https://raw.githubusercontent.com/spdx/license-list-data/main/text/AGPL-3.0-or-later.txt)，保持原文。

字体只用 OFL，美术与图标只用原创或 CC0；vendor 源文件保留上游头部与许可证，不改成项目 SPDX。第三方文件所在 vendor 目录不要求项目 SPDX，但完整上游许可证随文件分发。


## nuka-hub 依赖

运行时、平台可选项和开发工具的准确版本见 `hub/uv.lock`。以下清单含传递依赖，许可证来自对应发行版的上游 PyPI 元数据；尚未在当前平台安装的可选项也列出。MIT-CMU、MIT-0 与 PSF-2.0 属于宽松许可证；MPL / LGPL 包保持其上游许可与替换义务。未复制第三方源码。

| 依赖 | 锁定版本 | 许可证 | 上游发行元数据 |
|---|---|---|---|
| annotated-doc | 0.0.5 | MIT | [PyPI metadata](https://pypi.org/project/annotated-doc/0.0.5/) |
| annotated-types | 0.8.0 | MIT | [PyPI metadata](https://pypi.org/project/annotated-types/0.8.0/) |
| anyio | 4.15.1 | MIT | [PyPI metadata](https://pypi.org/project/anyio/4.15.1/) |
| attrs | 26.1.0 | MIT | [PyPI metadata](https://pypi.org/project/attrs/26.1.0/) |
| backports-tarfile | 1.2.0 | MIT | [PyPI metadata](https://pypi.org/project/backports-tarfile/1.2.0/) |
| boolean-py | 5.0 | BSD-2-Clause | [PyPI metadata](https://pypi.org/project/boolean-py/5.0/) |
| cachecontrol | 0.14.4 | Apache-2.0 | [PyPI metadata](https://pypi.org/project/cachecontrol/0.14.4/) |
| certifi | 2026.7.22 | MPL-2.0 | [PyPI metadata](https://pypi.org/project/certifi/2026.7.22/) |
| cffi | 2.1.1 | MIT-0 | [PyPI metadata](https://pypi.org/project/cffi/2.1.1/) |
| charset-normalizer | 3.5.2 | MIT | [PyPI metadata](https://pypi.org/project/charset-normalizer/3.5.2/) |
| click | 8.5.0 | BSD-3-Clause | [PyPI metadata](https://pypi.org/project/click/8.5.0/) |
| colorama | 0.4.6 | BSD-3-Clause | [PyPI metadata](https://pypi.org/project/colorama/0.4.6/) |
| cryptography | 50.0.2 | Apache-2.0 OR BSD-3-Clause | [PyPI metadata](https://pypi.org/project/cryptography/50.0.2/) |
| cyclonedx-python-lib | 11.12.0 | Apache-2.0 | [PyPI metadata](https://pypi.org/project/cyclonedx-python-lib/11.12.0/) |
| dbus-next | 0.2.3 | MIT | [PyPI metadata](https://pypi.org/project/dbus-next/0.2.3/) |
| defusedxml | 0.7.1 | PSF-2.0 | [PyPI metadata](https://pypi.org/project/defusedxml/0.7.1/) |
| fastapi | 0.143.3 | MIT | [PyPI metadata](https://pypi.org/project/fastapi/0.143.3/) |
| filelock | 4.2.1 | MIT | [PyPI metadata](https://pypi.org/project/filelock/4.2.1/) |
| h11 | 0.16.0 | MIT | [PyPI metadata](https://pypi.org/project/h11/0.16.0/) |
| httpcore | 1.0.9 | BSD-3-Clause | [PyPI metadata](https://pypi.org/project/httpcore/1.0.9/) |
| httpx | 0.28.1 | BSD-3-Clause | [PyPI metadata](https://pypi.org/project/httpx/0.28.1/) |
| idna | 3.20 | BSD-3-Clause | [PyPI metadata](https://pypi.org/project/idna/3.20/) |
| ifaddr | 0.2.0 | MIT | [PyPI metadata](https://pypi.org/project/ifaddr/0.2.0/) |
| importlib-metadata | 9.0.1 | Apache-2.0 | [PyPI metadata](https://pypi.org/project/importlib-metadata/9.0.1/) |
| iniconfig | 2.3.1 | MIT | [PyPI metadata](https://pypi.org/project/iniconfig/2.3.1/) |
| jaraco-classes | 3.4.0 | MIT | [PyPI metadata](https://pypi.org/project/jaraco-classes/3.4.0/) |
| jaraco-context | 6.1.2 | MIT | [PyPI metadata](https://pypi.org/project/jaraco-context/6.1.2/) |
| jaraco-functools | 4.6.0 | MIT | [PyPI metadata](https://pypi.org/project/jaraco-functools/4.6.0/) |
| jeepney | 0.9.0 | MIT | [PyPI metadata](https://pypi.org/project/jeepney/0.9.0/) |
| jsonschema | 4.26.0 | MIT | [PyPI metadata](https://pypi.org/project/jsonschema/4.26.0/) |
| jsonschema-specifications | 2025.9.1 | MIT | [PyPI metadata](https://pypi.org/project/jsonschema-specifications/2025.9.1/) |
| jsonpath-ng | 1.11.0 | Apache-2.0 | [PyPI metadata](https://pypi.org/project/jsonpath-ng/1.11.0/) |
| keyring | 25.7.0 | MIT | [PyPI metadata](https://pypi.org/project/keyring/25.7.0/) |
| license-expression | 30.4.4 | Apache-2.0 | [PyPI metadata](https://pypi.org/project/license-expression/30.4.4/) |
| markdown-it-py | 4.2.0 | MIT | [PyPI metadata](https://pypi.org/project/markdown-it-py/4.2.0/) |
| mdurl | 0.1.2 | MIT | [PyPI metadata](https://pypi.org/project/mdurl/0.1.2/) |
| more-itertools | 11.2.1 | MIT | [PyPI metadata](https://pypi.org/project/more-itertools/11.2.1/) |
| msgpack | 1.2.3 | Apache-2.0 | [PyPI metadata](https://pypi.org/project/msgpack/1.2.3/) |
| nvidia-ml-py | 13.615.71 | BSD-3-Clause | [PyPI metadata](https://pypi.org/project/nvidia-ml-py/13.615.71/) |
| opentelemetry-api | 1.45.1 | Apache-2.0 | [PyPI metadata](https://pypi.org/project/opentelemetry-api/1.45.1/) |
| packageurl-python | 0.17.6 | MIT | [PyPI metadata](https://pypi.org/project/packageurl-python/0.17.6/) |
| packaging | 26.3 | Apache-2.0 OR BSD-2-Clause | [PyPI metadata](https://pypi.org/project/packaging/26.3/) |
| pillow | 13.0.0 | MIT-CMU | [PyPI metadata](https://pypi.org/project/pillow/13.0.0/) |
| pip | 26.2.1 | MIT | [PyPI metadata](https://pypi.org/project/pip/26.2.1/) |
| pip-api | 0.0.35 | Apache-2.0 | [PyPI metadata](https://pypi.org/project/pip-api/0.0.35/) |
| pip-audit | 2.10.1 | Apache-2.0 | [PyPI metadata](https://pypi.org/project/pip-audit/2.10.1/) |
| pip-requirements-parser | 32.0.1 | MIT | [PyPI metadata](https://pypi.org/project/pip-requirements-parser/32.0.1/) |
| platformdirs | 4.13.4 | MIT | [PyPI metadata](https://pypi.org/project/platformdirs/4.13.4/) |
| pluggy | 1.7.0 | MIT | [PyPI metadata](https://pypi.org/project/pluggy/1.7.0/) |
| psutil | 7.2.2 | BSD-3-Clause | [PyPI metadata](https://pypi.org/project/psutil/7.2.2/) |
| prometheus-client | 0.26.0 | Apache-2.0 AND BSD-2-Clause | [PyPI metadata](https://pypi.org/project/prometheus-client/0.26.0/) |
| py-serializable | 2.1.0 | Apache-2.0 | [PyPI metadata](https://pypi.org/project/py-serializable/2.1.0/) |
| pycparser | 3.11 | BSD-3-Clause | [PyPI metadata](https://pypi.org/project/pycparser/3.11/) |
| pydantic | 2.14.1 | MIT | [PyPI metadata](https://pypi.org/project/pydantic/2.14.1/) |
| pydantic-core | 2.50.1 | MIT | [PyPI metadata](https://pypi.org/project/pydantic-core/2.50.1/) |
| pygments | 2.21.0 | BSD-2-Clause | [PyPI metadata](https://pypi.org/project/pygments/2.21.0/) |
| pyjwt | 2.15.1 | MIT | [PyPI metadata](https://pypi.org/project/pyjwt/2.15.1/) |
| pyparsing | 3.3.3 | MIT | [PyPI metadata](https://pypi.org/project/pyparsing/3.3.3/) |
| pyserial | 3.5 | BSD-3-Clause | [PyPI metadata](https://pypi.org/project/pyserial/3.5/) |
| pytest | 9.1.1 | MIT | [PyPI metadata](https://pypi.org/project/pytest/9.1.1/) |
| pytest-asyncio | 1.4.0 | Apache-2.0 | [PyPI metadata](https://pypi.org/project/pytest-asyncio/1.4.0/) |
| pywin32-ctypes | 0.2.3 | BSD-3-Clause | [PyPI metadata](https://pypi.org/project/pywin32-ctypes/0.2.3/) |
| referencing | 0.37.0 | MIT | [PyPI metadata](https://pypi.org/project/referencing/0.37.0/) |
| requests | 2.34.2 | Apache-2.0 | [PyPI metadata](https://pypi.org/project/requests/2.34.2/) |
| respx | 0.23.1 | BSD-3-Clause | [PyPI metadata](https://pypi.org/project/respx/0.23.1/) |
| rfc3339-validator | 0.1.4 | MIT | [PyPI metadata](https://pypi.org/project/rfc3339-validator/0.1.4/) |
| rfc3986-validator | 0.1.1 | MIT | [PyPI metadata](https://pypi.org/project/rfc3986-validator/0.1.1/) |
| rich | 15.0.0 | MIT | [PyPI metadata](https://pypi.org/project/rich/15.0.0/) |
| rpds-py | 2026.9.1 | MIT | [PyPI metadata](https://pypi.org/project/rpds-py/2026.9.1/) |
| ruff | 0.17.1 | MIT | [PyPI metadata](https://pypi.org/project/ruff/0.17.1/) |
| secretstorage | 3.5.0 | BSD-3-Clause | [PyPI metadata](https://pypi.org/project/secretstorage/3.5.0/) |
| six | 1.17.0 | MIT | [PyPI metadata](https://pypi.org/project/six/1.17.0/) |
| sortedcontainers | 2.4.0 | Apache-2.0 | [PyPI metadata](https://pypi.org/project/sortedcontainers/2.4.0/) |
| starlette | 1.8.0 | BSD-3-Clause | [PyPI metadata](https://pypi.org/project/starlette/1.8.0/) |
| tomli | 2.5.0 | MIT | [PyPI metadata](https://pypi.org/project/tomli/2.5.0/) |
| tomli-w | 1.2.0 | MIT | [PyPI metadata](https://pypi.org/project/tomli-w/1.2.0/) |
| typing-extensions | 4.16.0 | PSF-2.0 | [PyPI metadata](https://pypi.org/project/typing-extensions/4.16.0/) |
| typing-inspection | 0.4.4 | MIT | [PyPI metadata](https://pypi.org/project/typing-inspection/0.4.4/) |
| tzdata | 2026.5 | Apache-2.0 | [PyPI metadata](https://pypi.org/project/tzdata/2026.5/) |
| tzlocal | 5.4.4 | MIT | [PyPI metadata](https://pypi.org/project/tzlocal/5.4.4/) |
| urllib3 | 2.8.0 | MIT | [PyPI metadata](https://pypi.org/project/urllib3/2.8.0/) |
| uvicorn | 0.54.0 | BSD-3-Clause | [PyPI metadata](https://pypi.org/project/uvicorn/0.54.0/) |
| websockets | 17.2 | BSD-3-Clause | [PyPI metadata](https://pypi.org/project/websockets/17.2/) |
| winrt-runtime | 3.2.1 | MIT | [PyPI metadata](https://pypi.org/project/winrt-runtime/3.2.1/) |
| winrt-windows-foundation | 3.2.1 | MIT | [PyPI metadata](https://pypi.org/project/winrt-windows-foundation/3.2.1/) |
| winrt-windows-foundation-collections | 3.2.1 | MIT | [PyPI metadata](https://pypi.org/project/winrt-windows-foundation-collections/3.2.1/) |
| winrt-windows-media-control | 3.2.1 | MIT | [PyPI metadata](https://pypi.org/project/winrt-windows-media-control/3.2.1/) |
| winrt-windows-storage-streams | 3.2.1 | MIT | [PyPI metadata](https://pypi.org/project/winrt-windows-storage-streams/3.2.1/) |
| zeroconf | 0.151.5 | LGPL-2.1-or-later | [PyPI metadata](https://pypi.org/project/zeroconf/0.151.5/) |
| zipp | 4.1.1 | MIT | [PyPI metadata](https://pypi.org/project/zipp/4.1.1/) |

构建后端 hatchling 采用 MIT（[上游](https://github.com/pypa/hatch)）；仅用于构建 Python 包。Pillow 当前锁定版的许可证表达式为 MIT-CMU，以发行版元数据为准。

macOS 媒体采集通过独立安装的 [nowplaying-cli](https://github.com/kirtan-shah/nowplaying-cli) 子进程交互；该工具采用 [GPL-3.0](https://github.com/kirtan-shah/nowplaying-cli/blob/main/LICENSE)，未复制或随 Hub wheel 分发，其版本由安装环境的 Homebrew 记录。


## 固件字体与网页刷机

| 依赖 | 版本 / 锁定位置 | 许可证 | 用途 |
|---|---|---|---|
| FontTools | 4.62.1，firmware/tools/requirements.txt | MIT | 变量字体实例化与等宽数字 |
| Inter / JetBrains Mono / Noto Sans SC | firmware/assets_src/fonts，Google Fonts 原始 TTF | SIL OFL-1.1 | 字体；每份完整 OFL 随源文件与资源包分发 |
| JetBrains Mono Nerd Font Latin 子集 | Nerd Fonts v3.4.0，源 blob `de2d3b3abe5234284849110eb2d555ce0e028b0c` | SIL OFL-1.1 | 仅 U+0020–007E 拉丁轮廓及其字体布局闭包，不含 Nerd 图标；完整 OFL 随字体分发 |
| 通用规范汉字表机器可读转写 | firmware/assets_src/common-3500.txt | MIT；原规范字表属事实数据 | 一级常用 3500 字，完整转写许可证附于 assets_src |
| lv_font_conv | 1.5.3，firmware/tools/package-lock.json | MIT | 字体构建 |
| argparse | 2.0.1，firmware/tools/package-lock.json | Python-2.0 | 字体构建 |
| bit-buffer | 0.2.5，firmware/tools/package-lock.json | MIT | 字体构建 |
| debug | 4.3.4，firmware/tools/package-lock.json | MIT | 字体构建 |
| make-error | 1.3.6，firmware/tools/package-lock.json | ISC | 字体构建 |
| mkdirp | 1.0.4，firmware/tools/package-lock.json | MIT | 字体构建 |
| ms | 2.1.2，firmware/tools/package-lock.json | MIT | 字体构建 |
| opentype.js | 1.3.4，firmware/tools/package-lock.json | MIT | 字体构建 |
| pngjs | 6.0.0，firmware/tools/package-lock.json | MIT | 字体构建 |
| string.prototype.codepointat | 0.2.1，firmware/tools/package-lock.json | MIT | 字体构建 |
| tiny-inflate | 1.0.3，firmware/tools/package-lock.json | MIT | 字体构建 |
| @lit-labs/ssr-dom-shim | 1.6.0，site/package-lock.json | BSD-3-Clause | 网页刷机 / 构建 |
| @lit/context | 1.1.6，site/package-lock.json | BSD-3-Clause | 网页刷机 / 构建 |
| @lit/reactive-element | 2.1.2，site/package-lock.json | BSD-3-Clause | 网页刷机 / 构建 |
| @material/web | 2.5.0，site/package-lock.json | Apache-2.0 | 网页刷机 / 构建 |
| @types/trusted-types | 2.0.7，site/package-lock.json | MIT | 网页刷机 / 构建 |
| atob-lite | 2.0.0，site/package-lock.json | MIT | 网页刷机 / 构建 |
| esbuild | 0.28.2，site/package-lock.json | MIT | 网页刷机 / 构建 |
| esp-web-tools | 10.4.0，site/package-lock.json | Apache-2.0 | 网页刷机 / 构建 |
| esptool-js | 0.6.1，site/package-lock.json | Apache-2.0 | 网页刷机 / 构建 |
| improv-wifi-serial-sdk | 2.8.0，site/package-lock.json | Apache-2.0 | 网页刷机 / 构建 |
| lit | 3.3.3，site/package-lock.json | BSD-3-Clause | 网页刷机 / 构建 |
| lit-element | 4.2.2，site/package-lock.json | BSD-3-Clause | 网页刷机 / 构建 |
| lit-html | 3.3.3，site/package-lock.json | BSD-3-Clause | 网页刷机 / 构建 |
| pako | 2.2.0，site/package-lock.json | (MIT AND Zlib) | 网页刷机 / 构建 |
| tslib | 2.8.1，site/package-lock.json | 0BSD | 网页刷机 / 构建 |

ESP Web Tools 的本地分发副本固定关闭整片 Flash 擦除；构建补丁见 site/vendor.mjs。网页第三方完整许可证位于 site/vendor/licenses/。pako 的 Zlib、tslib 的 0BSD 与 FontTools 的 MIT 均为宽松、GPL 兼容许可。

## 固件与 SDL 模拟器

组件准确版本与哈希见 `firmware/dependencies.lock`。受配置条件控制但未启用的图像 / 字体后端不作为已使用组件列入。SDK 与 managed components 保留各自完整许可证；原创驱动没有复制 BSP 或示例代码。

| 依赖 | 版本 | 许可证 | 来源 / 用途 |
|---|---|---|---|
| ESP-IDF | 5.5.6 | 主体 Apache-2.0；包含的第三方组件各保留原许可证 | [官方 SDK](https://github.com/espressif/esp-idf/tree/v5.5.6)，系统、网络与升级 |
| LVGL | 9.6.0 | MIT | [上游](https://github.com/lvgl/lvgl)，UI |
| espressif/cmake_utilities | 0.5.3 | Apache-2.0 | [组件](https://components.espressif.com/components/espressif/cmake_utilities)，构建 |
| espressif/esp_jpeg | 1.3.1 | Apache-2.0；内含 TJpgDec 为 BSD 类许可证，保留上游文本 | [组件](https://components.espressif.com/components/espressif/esp_jpeg)，JPEG 解码 |
| espressif/esp_lcd_co5300 | 2.2.0 | Apache-2.0 | [组件](https://components.espressif.com/components/espressif/esp_lcd_co5300)，显示接口 |
| espressif/esp_lcd_touch | 1.2.1 | Apache-2.0 | [组件](https://components.espressif.com/components/espressif/esp_lcd_touch)，触摸接口依赖 |
| waveshare/esp_lcd_touch_cst9217 | 2.0.0 | Apache-2.0 | [组件](https://components.espressif.com/components/waveshare/esp_lcd_touch_cst9217)，硬件参考依赖 |
| espressif/esp_tinyusb | 2.4.0 | Apache-2.0 | [组件](https://components.espressif.com/components/espressif/esp_tinyusb)，USB 集成 |
| TinyUSB | 0.21.0~2 | MIT | [组件](https://components.espressif.com/components/espressif/tinyusb)，CDC / HID |
| espressif/esp_websocket_client | 1.8.0 | Apache-2.0 | [组件](https://components.espressif.com/components/espressif/esp_websocket_client)，Hub WS |
| espressif/mdns | 1.14.0 | Apache-2.0 | [组件](https://components.espressif.com/components/espressif/mdns)，Hub 发现 |
| esp_littlefs / littlefs | 1.23.0 / 随组件锁定 | MIT / BSD-3-Clause | [组件](https://components.espressif.com/components/joltwallet/littlefs)，日志存储 |
| cJSON | SDK 随附；模拟器 1.7.19 | MIT | [上游](https://github.com/DaveGamble/cJSON)，JSON |
| Mbed TLS | SDK 随附；模拟器 3.6.7 | 选择 Apache-2.0 | [上游](https://github.com/Mbed-TLS/mbedtls)，Vault 与 TLS |
| jsmn | `firmware/components/vault/vendor/jsmn.h` | MIT | [上游](https://github.com/zserge/jsmn)，Vault 内部 RAM JSON 解析；完整 LICENSE 随文件分发 |
| SDL | 2.32.10 | Zlib | [上游](https://github.com/libsdl-org/SDL)，桌面模拟器 |

Unity 的 MIT 许可证由 ESP-IDF 测试组件保留，只用于 T7 / T8 验证。AGPL 源码发布时应保留 SDK、构建锁文件与第三方声明，使接收者能取得相同依赖。

发布工作流还使用 actions/setup-node v4、upload-artifact / download-artifact v4、configure-pages v5、upload-pages-artifact v3、deploy-pages v4（均为 MIT），以及 docker/setup-qemu-action v3、setup-buildx-action v3、login-action v3、build-push-action v6（均为 Apache-2.0）。它们仅用于 CI，不作为设备或 Hub 运行时分发。


## 第二阶段音频与 Pi 助手依赖

音频原型新增组件由 ESP-IDF 组件管理器锁定。Pi 仅为可选助手运行时；精确 npm 传递依赖和完整性哈希见 `hub/src/nuka_hub/assistant/pi/package-lock.json`。未引入编码代理、LangChain、LangGraph 或向量数据库。npm lifecycle scripts 不运行。

| 依赖 | 锁定版本 | 许可证 | 来源 |
|---|---|---|---|
| espressif/esp_codec_dev | 1.5.9 | Apache-2.0 | [官方组件](https://components.espressif.com/components/espressif/esp_codec_dev/versions/1.5.9) |
| 78/esp-opus | 1.0.5 | 包装 MIT；libopus BSD-3-Clause | [上游](https://github.com/78/esp-opus)；用于 PCM / Opus spike |
| actions/setup-node | v4 | MIT | [上游](https://github.com/actions/setup-node)；CI 中安装可选 Pi 运行时以执行 T3 |
| tomlkit | 0.15.2 | MIT | [上游](https://github.com/python-poetry/tomlkit)；保留配置注释的 TOML 编辑 |

服务器部署模板另需外部 Node.js、GNOME Keyring、D-Bus 与 systemd；它们由服务器包管理器或官方发行包安装，不打包进 Hub。Node.js 主许可证为 MIT，内含依赖保留官方发行包的完整授权；GNOME Keyring 为 LGPL-2.1-or-later，D-Bus 为 AFL-2.1 或 GPL-2.0-or-later 双重许可，systemd 为 LGPL-2.1-or-later，均作为独立系统程序运行。

Pi npm 依赖许可证按已锁定发行版元数据逐项列出。0BSD 为宽松 BSD 变体；Unlicense 为公有领域奉献及宽松授权，均可与项目 AGPL 一同使用；保留各依赖原许可证。

| npm 包 | 版本 | 许可证 |
|---|---|---|
| @anthropic-ai/sdk | 0.129.0 | MIT |
| @aws-sdk/client-bedrock-runtime | 3.1127.0 | Apache-2.0 |
| @aws-sdk/core | 3.978.5 | Apache-2.0 |
| @aws-sdk/credential-provider-env | 3.972.76 | Apache-2.0 |
| @aws-sdk/credential-provider-http | 3.972.78 | Apache-2.0 |
| @aws-sdk/credential-provider-ini | 3.973.21 | Apache-2.0 |
| @aws-sdk/credential-provider-login | 3.972.83 | Apache-2.0 |
| @aws-sdk/credential-provider-node | 3.972.88 | Apache-2.0 |
| @aws-sdk/credential-provider-process | 3.972.76 | Apache-2.0 |
| @aws-sdk/credential-provider-sso | 3.973.20 | Apache-2.0 |
| @aws-sdk/credential-provider-sso/node_modules/@aws-sdk/token-providers | 3.1155.0 | Apache-2.0 |
| @aws-sdk/credential-provider-web-identity | 3.972.82 | Apache-2.0 |
| @aws-sdk/eventstream-handler-node | 3.972.38 | Apache-2.0 |
| @aws-sdk/middleware-eventstream | 3.972.33 | Apache-2.0 |
| @aws-sdk/middleware-websocket | 3.972.58 | Apache-2.0 |
| @aws-sdk/nested-clients | 3.997.50 | Apache-2.0 |
| @aws-sdk/signature-v4-multi-region | 3.996.49 | Apache-2.0 |
| @aws-sdk/token-providers | 3.1127.0 | Apache-2.0 |
| @aws-sdk/types | 3.975.0 | Apache-2.0 |
| @aws-sdk/xml-builder | 3.972.41 | Apache-2.0 |
| @aws/lambda-invoke-store | 0.3.0 | Apache-2.0 |
| @babel/runtime | 7.29.10 | MIT |
| @earendil-works/pi-agent-core | 1.4.0 | MIT |
| @earendil-works/pi-ai | 1.4.0 | MIT |
| @earendil-works/pi-telemetry | 1.4.0 | MIT |
| @google/genai | 2.21.0 | Apache-2.0 |
| @protobufjs/aspromise | 1.1.2 | BSD-3-Clause |
| @protobufjs/base64 | 1.1.2 | BSD-3-Clause |
| @protobufjs/codegen | 2.0.5 | BSD-3-Clause |
| @protobufjs/eventemitter | 1.1.1 | BSD-3-Clause |
| @protobufjs/fetch | 1.1.1 | BSD-3-Clause |
| @protobufjs/float | 1.0.2 | BSD-3-Clause |
| @protobufjs/path | 1.1.2 | BSD-3-Clause |
| @protobufjs/pool | 1.1.0 | BSD-3-Clause |
| @protobufjs/utf8 | 1.1.2 | BSD-3-Clause |
| @smithy/core | 3.36.2 | Apache-2.0 |
| @smithy/credential-provider-imds | 4.5.2 | Apache-2.0 |
| @smithy/fetch-http-handler | 5.8.0 | Apache-2.0 |
| @smithy/node-http-handler | 4.12.1 | Apache-2.0 |
| @smithy/signature-v4 | 5.7.4 | Apache-2.0 |
| @smithy/types | 4.19.0 | Apache-2.0 |
| @stablelib/base64 | 1.0.1 | MIT |
| @types/node | 26.6.5 | MIT |
| @types/retry | 0.12.0 | MIT |
| agent-base | 9.0.0 | MIT |
| base64-js | 1.5.1 | MIT |
| bignumber.js | 9.3.1 | MIT |
| bowser | 2.14.1 | MIT |
| buffer-equal-constant-time | 1.0.1 | BSD-3-Clause |
| data-uri-to-buffer | 4.0.1 | MIT |
| debug | 4.4.3 | MIT |
| ecdsa-sig-formatter | 1.0.11 | Apache-2.0 |
| extend | 3.0.2 | MIT |
| fast-sha256 | 1.3.0 | Unlicense |
| fetch-blob | 3.2.0 | MIT |
| formdata-polyfill | 4.0.10 | MIT |
| gaxios | 7.3.1 | Apache-2.0 |
| gaxios/node_modules/agent-base | 7.1.4 | MIT |
| gaxios/node_modules/https-proxy-agent | 7.0.6 | MIT |
| gcp-metadata | 8.1.2 | Apache-2.0 |
| google-auth-library | 10.9.1 | Apache-2.0 |
| google-logging-utils | 1.1.3 | Apache-2.0 |
| http-proxy-agent | 9.1.0 | MIT |
| https-proxy-agent | 9.1.0 | MIT |
| json-bigint | 1.0.0 | MIT |
| json-schema-to-ts | 3.1.1 | MIT |
| jwa | 2.0.1 | MIT |
| jws | 4.0.1 | MIT |
| long | 5.3.2 | Apache-2.0 |
| ms | 2.1.3 | MIT |
| node-domexception | 1.0.0 | MIT |
| node-fetch | 3.3.2 | MIT |
| openai | 7.19.0 | Apache-2.0 |
| p-retry | 4.6.2 | MIT |
| partial-json | 0.1.7 | MIT |
| protobufjs | 7.6.6 | BSD-3-Clause |
| proxy-agent-negotiate | 1.1.0 | MIT |
| retry | 0.13.1 | MIT |
| safe-buffer | 5.2.1 | MIT |
| standardwebhooks | 1.1.1 | MIT |
| ts-algebra | 2.0.0 | MIT |
| tslib | 2.8.1 | 0BSD |
| typebox | 1.3.27 | MIT |
| undici-types | 8.9.0 | MIT |
| web-streams-polyfill | 3.3.3 | MIT |
| ws | 8.22.0 | MIT |
