#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib/common.sh
source "${SCRIPT_DIR}/lib/common.sh"

DOCS_OUT="${1:-${DOCS_OUT:-build/build/html}}"

# Rebuild the Rust WebGPU renderer to wasm32 + wasm-bindgen and refresh the
# committed demo under docs/source/_webgpu_demo, so the deployed docs always
# show the CURRENT renderer rather than a hand-built snapshot that goes stale.
# Best-effort: a wasm build failure must NOT red the docs deploy - the committed
# snapshot then remains the fallback. The container already ships the wasm32
# target (ContainerHub install-rust.sh); only wasm-bindgen-cli is fetched here,
# pinned to the EXACT version the crate's Cargo.lock locks (CLI and crate must
# match or wasm-bindgen refuses to run).
build_webgpu_wasm_demo() {
    local rpt="ExternalLib/Kataglyphis-RustProjectTemplate"
    local demo="docs/source/_webgpu_demo/webgpu-demo"
    local wb_ver
    wb_ver="$(grep -A1 '^name = "wasm-bindgen"$' "${rpt}/Cargo.lock" | grep '^version' | head -1 | sed -E 's/version = "(.*)"/\1/')"
    [ -n "${wb_ver}" ] || return 1
    rustup target add wasm32-unknown-unknown &&
    { { command -v wasm-bindgen >/dev/null 2>&1 \
        && [ "$(wasm-bindgen --version 2>/dev/null | awk '{print $2}')" = "${wb_ver}" ]; } \
        || cargo install --locked wasm-bindgen-cli --version "${wb_ver}"; } &&
    ( cd "${rpt}" && CARGO_TARGET_DIR="target" \
        cargo build -p kataglyphis_webgpu_renderer --target wasm32-unknown-unknown --release ) &&
    wasm-bindgen "${rpt}/target/wasm32-unknown-unknown/release/kataglyphis_webgpu_renderer.wasm" \
        --out-dir "${rpt}/crates/webgpu_renderer/web/pkg" --target web &&
    cp "${rpt}/crates/webgpu_renderer/web/index.html" "${demo}/index.html" &&
    cp "${rpt}/crates/webgpu_renderer/web/pkg/kataglyphis_webgpu_renderer.js" \
        "${demo}/pkg/kataglyphis_webgpu_renderer.js" &&
    cp "${rpt}/crates/webgpu_renderer/web/pkg/kataglyphis_webgpu_renderer_bg.wasm" \
        "${demo}/pkg/kataglyphis_webgpu_renderer_bg.wasm"
}

info "Rebuilding the WebGPU WASM browser demo from the current renderer"
if build_webgpu_wasm_demo; then
    info "WebGPU WASM demo rebuilt from current source"
else
    info "WebGPU WASM demo rebuild skipped/failed; keeping the committed snapshot"
fi

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
SPHINXOPTS="-W --keep-going" make html

info "Checking internal links and toctree references"
SPHINXOPTS="-W --keep-going" make linkcheck

info "Documentation build completed successfully"