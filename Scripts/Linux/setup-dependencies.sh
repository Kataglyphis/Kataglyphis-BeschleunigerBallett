#!/usr/bin/env bash
set -euo pipefail

# -----------------------------------------------------------------------------
# setup-dependencies.sh
#
# Installs all required dependencies for building the project on Linux.
# Works both inside and outside GitHub Actions runners.
#
# Usage:
#   ./setup-dependencies.sh [vulkan-version] [--use-tarball]
#   vulkan-version (optional): e.g. "1.3.296" (default: 1.3.296)
#   --use-tarball: Force tarball installation even on x86_64
# -----------------------------------------------------------------------------

# Default Vulkan version
VULKAN_VERSION="1.3.296"
USE_TARBALL=false

# Parse arguments
while [[ $# -gt 0 ]]; do
  case $1 in
    --use-tarball)
      USE_TARBALL=true
      shift
      ;;
    -*)
      echo "Unknown option $1" >&2
      exit 1
      ;;
    *)
      if [ -z "${VULKAN_VERSION_SET:-}" ]; then
        VULKAN_VERSION="$1"
        VULKAN_VERSION_SET=true
      else
        echo "Usage: $0 [vulkan-version] [--use-tarball]" >&2
        exit 1
      fi
      shift
      ;;
  esac
done

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
# Vulkan SDK Installation Function for Tarball
# -----------------------------------------------------------------------------
install_vulkan_tarball() {
  local version="$1"
  local arch_suffix="x86_64"
  
  echo "Installing Vulkan SDK ${version} via tarball for ${ARCH}..."
  
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
  local vulkan_dir="${HOME}/vulkan"
  mkdir -p "$vulkan_dir"
  cd "$vulkan_dir"
  
  # Download tarball
  local tarball_name="vulkansdk-linux-${arch_suffix}-${version}.0.tar.xz"
  local download_url="https://sdk.lunarg.com/sdk/download/${version}.0/linux/${tarball_name}"
  
  echo "Downloading ${tarball_name}..."
  wget -q "$download_url" -O "$tarball_name"
  
  # Verify download (optional - you might want to add sha256 verification here)
  if [ ! -f "$tarball_name" ]; then
    echo "Failed to download Vulkan SDK tarball" >&2
    exit 1
  fi
  
  # Extract tarball
  echo "Extracting Vulkan SDK..."
  tar xf "$tarball_name"
  
  # Clean up tarball
  rm "$tarball_name"
  
  # Set up environment for current session
  local sdk_path="${vulkan_dir}/${version}.0"
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
# Install Vulkan SDK
# -----------------------------------------------------------------------------
echo "Installing Vulkan SDK version ${VULKAN_VERSION} for architecture $ARCH..."

if [ "$ARCH" == "x86_64" ] && [ "$USE_TARBALL" = false ]; then
  # x64: use GPG keyring instead of deprecated apt-key (existing behavior)
  LUNARG_KEY=/usr/share/keyrings/lunarg-archive-keyring.gpg
  wget -qO - https://packages.lunarg.com/lunarg-signing-key-pub.asc \
    | gpg --dearmor | $SUDO tee "$LUNARG_KEY" >/dev/null
  echo "deb [arch=amd64 signed-by=$LUNARG_KEY] https://packages.lunarg.com/vulkan/${VULKAN_VERSION} $DISTRO main" \
    | $SUDO tee /etc/apt/sources.list.d/lunarg-vulkan-${VULKAN_VERSION}-${DISTRO}.list >/dev/null
  $SUDO apt-get update -y
  $SUDO apt-get install -y vulkan-sdk
elif [[ "$ARCH" == "x86_64" && "$USE_TARBALL" = true ]] || [[ "$ARCH" == "aarch64" || "$ARCH" == "arm64" ]]; then
  # Use tarball installation for x86_64 when requested or ARM64 always
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

if [[ ("$ARCH" == "x86_64" && "$USE_TARBALL" = true) || "$ARCH" == "aarch64" || "$ARCH" == "arm64" ]]; then
  echo ""
  echo "IMPORTANT: Since you used tarball installation, remember to source the setup script:"
  echo "  source ~/vulkan/${VULKAN_VERSION}.0/setup-env.sh"
fi