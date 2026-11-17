# 共用主题

`theme.json` 是 SPEC §6.6.1 的颜色、圆角、间距、安全区及 §6.6.2 字号的唯一来源。运行 `python firmware/tools/gen_theme.py` 同时生成：

- `firmware/components/ui/theme_tokens.h`：`NUKA_COLOR_*` 为 RGB 整数，可交给 `lv_color_hex()`；其他值以 px 为单位。
- `hub/src/nuka_hub/web/theme.css`：`--color-*`、`--radius-*`、`--space-*`、`--font-size-*` 和 `--safe-inset`。

字体文件和位图字体管线由 FW-05 实现。字号下限为 20px；中文仍按 SPEC 使用 22 / 28px，不由本生成器缩放。

生成器检查 `text`、`text_dim` 在 `bg`、`surface` 上的四个 WCAG 相对亮度对比度，分别要求 ≥7:1、≥4.5:1。失败时退出非零，且不写产物。CI 用 `--check` 检查已提交产物是否与 token 一致。
