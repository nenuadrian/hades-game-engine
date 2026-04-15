FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        git \
        libvulkan-dev \
        ninja-build \
        pkg-config \
        python3 \
        python3-pip \
    && rm -rf /var/lib/apt/lists/*

RUN pip install --break-system-packages --no-cache-dir \
      --index-url https://download.pytorch.org/whl/cpu \
      torch

WORKDIR /workspace

COPY . .

RUN cmake -S . -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DHADES_USE_BUNDLED_DEPS=OFF \
    && cmake --build build --target hades_tests

CMD ["ctest", "--test-dir", "build", "--output-on-failure"]
