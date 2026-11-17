# SPDX-License-Identifier: AGPL-3.0-or-later

"""Reject deployment values without displaying the values being rejected."""

import io
import re
import subprocess
import tokenize
from ipaddress import IPv4Address, IPv4Network
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DOC_NETS = tuple(
    IPv4Network(net)
    for net in (
        "192.0.2.0/24",
        "198.51.100.0/24",
        "203.0.113.0/24",
    )
)
DEFAULT_DOCS = {
    "docs/CONFIG.md",
    "docs/SPEC.md",
    "docs/PROTOCOL.md",
    "hub/src/nuka_hub/core/config.py",
}
OFFICIAL_DOMAINS = {
    "github.com",
    "githubusercontent.com",
    "ghcr.io",
    "gnu.org",
    "fsf.org",
    "spdx.org",
    "opensource.org",
    "espressif.com",
    "lvgl.io",
    "python.org",
    "pypi.org",
    "pythonhosted.org",
    "astral.sh",
    "json-schema.org",
    "readthedocs.io",
    "wmo.int",
    "open-meteo.com",
    "waveshare.com",
    "cloudflare.com",
    "cloudflareaccess.com",
    "tailscale.com",
    "ntp.org",
    "rfc-editor.org",
    "ietf.org",
    "unicode.org",
    "fonts.google.com",
    "jetbrains.com",
    "rsms.me",
    "apache.org",
    "mozilla.org",
    "chatgpt.com",
    "anthropic.com",
    "googleapis.com",
    "npmjs.org",
    "sil.org",
    "adobe.com",
    "geminicli.com",
    "claude.com",
    "esphome.github.io",
    "home-assistant.io",
    "silabs.com",
    "wch.cn",
    "w3.org",
    "microsoft.com",
    "docker.com",
    "freedesktop.org",
    "nodejs.org",
    "pi.dev",
    "letta.com",
    "mem0.ai",
    "ffmpeg.org",
    "developers.deepgram.com",
    "docs.searxng.org",
    "api-dashboard.search.brave.com",
}
IPV4 = re.compile(r"(?<![\w.])(?:\d{1,3}\.){3}\d{1,3}(?![\w.])")
URL_HOST = re.compile(
    r"(?:https?|wss?)://(?:[^\s/@]+@)?([a-z0-9][a-z0-9.-]*)", re.IGNORECASE
)
BARE_DOMAIN = re.compile(
    r"(?<![\w.-])(?:[a-z0-9](?:[a-z0-9-]*[a-z0-9])?\.)+"
    r"(?:com|net|org|edu|gov|mil|int|io|ai|app|dev|co|me|xyz|info|biz|"
    r"tech|cloud|online|site|lan|local|[a-z]{2})(?![\w.-])",
    re.IGNORECASE,
)
FILE_SUFFIXES = {"py", "js", "ts", "md", "sh", "ps", "cc", "rs", "go", "db", "gz"}
NON_DOMAINS = {
    "ai.status.ok",
    "astral-sh.uv",
    "config.ai",
    "clocks.current.graphics",
    "cards.up",
    "excluded.fw",
    "app.ai",
    "pair.ap",
    "metric.online",
    "vm.ai",
    "diag.info",
    "pair.ok",
    "color.bg",
    "color.info",
    "space.xs",
    "system.net",
    "server.online",
    "tool.uv",
    "matrix.os",
    "ota.result.ok",
    "cdc.ok",
}
PERSONAL = (
    (
        "serial port",
        re.compile(r"\bCOM\d+\b|/dev/(?:tty(?:USB|ACM)\d+|cu\.[\w.-]+)", re.IGNORECASE),
    ),
    (
        "user home path",
        re.compile(
            r"(?i:[A-Z]:[\\/]Users[\\/])[\w.-]+|(?<![\w/])/(?:home|Users)/[\w.-]+"
        ),
    ),
    (
        "credential",
        re.compile(
            r"\b(?:gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,}|sk-[A-Za-z0-9_-]{20,})\b"
        ),
    ),
    ("private key", re.compile(r"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----")),
)


def official(host: str) -> bool:
    host = host.lower().rstrip(".")
    return host == "localhost" or any(
        host == suffix or host.endswith("." + suffix)
        for suffix in (
            "example.com",
            "example.lan",
            *OFFICIAL_DOMAINS,
        )
    )


