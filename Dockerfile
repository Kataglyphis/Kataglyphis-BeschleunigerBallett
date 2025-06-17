# syntax=docker/dockerfile:1.4
FROM ubuntu:24.04 AS base

# --------- Build args & env ---------
ARG BUILD_TYPE=Debug
ARG VULKAN_VERSION=1.3.296
ARG TARGETARCH

ENV DEBIAN_FRONTEND=noninteractive \
    APT_LISTCHANGES_FRONTEND=none \
    BUILD_TYPE=${BUILD_TYPE} \
    VULKAN_VERSION=${VULKAN_VERSION} \
    WORKDIR=/workspace

# --------- Common prerequisites ---------
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
      ca-certificates \
      curl \
      pkg-config \
      gnupg \
      lsb-release \
      wget \
      xz-utils \
      software-properties-common \
      sudo \
      git \
      libssl-dev \
      build-essential ninja-build make \
      clang            \
      clang-format     \
      clang-tidy       \
      lld              \
      lldb             \
      llvm             \
      llvm-dev         \
      libclang-dev \
      libclang-rt-dev && \
    rm -rf /var/lib/apt/lists/*

# --------- Install Rust via rustup (official method) ---------
# Download and install rustup (the Rust installer) non‑interactively
# The `-s -- -y` flags tell rustup.sh to skip prompts and accept defaults
RUN curl https://sh.rustup.rs -sSf | sh -s -- -y

# Make sure the Cargo bin directory is on PATH
ENV PATH="/root/.cargo/bin:${PATH}"

# (Optional) Verify installation
RUN rustc --version && cargo --version

# --------- Install Kitware CMake ---------
RUN mkdir -p /usr/share/keyrings && \
    wget -qO- https://apt.kitware.com/keys/kitware-archive-latest.asc \
      | gpg --dearmor \
      | tee /usr/share/keyrings/kitware-archive-keyring.gpg >/dev/null && \
    echo "deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu noble main" \
      | tee /etc/apt/sources.list.d/kitware.list && \
    apt-get update && \
    apt-get install -y --no-install-recommends cmake && \
    rm -rf /var/lib/apt/lists/*

# --------- Add LunarG key & repo for Vulkan SDK ---------
RUN wget -qO- https://packages.lunarg.com/lunarg-signing-key-pub.asc | apt-key add - && \
    wget -qO /etc/apt/sources.list.d/lunarg-vulkan-${VULKAN_VERSION}-noble.list https://packages.lunarg.com/vulkan/${VULKAN_VERSION}/lunarg-vulkan-${VULKAN_VERSION}-noble.list

# --------- Install Vulkan SDK per-architecture ---------
RUN if [ "$TARGETARCH" = "amd64" ]; then \
      apt-get update && \
      apt-get install -y --no-install-recommends vulkan-sdk \
      vulkan-validationlayers \
      vulkan-tools \
      libvulkan1 \
      mesa-vulkan-drivers \
      vulkan-utils \
      libvulkan-dev \
      nvidia-utils-570 \
      libnvidia-gl-570 && \
      rm -rf /var/lib/apt/lists/*; \
    else \
      apt-get update && \
      DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        libglm-dev cmake libxcb-dri3-0 libxcb-present0 libpciaccess0 \
        libpng-dev libxcb-keysyms1-dev libxcb-dri3-dev libx11-dev \
        git python-is-python3 bison libx11-xcb-dev liblz4-dev libzstd-dev \
        ocaml-core ninja-build pkg-config libxml2-dev wayland-protocols \
        python3-jsonschema clang-format qtbase5-dev qt6-base-dev \
        g++ g++-13 g++-13-aarch64-linux-gnu g++-aarch64-linux-gnu libffi-dev \
        libstdc++-13-dev libwayland-bin libwayland-cursor0 libwayland-dev \
        libwayland-egl1 libxcb-ewmh2 libxcb-render0-dev libxrandr-dev libxrandr2 libxrender-dev \
        libxcb-ewmh-dev libxcb-randr0-dev && \
      rm -rf /var/lib/apt/lists/* && \
      wget -q https://sdk.lunarg.com/sdk/download/${VULKAN_VERSION}.0/linux/vulkansdk-linux-x86_64-${VULKAN_VERSION}.0.tar.xz && \
      tar -xf vulkansdk-linux-x86_64-${VULKAN_VERSION}.0.tar.xz && \
      cd ${VULKAN_VERSION}.0 && \
      chmod +x vulkansdk && \
      ./vulkansdk -j "$(nproc)" glslang vulkan-tools vulkan-headers vulkan-loader vulkan-validationlayers shaderc spirv-headers spirv-tools vulkan-extensionlayer volk vma vcv vul slang && \
      echo "${PWD}/aarch64/bin" >> /etc/profile.d/vulkan-sdk.sh; \
    fi

# --------- Install compiler, test & graphics deps ---------
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
      build-essential \
      ninja-build sccache ccache \
      python3 python3-pip \
      doxygen graphviz gcovr libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
      libglu1-mesa-dev freeglut3-dev mesa-common-dev mesa-utils wayland-protocols \
      libwayland-dev libxkbcommon-dev libglx-mesa0 libosmesa6-dev && \
    rm -rf /var/lib/apt/lists/*

# --------- Workspace ---------
WORKDIR ${WORKDIR}
VOLUME ["${WORKDIR}"]

ENTRYPOINT ["/bin/bash"]
