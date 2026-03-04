#!/usr/bin/env bash
set -euo pipefail

DOCS_OUT="${1:-${DOCS_OUT:-build/build/html}}"
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

# Reuse shared helpers so CI no longer needs dedicated UV setup steps.
"${SCRIPT_DIR}/lib/uv-venv-create.sh"
"${SCRIPT_DIR}/lib/uv-install-requirements.sh"

. ".venv/bin/activate"

cp "${DOCS_OUT}"/*.svg ./docs/source/_static
cd docs/source
python graphviz_generator.py
cd ..
make html
