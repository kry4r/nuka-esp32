# SPDX-License-Identifier: AGPL-3.0-or-later
import pytest

from nuka_hub.core.config import Alerts
from nuka_hub.plugins.alerts import AlertEngine, Expression


@pytest.mark.parametrize(
    "expression,item,expected",
    [
        ("server.online == false", {"online": False}, True),
        ("cpu_pct > 90 && mem_pct >= 50", {"cpu_pct": 91, "mem_pct": 50}, True),
        ("!(online == false) || cpu_pct > 90", {"online": True}, True),
        ("(cpu_pct < 90 || cpu_pct == 90) && online", {"cpu_pct": 90, "online": True}, True),
        ("disk_pct != 90", {}, False),
        ("!disk_pct", {}, False),
        ("disk_pct > 90 || online", {"online": True}, True),
        ("cpu_pct <= 90", {"cpu_pct": 90}, True),
        ("cpu_pct > 90", {"cpu_pct": 89}, False),
    ],
)
def test_t4_expression_table(expression, item, expected):
    assert Expression(expression).matches(item) is expected


@pytest.mark.parametrize(
    "expression", ['__import__("os")', "cpu_pct + 1 > 90", "online &&", "(online"]
)
def test_t4_reject_executable_or_invalid_expressions(expression):
    with pytest.raises(ValueError):
        Expression(expression)


async def test_t4_debounce_recovery_repeat():
    notifications = []

    async def notify(payload):
        notifications.append(payload)
        return "n_example"

    config = Alerts.model_validate(
        {
            "builtin": {
                name: False
                for name in [
                    "server_offline",
                    "cpu_high",
                    "mem_high",
                    "disk_high",
                    "ai_exhausted",
                    "ai_reauth",
                ]
            },
            "repeat": 30,
            "rule": [{"name": "example", "scope": "servers", "when": "cpu_pct > 90", "for": 10}],
        }
    )
    engine = AlertEngine(config, notify)
    hot = [{"id": "s_example", "name": "example", "cpu_pct": 95}]
    cold = [{"id": "s_example", "name": "example", "cpu_pct": 20}]
    await engine.evaluate("servers", hot, 0)
    await engine.evaluate("servers", hot, 9)
    assert not notifications
    await engine.evaluate("servers", hot, 10)
    await engine.evaluate("servers", hot, 39)
    assert len(notifications) == 1
    await engine.evaluate("servers", hot, 40)
    assert len(notifications) == 2
    await engine.evaluate("servers", cold, 41)
    await engine.evaluate("servers", cold, 42)
    assert len(notifications) == 3 and notifications[-1]["level"] == "info"
