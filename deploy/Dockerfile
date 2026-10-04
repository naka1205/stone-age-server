# deploy/Dockerfile
# 多阶段构建: Ubuntu 24.04 + GCC 13 + CMake + Ninja
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    libssl-dev \
    git \
    python3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DSA_ENABLE_MYSQL_STORAGE=OFF \
    && cmake --build build --target stone_age_server --parallel $(nproc)

FROM ubuntu:24.04 AS runtime

RUN apt-get update && apt-get install -y \
    libssl3 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /src/build/src/stone_age_server /app/stone_age_server

EXPOSE 8300
ENTRYPOINT ["/app/stone_age_server", "--config", "config/demo.json"]
