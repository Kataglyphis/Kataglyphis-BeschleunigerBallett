#!/usr/bin/env bash
set -euo pipefail

. ".venv/bin/activate"
uv pip install -r requirements.txt
