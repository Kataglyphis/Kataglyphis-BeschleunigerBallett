#!/usr/bin/env bash
set -euo pipefail

DOCS_OUT="${DOCS_OUT:-build/build/html}"

cp ${DOCS_OUT}/*.svg ./docs/source/_static
cd docs/source
uv run python graphviz_generator.py
cd ..
uv run make html
