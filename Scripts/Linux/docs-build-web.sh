#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib/common.sh
source "${SCRIPT_DIR}/lib/common.sh"

DOCS_OUT="${1:-${DOCS_OUT:-build/build/html}}"

info "Ensuring Python virtual environment and dependencies"
"${SCRIPT_DIR}/lib/uv-venv-create.sh"
"${SCRIPT_DIR}/lib/uv-install-requirements.sh"

info "Activating virtual environment"
# shellcheck disable=SC1091
source ".venv/bin/activate"

info "Copying SVG files to docs static directory"
cp "${DOCS_OUT}"/*.svg ./docs/source/_static

info "Generating Graphviz diagrams"
cd docs/source
python graphviz_generator.py
cd ..

info "Building HTML documentation"
make html

info "Documentation build completed successfully"