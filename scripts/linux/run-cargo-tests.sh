#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib/common.sh
source "${SCRIPT_DIR}/lib/common.sh"

# Runs the Rust renderer crate's own test suite (kataglyphis_webgpu_renderer)
# from this repo's CI. Delegates to ContainerHub's cargo_test.sh
# (AGENTS.md § "Reusable Work Belongs in ContainerHub") rather than
# reimplementing it; the package filter below is just an extra arg forwarded
# to `cargo test --all --verbose`, so no upstream change was needed.
#
# GPU-touching tests in the crate already self-skip when no adapter is
# present (headless.rs, ibl.rs, occlusion.rs, gpu_timing.rs all print
# "SKIP: no GPU adapter available in this environment" and return), so a
# GPU-less CI runner still exercises the CPU-only tests and skips the rest.

REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
RUST_PROJECT_DIR="${RUST_PROJECT_DIR:-${REPO_ROOT}/ExternalLib/Kataglyphis-RustProjectTemplate}"
CARGO_TEST_SH="${REPO_ROOT}/ExternalLib/Kataglyphis-ContainerHub/linux/scripts/02-toolchain/rust/cargo_test.sh"

[[ -d "${RUST_PROJECT_DIR}" ]] || err "Rust project dir not found at ${RUST_PROJECT_DIR} (is the RustProjectTemplate submodule checked out?)"
[[ -f "${CARGO_TEST_SH}" ]] || err "cargo_test.sh not found at ${CARGO_TEST_SH} (is the ContainerHub submodule checked out?)"

export CARGO_TARGET_DIR="${CARGO_TARGET_DIR:-target}"
# The image ships CARGO_HOME=/usr/local/cargo owned by root, and the
# container runs as uid 1001; cargo_test.sh's own guard falls back to a
# writable dir automatically, but pin one explicitly so a registry write
# never depends on the guard's write-probe racing another cargo step's cache.
export CARGO_HOME="${CARGO_HOME:-/tmp/cargo-home}"
mkdir -p "${CARGO_HOME}"

info "=== Rust renderer test suite (kataglyphis_webgpu_renderer) ==="
( cd "${RUST_PROJECT_DIR}" && bash "${CARGO_TEST_SH}" -p kataglyphis_webgpu_renderer )
