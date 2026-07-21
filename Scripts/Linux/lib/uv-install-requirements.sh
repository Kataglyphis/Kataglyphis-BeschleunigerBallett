#!/usr/bin/env bash
set -euo pipefail

. ".venv/bin/activate"
# Pin the install target to the local .venv. The CI container image exports
# UV_PYTHON=/opt/venv/bin/python (its root-owned system venv), and uv honours
# UV_PYTHON OVER the activated .venv - so a plain `uv pip install` targets
# /opt/venv and dies with "Permission denied (os error 13)" as the CI user
# (uid 1001). --python forces the writable local environment. Reproduced and
# verified in the :latest-cross image; same class of fix as the CARGO_HOME
# fallback in cmake-configure-build.sh.
uv pip install --python .venv/bin/python -r requirements.txt
