# SPDX-License-Identifier: AGPL-3.0-or-later
FROM python:3.12-slim
COPY --from=ghcr.io/astral-sh/uv:0.9.28 /uv /uvx /bin/
WORKDIR /app
COPY . /app
RUN uv sync --locked --no-dev --extra images --extra media-linux
ENV PATH="/app/.venv/bin:$PATH" NUKA_CONFIG=/config/config.toml NUKA__HUB__DATA_DIR='"/data"'
EXPOSE 8790
VOLUME ["/config", "/data"]
ENTRYPOINT ["nuka"]
CMD ["hub"]
