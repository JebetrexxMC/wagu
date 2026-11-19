# the official omp-server is a 32-bit Linux program: i386 C/C++ runtimes
ARG BASE=ubuntu:26.04
FROM ${BASE}
ENV DEBIAN_FRONTEND=noninteractive
RUN \
    dpkg --add-architecture i386 && \
    apt-get update && \
    apt-get install -y --no-install-recommends \
        ca-certificates \
        tzdata \
        libc6:i386 \
        libstdc++6:i386 \
        libatomic1:i386 \
    && \
    rm -rf /var/lib/apt/lists/*
WORKDIR /omp
