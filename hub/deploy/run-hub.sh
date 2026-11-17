#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
set -eu
gnome-keyring-daemon --unlock --components=secrets < "$CREDENTIALS_DIRECTORY/keyring-password" >/dev/null
exec /opt/nuka/venv/bin/nuka hub --config /etc/nuka/config.toml
