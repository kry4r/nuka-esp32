# SPDX-License-Identifier: AGPL-3.0-or-later
from __future__ import annotations

from datetime import UTC, datetime
from typing import Any

import httpx
from pydantic import BaseModel, ConfigDict, Field

from nuka_hub.core.plugin import Health

FORECAST_URL = "https://api.open-meteo.com/v1/forecast"
GEOCODING_URL = "https://geocoding-api.open-meteo.com/v1/search"
ICONS = {
    0: "clear_day",
    1: "clear_day",
    2: "partly_cloudy",
    3: "cloudy",
    45: "fog",
    48: "fog",
    51: "drizzle",
    53: "drizzle",
    55: "drizzle",
    56: "sleet",
    57: "sleet",
    61: "rain",
    63: "rain",
    65: "heavy_rain",
    66: "sleet",
    67: "sleet",
    71: "snow",
    73: "snow",
    75: "snow",
    77: "snow",
    80: "rain",
    81: "rain",
    82: "heavy_rain",
    85: "snow",
    86: "snow",
    95: "thunder",
    96: "thunder",
    99: "thunder",
}


class Upstream(BaseModel):
    model_config = ConfigDict(extra="ignore")


class Location(Upstream):
    name: str
    latitude: float = Field(ge=-90, le=90)
    longitude: float = Field(ge=-180, le=180)


class Geocoding(Upstream):
    results: list[Location]


class Current(Upstream):
    time: str
    temperature_2m: float
    apparent_temperature: float
    relative_humidity_2m: float = Field(ge=0, le=100)
    wind_speed_10m: float = Field(ge=0)
    wind_direction_10m: float = Field(ge=0, le=360)
    weather_code: int
    is_day: int = Field(ge=0, le=1)


class Daily(Upstream):
    time: list[str]
    temperature_2m_max: list[float]
    temperature_2m_min: list[float]
    weather_code: list[int]


class Forecast(Upstream):
    current: Current
    daily: Daily


class Weather:
    name = "weather"
    kind = "weather"
    doc = "07-weather"
    timeout_s = 10.0

    def __init__(self, config: Any, client: httpx.AsyncClient):
        self.config, self.client = config, client
        self.interval_s = config.interval
        self.location = None

    async def probe(self) -> Health:
        return Health(
            self.name,
            self.kind,
            "ok" if self.config.enabled else "disabled",
            "" if self.config.enabled else "Weather is not configured",
            self.doc,
        )

    async def sample(self) -> dict:
        if self.location is None:
            if self.config.city:
                response = await self.client.get(
                    GEOCODING_URL, params={"name": self.config.city, "count": 1}
                )
                response.raise_for_status()
                location = Geocoding.model_validate(response.json()).results[0]
                self.location = location.name, location.latitude, location.longitude
            else:
                self.location = (
                    f"{self.config.lat:.2f}, {self.config.lon:.2f}",
                    self.config.lat,
                    self.config.lon,
                )
        name, latitude, longitude = self.location
        response = await self.client.get(
            FORECAST_URL,
            params={
                "latitude": latitude,
                "longitude": longitude,
                "timezone": "UTC",
                "forecast_days": 4,
                "current": (
                    "temperature_2m,apparent_temperature,relative_humidity_2"
                    "m,weather_code,is_day,wind_speed_10m,wind_direction_10m"
                ),
                "daily": "weather_code,temperature_2m_max,temperature_2m_min",
                "temperature_unit": "celsius",
                "wind_speed_unit": "mph" if self.config.units == "imperial" else "kmh",
            },
        )
        response.raise_for_status()
        forecast = Forecast.model_validate(response.json())
        current, daily = forecast.current, forecast.daily
        icon = ICONS[current.weather_code]
        if current.weather_code in {0, 1} and current.is_day == 0:
            icon = "clear_night"
        direction = ("N", "NE", "E", "SE", "S", "SW", "W", "NW")[
            int((current.wind_direction_10m + 22.5) % 360 // 45)
        ]
        return {
            "loc_txt": name,
            "updated": datetime.fromisoformat(current.time)
            .replace(tzinfo=UTC)
            .isoformat()
            .replace("+00:00", "Z"),
            "now": {
                "temp_c": current.temperature_2m,
                "feels_c": current.apparent_temperature,
                "hum_pct": round(current.relative_humidity_2m, 1),
                "icon": icon,
                "desc_txt": icon.replace("_", " "),
                "wind_txt": (
                    f"{direction} {current.wind_speed_10m:g} "
                    + ("mph" if self.config.units == "imperial" else "km/h")
                ),
            },
            "days": [
                {
                    "date": daily.time[index],
                    "hi_c": daily.temperature_2m_max[index],
                    "lo_c": daily.temperature_2m_min[index],
                    "icon": ICONS[daily.weather_code[index]],
                }
                for index in range(1, 4)
            ],
        }
