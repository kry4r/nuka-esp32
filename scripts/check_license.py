# SPDX-License-Identifier: AGPL-3.0-or-later

"""Check SPDX headers in repository-owned source files, including new files."""

import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPDX = "SPDX-License-Identifier: AGPL-3.0-or-later"
SOURCE_SUFFIXES = {
    ".c",
    ".h",
    ".cpp",
    ".hpp",
    ".cc",
    ".py",
    ".js",
    ".mjs",
    ".ts",
    ".css",
    ".html",
    ".sh",
    ".bash",
    ".ps1",
    ".yml",
    ".yaml",
    ".toml",
    ".cmake",
    ".csv",
}
SOURCE_NAMES = {
    "CMakeLists.txt",
    "Makefile",
    "Dockerfile",
    "Kconfig",
    "Kconfig.projbuild",
}


def main() -> int:
    paths = (
        subprocess.check_output(
            ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
            cwd=ROOT,
        )
        .decode()
        .split("\0")
    )
    failures = 0
    checked = 0
    for name in sorted(set(paths) - {""}):
        path = ROOT / name
        if (
            name.startswith(
                (
                    "local/",
                    "site/vendor/",
                    "hub/src/nuka_hub/web/vendor/",
                    "firmware/components/vault/vendor/",
                )
            )
            or not path.is_file()
        ):
            continue
        json_source = (
            name.startswith("protocol/schemas/") or name == "design/theme.json"
        )
        if (
            path.suffix not in SOURCE_SUFFIXES
            and path.name not in SOURCE_NAMES
            and not json_source
        ):
            continue
        checked += 1
        header = "\n".join(path.read_text(encoding="utf-8-sig").splitlines()[:5])
        if SPDX not in header:
            print(f"{name}:1: missing {SPDX}")
            failures += 1
    print(f"SPDX: {checked} source files, {failures} failures")
    return int(failures > 0)


if __name__ == "__main__":
    raise SystemExit(main())
