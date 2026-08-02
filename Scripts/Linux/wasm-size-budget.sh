#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib/common.sh
source "${SCRIPT_DIR}/lib/common.sh"

# Builds kataglyphis_webgpu_renderer for wasm32-unknown-unknown, optimises with
# wasm-opt -Oz, and fails the step if the result exceeds the size budget - so a
# bloat regression is caught here, not discovered after the demo is already
# deployed by "Sync files to domain". Mirrors Scripts/Test-WasmSizeBudget.ps1
# (the local/Windows equivalent), adapted to the container's bash + no
# guaranteed wasm-opt on PATH.
#
# Budget default measured 2026-07-31 straight off `cargo build --release` +
# wasm-opt -Oz on this host: pre-opt 9,873,819 bytes, post-opt 8,730,038 bytes.
# The ~3.7 MB figure previously carried in BACKLOG.md/docs was stale - nothing
# had ever measured or enforced it. 12 MiB gives ~1.8 MiB of headroom above the
# measured figure without hiding a real regression.

BINARYEN_VERSION="version_131"
BINARYEN_ASSET="binaryen-${BINARYEN_VERSION}-x86_64-linux.tar.gz"
BINARYEN_SHA256="b5bf1f0eaf17c63ee588ff7a5954dc8f6ce2c26989051c66f24dfe9ece3e46db"

REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
RUST_PROJECT_DIR="${RUST_PROJECT_DIR:-${REPO_ROOT}/ExternalLib/Kataglyphis-RustProjectTemplate}"
WASM_FILE="${RUST_PROJECT_DIR}/target/wasm32-unknown-unknown/release/kataglyphis_webgpu_renderer.wasm"
BUDGET_BYTES="${WASM_SIZE_BUDGET_BYTES:-12582912}" # 12 MiB

info "=== Wasm Size Budget Test ==="
info "Budget: ${BUDGET_BYTES} bytes ($(( BUDGET_BYTES / 1024 / 1024 )) MiB)"

info "Ensuring wasm32-unknown-unknown target is installed"
rustup target add wasm32-unknown-unknown

info "Building kataglyphis_webgpu_renderer for wasm32-unknown-unknown (release)"
( cd "${RUST_PROJECT_DIR}" && CARGO_TARGET_DIR="target" \
    cargo build -p kataglyphis_webgpu_renderer --target wasm32-unknown-unknown --release )

[[ -f "${WASM_FILE}" ]] || err "Wasm file not found at ${WASM_FILE}"

if ! command -v wasm-opt >/dev/null 2>&1; then
  info "wasm-opt not on PATH; fetching pinned binaryen ${BINARYEN_VERSION}"
  # SHA-verified download comes from ContainerHub 01-core (download_verified_file).
  source_module downloads.sh || err "ContainerHub downloads.sh not available for verified binaryen fetch"
  TMP_DIR="$(mktemp -d)"
  trap 'rm -rf "${TMP_DIR}"' EXIT
  download_verified_file \
    "https://github.com/WebAssembly/binaryen/releases/download/${BINARYEN_VERSION}/${BINARYEN_ASSET}" \
    "${BINARYEN_SHA256}" \
    "${TMP_DIR}/${BINARYEN_ASSET}"
  tar -xzf "${TMP_DIR}/${BINARYEN_ASSET}" -C "${TMP_DIR}"
  export PATH="${TMP_DIR}/binaryen-${BINARYEN_VERSION}/bin:${PATH}"
fi

PRE_SIZE=$(stat -c%s "${WASM_FILE}")
info "Pre-opt size: ${PRE_SIZE} bytes"

info "Running wasm-opt -Oz"
OPT_FILE="${WASM_FILE%.wasm}.opt.wasm"
# wgpu/naga emit bulk-memory, nontrapping-float-to-int, sign-extension and simd
# instructions; the explicit flags are the exact feature names wasm-opt's
# validator asks for. --all-features is the fallback if a future codegen
# change needs a feature not listed here.
if ! wasm-opt -Oz \
  --enable-bulk-memory-opt --enable-nontrapping-float-to-int --enable-simd \
  --enable-sign-ext --enable-reference-types --enable-mutable-globals --enable-multivalue \
  "${WASM_FILE}" -o "${OPT_FILE}"; then
  warn "wasm-opt with explicit feature flags failed; retrying with --all-features"
  wasm-opt -Oz --all-features "${WASM_FILE}" -o "${OPT_FILE}"
fi
mv -f "${OPT_FILE}" "${WASM_FILE}"

FINAL_SIZE=$(stat -c%s "${WASM_FILE}")
info "Post-opt size: ${FINAL_SIZE} bytes (saved $(( PRE_SIZE - FINAL_SIZE )) bytes)"

if (( FINAL_SIZE > BUDGET_BYTES )); then
  err "FAIL: wasm size ${FINAL_SIZE} bytes exceeds budget of ${BUDGET_BYTES} bytes ($(( FINAL_SIZE - BUDGET_BYTES )) bytes over). Consider feature flags, LTO, dead-code elimination, or raising WASM_SIZE_BUDGET_BYTES deliberately."
fi

info "PASS: wasm size ${FINAL_SIZE} bytes is within budget ($(( BUDGET_BYTES - FINAL_SIZE )) bytes to spare)."
