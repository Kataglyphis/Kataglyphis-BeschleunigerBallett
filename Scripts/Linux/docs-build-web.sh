#!/usr/bin/env bash
set -euo pipefail

DOCS_OUT="${1:-${DOCS_OUT:-build/build/html}}"

if [[ -f ".venv/bin/activate" ]]; then
	echo "Verwende vorhandene .venv"
else
	echo "Keine .venv gefunden, erstelle virtuelle Umgebung mit uv"
	uv venv .venv
fi

. ".venv/bin/activate"

if ! python -c "import breathe" >/dev/null 2>&1; then
	echo "Python-Abhängigkeiten fehlen, installiere requirements.txt"
	uv pip install -r requirements.txt
fi

cp "${DOCS_OUT}"/*.svg ./docs/source/_static
cd docs/source
python graphviz_generator.py
cd ..
make html