def domain_text(content: str, suffix: str) -> list[str]:
    if suffix not in {".py", ".c", ".h", ".js", ".mjs"}:
        return content.splitlines()
    # Attribute access is code, while deployment domains live in strings/comments.
    rows = [list(" " * len(line)) for line in content.splitlines()]
    tokens = []
    if suffix == ".py":
        for token in tokenize.generate_tokens(io.StringIO(content).readline):
            if token.type in {tokenize.STRING, tokenize.COMMENT}:
                tokens.append((token.start[0] - 1, token.start[1], token.string))
    else:
        pattern = r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|`(?:\\.|[^`\\])*`'
        for match in re.finditer(pattern, content, re.DOTALL):
            before = content[: match.start()]
            tokens.append(
                (before.count("\n"), len(before.rsplit("\n", 1)[-1]), match.group())
            )
    for start_row, start_column, value in tokens:
        if suffix in {".js", ".mjs"} and value.startswith("`"):
            value = re.sub(r"\$\{[^}]*\}", lambda m: " " * len(m.group()), value)
        for row, part in enumerate(value.splitlines(), start_row):
            column = start_column if row == start_row else 0
            rows[row][column : column + len(part)] = part
    return ["".join(row) for row in rows]


def main() -> int:
    paths = (
        subprocess.check_output(
            ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
            cwd=ROOT,
        )
        .decode()
        .split("\0")
    )
    # Only this explicitly authorized local file is read. Never echo its entries.
    blocklist = ROOT / "local/blocklist.txt"
    keywords = []
    if blocklist.is_file():
        keywords = [
            line.strip().casefold()
            for line in blocklist.read_text(
                encoding="utf-8-sig",
            ).splitlines()
            if line.strip() and not line.lstrip().startswith("#")
        ]
    failures = 0
    checked = 0
    for name in sorted(set(paths) - {""}):
        if name.startswith("local/"):
            print("local/: private files must not be tracked")
            failures += 1
            continue
        path = ROOT / name
        if not path.is_file():
            continue
        raw = path.read_bytes()
        if b"\0" in raw:
            continue
        try:
            content = raw.decode("utf-8-sig")
        except UnicodeDecodeError:
            print(f"{name}: text files must use UTF-8")
            failures += 1
            continue
        checked += 1
        defaults = (
            name in DEFAULT_DOCS
            or name.startswith("protocol/examples/")
            or path.name
            in {"config.toml", "config.example.toml", "config.defaults.toml"}
        )
        domains = domain_text(content, path.suffix)
        for line_no, line in enumerate(content.splitlines(), 1):
            reasons = set()
            for match in IPV4.finditer(line):
                address = IPv4Address(match.group())
                if any(address in net for net in DOC_NETS):
                    continue
                if (
                    (defaults and not address.is_global)
                    or address.is_loopback
                    or address.is_unspecified
                ):
                    continue
                reasons.add("IPv4 address outside allowed documentation/defaults")
            for match in URL_HOST.finditer(line):
                host = match.group(1)
                if re.fullmatch(r"[\d.]+", host):
                    continue
                if not official(host):
                    reasons.add("URL host outside official/example allowlist")
            # Bundled upstream JavaScript contains minified property names; URLs and
            # every personal-value/blocklist rule are still scanned there.
            domain_line = (
                "" if name.startswith("site/vendor/") else domains[line_no - 1]
            )
            for match in BARE_DOMAIN.finditer(domain_line):
                host = match.group().lower()
                if host.rsplit(".", 1)[1] in FILE_SUFFIXES or host in NON_DOMAINS:
                    continue
                if not official(host):
                    reasons.add("domain outside official/example allowlist")
            for reason, pattern in PERSONAL:
                if pattern.search(line):
                    reasons.add(reason)
            # Lockfile digests are opaque: a substring collision is not a deployment value.
            digests = re.findall(r"sha256:([a-f0-9]{64})", line, re.IGNORECASE)
            human_text = re.sub(r"sha256:[a-f0-9]{64}", "", line, flags=re.IGNORECASE)
            if any(
                word in human_text.casefold() or word in digests for word in keywords
            ):
                reasons.add("local blocklist match (value redacted)")
            for reason in sorted(reasons):
                print(f"{name}:{line_no}: {reason}")
                failures += 1
    print(f"Personal-value check: {checked} text files, {failures} failures")
    return int(failures > 0)


if __name__ == "__main__":
    raise SystemExit(main())
