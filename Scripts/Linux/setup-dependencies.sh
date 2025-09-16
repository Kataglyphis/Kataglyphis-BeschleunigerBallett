#!/usr/bin/env bash
set -euo pipefail
# -----------------------------------------------------------------------------
# setup-dependencies.sh
#
# Installs all required dependencies for building the project on Linux.
# Works both inside and outside GitHub Actions runners.
#
# Usage:
#   ./setup-dependencies.sh [vulkan-version]
#   vulkan-version (optional): e.g. "1.3.296" (default: 1.3.296)
#   Use "latest" to automatically fetch the current latest SDK.
# -----------------------------------------------------------------------------

# Default Vulkan version
VULKAN_VERSION="1.3.296"
if [ "$#" -gt 1 ]; then
  echo "Usage: $0 [vulkan-version]" >&2
  exit 1
elif [ "$#" -eq 1 ]; then
  VULKAN_VERSION="$1"
fi

# Detect system architecture and distribution
ARCH="$(uname -m)"
DISTRO="$(lsb_release -cs || echo unknown)"
echo "Detected architecture: $ARCH"
echo "Detected distribution codename: $DISTRO"

# Detect if sudo is required
SUDO=""
if [ "$EUID" -ne 0 ]; then
  if command -v sudo >/dev/null 2>&1; then
    SUDO="sudo"
  else
    echo "This script requires root privileges or sudo. Aborting." >&2
    exit 1
  fi
fi

# Utility: update package database
echo "Refreshing package databases..."
$SUDO apt-get update -y

# Install basic tools
echo "Installing core tools..."
$SUDO apt-get install -y wget gpg lsb-release ca-certificates gnupg apt-transport-https curl xz-utils

# -----------------------------------------------------------------------------
# Install CMake (latest from Kitware)  - unchanged from your script
# -----------------------------------------------------------------------------
if ! command -v cmake >/dev/null 2>&1; then
  echo "Installing latest CMake..."
  KITWARE_KEY=/usr/share/keyrings/kitware-archive-keyring.gpg
  wget -qO - https://apt.kitware.com/keys/kitware-archive-latest.asc \
    | gpg --dearmor \
    | $SUDO tee "$KITWARE_KEY" >/dev/null
  echo "deb [signed-by=$KITWARE_KEY] https://apt.kitware.com/ubuntu $DISTRO main" \
    | $SUDO tee /etc/apt/sources.list.d/kitware.list >/dev/null
  $SUDO apt-get update -y
  $SUDO apt-get install -y cmake
else
  echo "cmake already installed: $(cmake --version | head -n1)"
fi

# -----------------------------------------------------------------------------
# Install Vulkan SDK (tarball-based; replaces apt/repo flow)
# - uses LunarG SDK web API to fetch 'latest' or a specific version, and SHA.
# - extracts into /opt/vulkan (system) or $HOME/vulkan (user).
# -----------------------------------------------------------------------------
echo "Installing Vulkan SDK version ${VULKAN_VERSION} for architecture $ARCH..."

# Decide install prefix
if [ -n "$SUDO" ]; then
  INSTALL_PREFIX=${INSTALL_PREFIX:-/opt/vulkan}
else
  INSTALL_PREFIX=${INSTALL_PREFIX:-"$HOME/vulkan"}
fi

TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT

# support 'latest' keyword: query LunarG latest API
if [ "${VULKAN_VERSION}" = "latest" ] || [ -z "${VULKAN_VERSION}" ]; then
  echo "Fetching latest Linux SDK version from LunarG..."
  VULKAN_VERSION="$(curl -fsS https://vulkan.lunarg.com/sdk/latest/linux.txt || true)"
  if [ -z "$VULKAN_VERSION" ]; then
    echo "Failed to determine latest Vulkan SDK version. Aborting." >&2
    exit 1
  fi
  echo "Resolved latest Vulkan SDK: $VULKAN_VERSION"
fi

# Construct download & sha URLs
SDK_FILENAME="vulkan_sdk.tar.xz"
DOWNLOAD_URL="https://sdk.lunarg.com/sdk/download/${VULKAN_VERSION}/linux/${SDK_FILENAME}"
SHA_URL="https://sdk.lunarg.com/sdk/sha/${VULKAN_VERSION}/linux/${SDK_FILENAME}.txt"

echo "Download URL: ${DOWNLOAD_URL}"

cd "$TMPDIR"

# Download the SDK tarball
echo "Downloading SDK tarball..."
if ! curl -fSL -o "${SDK_FILENAME}" "${DOWNLOAD_URL}"; then
  echo "Download failed for ${DOWNLOAD_URL}" >&2
  echo "If LunarG does not publish a linux tarball for this version/platform, try 'latest' or a different version." >&2
  exit 1
fi

# Attempt to fetch the expected sha (if available)
EXPECTED_SHA=""
if curl -fsS -o expected.sha "${SHA_URL}"; then
  EXPECTED_SHA="$(tr -d ' \t\r\n' < expected.sha)"
  echo "Fetched expected SHA: ${EXPECTED_SHA}"
else
  echo "Warning: could not fetch expected SHA from LunarG API (${SHA_URL}). Continuing but please verify integrity manually." >&2
fi

# Compute local sha256
LOCAL_SHA="$(sha256sum "${SDK_FILENAME}" | awk '{print $1}')"
echo "Local SHA256: ${LOCAL_SHA}"

