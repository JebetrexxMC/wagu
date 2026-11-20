#!/bin/bash
# 5.02: a real open.mp server (official Linux build, omp-server 32-bit) in an Ubuntu 26.04 / Debian 12
# container - the PC side for testing the crossplay: phones join it from libMP (Add server > open.mp /
# SA-MP > Join PC server, or test/sampgate_test), PC players join it as usual. UDP 7777.
#   ./run_omp.sh                 DISTRO=debian-12 ./run_omp.sh      OMP_PORT=7778 ./run_omp.sh
cd "$(dirname "$0")" || exit 1
OMP_VERSION=${OMP_VERSION:-v1.6.7.3218}
[[ -z "$UBUNTU_VERSION" ]] && ubuntu_version=26.04 || ubuntu_version="$UBUNTU_VERSION"
[[ -z "$DISTRO" ]] && distro="ubuntu-${ubuntu_version}" || distro="$DISTRO"
port=${OMP_PORT:-7777}
if [ ! -x Server/omp-server ]; then
    curl -fL --retry 5 -o open.mp-linux-x86.tar.gz "https://github.com/openmultiplayer/open.mp/releases/download/${OMP_VERSION}/open.mp-linux-x86.tar.gz" || exit 1
    tar xzf open.mp-linux-x86.tar.gz && rm -f open.mp-linux-x86.tar.gz || exit 1
fi
docker build -t mp/omp:${distro} --build-arg BASE="${distro/-/:}" . || exit 1
mode=(-ti); [ "$DETACH" = 1 ] && mode=(-d --name omp-${distro})   # DETACH=1: in the background
docker run --rm "${mode[@]}" -v "$PWD/Server":/omp -w /omp -p ${port}:${port}/udp -p ${port}:${port}/tcp mp/omp:${distro} \
    ./omp-server -c network.port=${port}
