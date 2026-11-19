#!/bin/bash
# 5.02: builds the MP server (server/mp_server, with the open.mp / SA-MP crossplay) inside Docker,
# the same way open.mp's docker/build.sh builds omp-server. The repo is mounted at /code.
#   ./build.sh                          Ubuntu 26.04 (default)
#   DISTRO=debian-12 ./build.sh         Debian 12
#   UBUNTU_VERSION=26.04 ./build.sh     (same as the default)
#   CONFIG=Debug ./build.sh             -O0 -g
#   BUILD_TESTS=1 ./build.sh            + test/sampgate_test (crossplay test against an open.mp server)
cd "$(dirname "$0")" || exit 1
[[ -z "$CONFIG" ]] && config=Release || config="$CONFIG"
[[ -z "$UBUNTU_VERSION" ]] && ubuntu_version=26.04 || ubuntu_version="$UBUNTU_VERSION"
[[ -z "$DISTRO" ]] && distro="ubuntu-${ubuntu_version}" || distro="$DISTRO"
[[ -z "$BUILD_TESTS" ]] && build_tests=0 || build_tests="$BUILD_TESTS"
if [[ ! -d "build_${distro}" ]]; then
    echo "Unknown distro '${distro}'. Available: $(ls -d build_*/ | sed 's#build_##;s#/##' | tr '\n' ' ')"; exit 1
fi

docker build -t mp/build:${distro} build_${distro}/ || exit 1

docker run \
    --rm \
    -t \
    -w /code \
    -v "$PWD/..":/code \
    -e CONFIG=${config} \
    -e BUILD_TESTS=${build_tests} \
    -e HOST_UID=$(id -u) \
    -e HOST_GID=$(id -g) \
    mp/build:${distro} || exit 1
echo "Built: server/mp_server (${distro}, ${config})"
