#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib/common.sh
source "${SCRIPT_DIR}/lib/common.sh"

APP_RUNNER_LIB="${SCRIPT_DIR}/../../third_party/ContainerHub/linux/scripts/lib/app-runner.sh"
if [[ ! -f "${APP_RUNNER_LIB}" ]]; then
  err "Shared app-runner library not found at '${APP_RUNNER_LIB}'. Initialize the ContainerHub submodule first."
fi
# shellcheck source=../../third_party/ContainerHub/linux/scripts/lib/app-runner.sh
source "${APP_RUNNER_LIB}"

APP_RUNNER_DEFAULT_EXE_NAME="GraphicsEngine"
APP_RUNNER_DEFAULT_BUILD_DIR="build-release"
APP_RUNNER_DEFAULT_BUILD_TYPE="Release"
APP_RUNNER_LABEL="release"
APP_RUNNER_USAGE_INTRO="Starts the built application from the release build directory."
APP_RUNNER_ENABLE_SHADER_CLEAN=true
APP_RUNNER_SHADER_CLEAN_DIR="Resources/ShadersSlang/build"
APP_RUNNER_SHADER_COMPILE_SCRIPT="${SCRIPT_DIR}/compile-slang-shaders.sh"

# For release builds we prefer not to have validation layers intercepting Vulkan if present
app_runner_env_hook() {
  export VK_LAYER_PATH=""
  export VK_INSTANCE_LAYERS=""
}

app_runner_main "$@"
