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
DISTRO="$(lsb_release -cs)"
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
$SUDO apt-get install -y wget gpg lsb-release ca-certificates gnupg apt-transport-https

# -----------------------------------------------------------------------------
# Install CMake (latest from Kitware)
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
# Install Vulkan SDK
# -----------------------------------------------------------------------------
echo "Installing Vulkan SDK version ${VULKAN_VERSION} for architecture $ARCH..."
if [ "$ARCH" == "x86_64" ]; then
  # x64: use GPG keyring instead of deprecated apt-key
  LUNARG_KEY=/usr/share/keyrings/lunarg-archive-keyring.gpg
  wget -qO - https://packages.lunarg.com/lunarg-signing-key-pub.asc \
    | gpg --dearmor | $SUDO tee "$LUNARG_KEY" >/dev/null
  echo "deb [arch=amd64 signed-by=$LUNARG_KEY] https://packages.lunarg.com/vulkan/${VULKAN_VERSION} $DISTRO main" \
    | $SUDO tee /etc/apt/sources.list.d/lunarg-vulkan-${VULKAN_VERSION}-${DISTRO}.list >/dev/null
  $SUDO apt-get update -y
  $SUDO apt-get install -y vulkan-sdk
elif [[ "$ARCH" == "aarch64" || "$ARCH" == "arm64" ]]; then
  # ARM64
  $SUDO apt-get install -y xz-utils libglm-dev cmake libxcb-dri3-0 \
    libxcb-present0 libpciaccess0 libpng-dev libxcb-keysyms1-dev \
    libxcb-dri3-dev libx11-dev g++ gcc libwayland-dev \
    libxrandr-dev libxcb-randr0-dev libxcb-ewmh-dev git \
    python3 bison libx11-xcb-dev liblz4-dev libzstd-dev \
    ocaml ninja-build pkg-config libxml2-dev \
    wayland-protocols python3-jsonschema clang-format qtbase5-dev qt6-base-dev
  wget -q https://sdk.lunarg.com/sdk/download/${VULKAN_VERSION}.0/linux/vulkansdk-linux-aarch64-${VULKAN_VERSION}.0.tar.xz
  tar -xf vulkansdk-linux-aarch64-${VULKAN_VERSION}.0.tar.xz
  pushd ${VULKAN_VERSION}.0 >/dev/null
  chmod +x vulkansdk
  ./vulkansdk -j $(nproc) \
    glslang vulkan-tools vulkan-headers vulkan-loader \
    vulkan-validationlayers shaderc spirv-headers spirv-tools \
    vulkan-extensionlayer volk vma vcv vul slang
  popd >/dev/null
  echo "Remember to source setup-env.sh before running your app:"
  echo "  source \${PWD}/${VULKAN_VERSION}.0/setup-env.sh"
else
  echo "Unknown or unsupported architecture: $ARCH. Skipping Vulkan SDK." >&2
fi

# -----------------------------------------------------------------------------
# Install additional dependencies
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
ccache --version | head -n1
gcc --version | head -n1
g++ --version | head -n1
clang --version | head -n1

echo "All dependencies installed successfully."
