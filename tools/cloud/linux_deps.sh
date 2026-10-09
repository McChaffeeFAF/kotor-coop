# Sourced by tools/cloud/linux_test.sh and linux_engine.sh: installs the build
# dependencies (apt, SDL3 and GameNetworkingSockets from source, as Ubuntu 24.04
# packages neither) and exports the paths the build needs.
#
# Env: JOBS, SDL3_VERSION, SDL3_PREFIX, GNS_VERSION, GNS_PREFIX,
#      SKIP_DEPS=1 to skip apt and the source builds (paths are still exported).

JOBS="${JOBS:-$(nproc)}"
SDL3_VERSION="${SDL3_VERSION:-3.4.8}"
SDL3_PREFIX="${SDL3_PREFIX:-/opt/sdl3}"
GNS_VERSION="${GNS_VERSION:-1.6.0}"
GNS_PREFIX="${GNS_PREFIX:-/opt/gns}"
SUDO=""; [ "$(id -u)" -eq 0 ] || SUDO="sudo"

if [ -z "${SKIP_DEPS:-}" ]; then
    export DEBIAN_FRONTEND=noninteractive
    $SUDO apt-get update -qq
    $SUDO apt-get install -y -qq build-essential cmake ninja-build pkg-config git patchelf \
        libboost-all-dev libgtest-dev libgmock-dev libmad0-dev libopenal-dev \
        libgl-dev libglu1-mesa-dev xvfb libgl1-mesa-dri \
        libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev \
        libxss-dev libxtst-dev libxrender-dev libxinerama-dev libxkbcommon-dev \
        libwayland-dev libdrm-dev libgbm-dev libegl-dev libasound2-dev libpulse-dev \
        libudev-dev libdecor-0-dev libibus-1.0-dev \
        libprotobuf-dev protobuf-compiler libssl-dev \
        libavcodec-dev libavformat-dev libavutil-dev libswresample-dev libswscale-dev \
        libwxgtk3.2-dev

    # SDL3 is not packaged for Ubuntu 24.04; build it from source.
    if [ ! -f "$SDL3_PREFIX/lib/pkgconfig/sdl3.pc" ] && ! pkg-config --exists sdl3; then
        src="$(mktemp -d)"
        git clone -q --depth 1 --branch "release-$SDL3_VERSION" https://github.com/libsdl-org/SDL.git "$src/SDL"
        cmake -S "$src/SDL" -B "$src/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_INSTALL_PREFIX="$SDL3_PREFIX" -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
        cmake --build "$src/build" -j"$JOBS"
        $SUDO cmake --install "$src/build"
        rm -rf "$src"
    fi

    # GameNetworkingSockets (co-op networking) is not packaged either.
    if [ ! -f "$GNS_PREFIX/lib/cmake/GameNetworkingSockets/GameNetworkingSocketsConfig.cmake" ]; then
        src="$(mktemp -d)"
        git clone -q --depth 1 --branch "v$GNS_VERSION" https://github.com/ValveSoftware/GameNetworkingSockets.git "$src/gns"
        cmake -S "$src/gns" -B "$src/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_INSTALL_PREFIX="$GNS_PREFIX" -DBUILD_SHARED_LIB=ON -DBUILD_STATIC_LIB=OFF \
            -DBUILD_EXAMPLES=OFF -DBUILD_TESTS=OFF -DBUILD_TOOLS=OFF \
            -DUSE_CRYPTO=OpenSSL -DUSE_CRYPTO25519=OpenSSL
        cmake --build "$src/build" -j"$JOBS"
        $SUDO cmake --install "$src/build"
        rm -rf "$src"
    fi
fi

export PKG_CONFIG_PATH="$SDL3_PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export LD_LIBRARY_PATH="$SDL3_PREFIX/lib:$GNS_PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export CMAKE_PREFIX_PATH="$GNS_PREFIX${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
