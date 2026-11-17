FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build gdb libgtest-dev time ca-certificates \
        pkg-config libcpp-httplib-dev curl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
CMD ["bash"]