if [ -n "$EXPECTED_SHA" ] && [ "$EXPECTED_SHA" != "$LOCAL_SHA" ]; then
  echo "SHA mismatch! Expected ${EXPECTED_SHA} but got ${LOCAL_SHA}. Aborting." >&2
  exit 1
fi

# Extract the tarball to install prefix
echo "Creating install prefix: ${INSTALL_PREFIX} (requires sudo if system-wide)..."
$SUDO mkdir -p "${INSTALL_PREFIX}"
$SUDO tar -xJf "${SDK_FILENAME}" -C "${INSTALL_PREFIX}" || { echo "Extraction failed"; exit 1; }

# Find extracted directory name (first component of the tar)
EXTRACTED_DIR=$($SUDO tar -tf "${SDK_FILENAME}" | head -n1 | cut -d/ -f1)
FULL_SDK_DIR="${INSTALL_PREFIX}/${EXTRACTED_DIR}"

if [ ! -d "${FULL_SDK_DIR}" ]; then
  echo "ERROR: expected extracted SDK directory not found: ${FULL_SDK_DIR}" >&2
  echo "List of ${INSTALL_PREFIX}:"
  ls -al "${INSTALL_PREFIX}" || true
  exit 1
fi

# Create or update a stable 'latest' symlink for convenience
$SUDO ln -sfn "${FULL_SDK_DIR}" "${INSTALL_PREFIX}/latest"
echo "SDK extracted to: ${FULL_SDK_DIR}"
echo "Created symlink: ${INSTALL_PREFIX}/latest -> ${FULL_SDK_DIR}"

# system vs user environment installation of env file
VULKAN_SDK_PATH="${INSTALL_PREFIX}/latest/x86_64"
ENV_CONTENT=$(cat <<EOF
# Vulkan SDK environment (added by setup-dependencies.sh)
export VULKAN_SDK="${VULKAN_SDK_PATH}"
export PATH="\$VULKAN_SDK/bin:\$PATH"
export LD_LIBRARY_PATH="\$VULKAN_SDK/lib:\${LD_LIBRARY_PATH:-}"
export VK_LAYER_PATH="\$VULKAN_SDK/etc/vulkan/explicit_layer.d"
EOF
)

if [ "${INSTALL_PREFIX}" = "/opt/vulkan" ] && [ -w /etc/profile.d ]; then
  echo "Writing system profile /etc/profile.d/vulkan_sdk.sh"
  echo "${ENV_CONTENT}" | $SUDO tee /etc/profile.d/vulkan_sdk.sh >/dev/null
  $SUDO chmod 644 /etc/profile.d/vulkan_sdk.sh
  echo "Sourced at login system-wide. To apply in current shell: source /etc/profile.d/vulkan_sdk.sh"
else
  echo "Appending Vulkan environment to ~/.profile (user-local)"
  # idempotent append (avoid duplicate block)
  if ! grep -q 'Vulkan SDK environment (added by setup-dependencies.sh)' ~/.profile 2>/dev/null; then
    printf "\n# Vulkan SDK environment (added by setup-dependencies.sh)\n%s\n" "$ENV_CONTENT" >> ~/.profile
    echo "Appended to ~/.profile. Run 'source ~/.profile' or open a new shell."
  else
    echo "~/.profile already contains a Vulkan SDK block. Skipping append."
  fi
fi

# If the SDK provided a setup-env.sh, source it for the current shell (best-effort)
SETUP_SCRIPT="${FULL_SDK_DIR}/setup-env.sh"
if [ -f "${SETUP_SCRIPT}" ]; then
  echo "Sourcing ${SETUP_SCRIPT} for the current shell (temporary)."
  # shellcheck disable=SC1090
  source "${SETUP_SCRIPT}"
  echo "VULKAN_SDK is now: ${VULKAN_SDK:-<not set>}"
else
  echo "No setup-env.sh found at ${SETUP_SCRIPT} (this is unusual). Use the environment block above."
fi

# -----------------------------------------------------------------------------
# Fallback/ARM64 notes:
# LunarG primarily publishes x86_64 Linux binaries. If you're on aarch64 and a
# prebuilt ARM64 tarball isn't available, you may need to:
#  - use the x86_64 SDK installer script (some users have used it to build host tools),
#  - or build specific components from sources (vulkan-tools, validation layers, etc),
#  - or use distro-provided packages where available.
# See LunarG docs and SDK API for available platform builds. :contentReference[oaicite:1]{index=1}
# -----------------------------------------------------------------------------

# -----------------------------------------------------------------------------
# Install additional dependencies (unchanged from your script)
# -----------------------------------------------------------------------------
echo "Installing GLFW, rendering, and build-tool dependencies..."
$SUDO apt-get install -y \
  libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libglu1-mesa-dev \
  freeglut3-dev mesa-common-dev mesa-utils wayland-protocols libwayland-dev \
  libxkbcommon-dev libglx-mesa0 ninja-build ccache sccache iwyu graphviz \
  doxygen libosmesa6-dev gcovr clang llvm

# Confirm key tools
echo "Installed versions:"
cmake --version
ccache --version | head -n1 || true
gcc --version | head -n1 || true
g++ --version | head -n1 || true
clang --version || true

echo "All dependencies installed successfully."
