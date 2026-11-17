# SPDX-License-Identifier: AGPL-3.0-or-later
from datetime import tzinfo
from typing import Any

import httpx

from .cpa import CPA
from .cpamp import CPAMP


def build_providers(
    config: Any, secrets: Any, client: httpx.AsyncClient, timezone: tzinfo
) -> list[CPA]:
    kinds = {"cpa": CPA, "cpamp": CPAMP}
    return [
        kinds[provider.type](provider, config, secrets, client, timezone)
        for provider in config.provider
    ]
