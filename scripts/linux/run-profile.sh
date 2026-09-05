#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib/common.sh
source "${SCRIPT_DIR}/lib/common.sh"

APP_RUNNER_LIB="${SCRIPT_DIR}/../../third_party/ContainerHub/linux/scripts/lib/app-runner.sh"
if [[ ! -f "${APP_RUNNER_LIB}" ]]; then
  err "Shared app-runner library not found at '${APP_RUNNER_LIB}'. Initialize the Kataglyphis-ContainerHub submodule first."
fi
# shellcheck source=../../third_party/ContainerHub/linux/scripts/lib/app-runner.sh
source "${APP_RUNNER_LIB}"

APP_RUNNER_DEFAULT_EXE_NAME="GraphicsEngine"
APP_RUNNER_DEFAULT_BUILD_DIR="build"
APP_RUNNER_DEFAULT_BUILD_TYPE="RelWithDebInfo"
APP_RUNNER_LABEL="profile"
APP_RUNNER_USAGE_INTRO="Starts the built application from the profile build directory."

app_runner_main "$@"
