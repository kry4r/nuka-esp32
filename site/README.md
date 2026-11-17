# 网页刷机页（FW-29）

静态页面、原生 ES module 和本地 vendored ESP Web Tools 10.4.0；运行时不用 CDN。
先确认完整 16 MiB 备份，再连接设备、刷入、关闭串口窗口并运行 `nuka pair`。
本地预览：`python -m http.server 8080 --directory site`，打开
`http://localhost:8080`。Web Serial 需要桌面 Chrome / Edge 与安全上下文。

`manifest.json` 从 GitHub Release 读取五个独立镜像，保留 NVS / Vault 分区；
owner 尚未确定，源码中统一为 `<owner>`。Pages workflow 在发布时使用运行环境的
repository / tag 生成实际链接。新安装只写 assets bank 0，后续使用 OTA 更新。

**无整片擦除补丁**：ESP Web Tools 对无 Improv 的新设备默认整片擦除。
`vendor.mjs` 针对锁定的上游版本，在打包阶段把安装路径的 `_installErase` 固定为
`false`；manifest 关闭 Improv 等待和擦除提示。改动只作用于分发副本，原包仍由
`package-lock.json` 锁定和校验。源码未改用项目许可，上游 Apache-2.0 许可证保留在
`vendor/licenses/esp-web-tools/`。禁止移除这项补丁。

重建 vendor：`npm ci --prefix site --ignore-scripts`，然后
`npm run vendor --prefix site`。`vendor/DEPENDENCIES.txt` 和 `vendor/licenses/`
列出全部依赖及完整许可证；没有源码 CDN 或远程 JS。网页真机刷写验收由 FW 持有
串口时进行；只打开页面和验证 JS 不等于已经完成刷机。
