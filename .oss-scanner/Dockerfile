# Build environment for the OSS Scanner (https://github.com/anthropics/oss-scanner).
# Builds the C game server (Server/ + Shared/) and installs the Node.js MasterServer's dependencies.
# The browser client (Client/, C compiled to WebAssembly with Emscripten) is not built here.
FROM debian:bookworm
ENV DEBIAN_FRONTEND=noninteractive \
    CC=clang \
    CXX=clang++
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates build-essential clang libclang-rt-dev cmake pkg-config git python3 \
      libwebsockets-dev libcurl4-openssl-dev nodejs npm \
    && rm -rf /var/lib/apt/lists/*

# scanner contract: the checkout lives inside the image, at /src
COPY . /src
WORKDIR /src

# Game server, debug build (asserts enabled) with AddressSanitizer + UBSan. Run: /src/Server/build-asan/rrolf-server
RUN cmake -S /src/Server -B /src/Server/build-asan \
      -DDEBUG_BUILD=1 -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
      -DCMAKE_C_FLAGS="-g -fno-omit-frame-pointer -fsanitize=address,undefined" \
    && cmake --build /src/Server/build-asan -j"$(nproc)"

# Game server, release build as deployed (-O3, NDEBUG). Run: /src/Server/build-release/rrolf-server
RUN cmake -S /src/Server -B /src/Server/build-release -DDEBUG_BUILD=0 \
    && cmake --build /src/Server/build-release -j"$(nproc)"

# Master server (Node.js): dependencies only, it runs straight from source (node MasterServer/main.js)
RUN cd /src/MasterServer && npm ci
