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
$SUDO apt-get install -y wget curl gpg lsb-release ca-certificates gnupg apt-transport-https

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
# Vulkan SDK Installation Function for Tarball
# -----------------------------------------------------------------------------
verify_vulkan_version() {
  local version="$1"
  echo "Attempting to verify Vulkan SDK version ${version} availability..."
  
  # Try to access the download page to see if version exists
  local test_url="https://sdk.lunarg.com/sdk/download/${version}/linux/"
  if ! curl -sSf --connect-timeout 10 "$test_url" >/dev/null 2>&1; then
    echo "Warning: Could not verify version ${version} exists at ${test_url}" >&2
    echo "This might be due to network issues or the version may not exist." >&2
    echo "Common available versions include: 1.3.290, 1.3.296, 1.3.280, etc." >&2
    echo "Check https://vulkan.lunarg.com/ for available versions." >&2
    echo "Continuing with download attempt..." >&2
  fi
}

install_vulkan_tarball() {
  local version="$1"
  local arch_suffix="x86_64"
  
  echo "Installing Vulkan SDK ${version} via tarball for ${ARCH}..."
  
  # Verify version availability
  verify_vulkan_version "$version"
  
  # Install prerequisite packages for tarball installation
  echo "Installing tarball prerequisites..."
  $SUDO apt-get install -y xz-utils libglm-dev libxcb-dri3-0 \
    libxcb-present0 libpciaccess0 libpng-dev libxcb-keysyms1-dev \
    libxcb-dri3-dev libx11-dev g++ gcc libwayland-dev \
    libxrandr-dev libxcb-randr0-dev libxcb-ewmh-dev git \
    python3 bison libx11-xcb-dev liblz4-dev libzstd-dev \
    ocaml ninja-build pkg-config libxml2-dev \
    wayland-protocols python3-jsonschema clang-format qtbase5-dev qt6-base-dev \
    libxcb-xinput0 libxcb-xinerama0 libxcb-cursor-dev
  
  # Create Vulkan directory
  # local vulkan_dir="""
  # mkdir -p "$vulkan_dir"
  #cd "$vulkan_dir"
  
  # Download tarball
  local tarball_name="vulkansdk-linux-${arch_suffix}-${version}.tar.xz"
  local download_url="https://sdk.lunarg.com/sdk/download/${version}/linux/${tarball_name}"
  
  echo "Downloading ${tarball_name}..."
  if ! wget --timeout=30 --tries=3 -q "$download_url" -O "$tarball_name"; then
    echo "Failed to download Vulkan SDK from: $download_url" >&2
    echo "Please check if the version ${version} exists at https://vulkan.lunarg.com/" >&2
    exit 1
  fi
  
  # Verify download (optional - you might want to add sha256 verification here)
  if [ ! -f "$tarball_name" ] || [ ! -s "$tarball_name" ]; then
    echo "Failed to download Vulkan SDK tarball or file is empty" >&2
    exit 1
  fi
  
  echo "Successfully downloaded $(du -h "$tarball_name" | cut -f1) tarball"
  
  # Extract tarball
  echo "Extracting Vulkan SDK..."
  tar xf "$tarball_name"
  
  # Clean up tarball
  rm "$tarball_name"
  
  # Set up environment for current session
  local sdk_path="${version}" #${vulkan_dir}/
  if [ -d "$sdk_path" ]; then
    echo "Vulkan SDK extracted to: $sdk_path"
    echo ""
    echo "To use the Vulkan SDK, run the following command in each terminal session:"
    echo "  source ${sdk_path}/setup-env.sh"
    echo ""
    echo "Or add this to your ~/.bashrc or ~/.profile for automatic setup:"
    echo "  echo 'source ${sdk_path}/setup-env.sh' >> ~/.bashrc"
    echo ""
    echo "You can also build all SDK components from source using:"
    echo "  cd ${sdk_path} && ./vulkansdk"
  else
    echo "Failed to extract Vulkan SDK properly" >&2
    exit 1
  fi
}

# -----------------------------------------------------------------------------
# Install Vulkan SDK (via tarball for all architectures)
# -----------------------------------------------------------------------------
echo "Installing Vulkan SDK version ${VULKAN_VERSION} via tarball for architecture $ARCH..."

if [ "$ARCH" == "x86_64" ] || [[ "$ARCH" == "aarch64" || "$ARCH" == "arm64" ]]; then
  install_vulkan_tarball "$VULKAN_VERSION"
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

echo ""
echo "IMPORTANT: Remember to source the setup script before using Vulkan:"
echo "  source ~/vulkan/${VULKAN_VERSION}/setup-env.sh"