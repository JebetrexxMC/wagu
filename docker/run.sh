#!/bin/bash
# 5.02: runs the MP server in a container (like open.mp's docker/run.sh). Phones join on UDP 1922
# (and through the online lobby), PC players with SA-MP 0.3.7 / the open.mp launcher on UDP 7777.
#   ./run.sh                                   Ubuntu 26.04, "MP LAN Server" on 1922, SA-MP port 7777
#   DISTRO=debian-12 ./run.sh
#   MP_PORT=1940 MP_NAME="My Server" MP_MAX=50 SAMP_PORT=7778 REGION=Europe ./run.sh
#   SAMP_PORT=0 ./run.sh                       no PC crossplay
cd "$(dirname "$0")" || exit 1
[[ -z "$CONFIG" ]] && config=Release || config="$CONFIG"
[[ -z "$UBUNTU_VERSION" ]] && ubuntu_version=26.04 || ubuntu_version="$UBUNTU_VERSION"
[[ -z "$DISTRO" ]] && distro="ubuntu-${ubuntu_version}" || distro="$DISTRO"
mp_port=${MP_PORT:-1922}; mp_name=${MP_NAME:-MP LAN Server}; mp_max=${MP_MAX:-100}
samp_port=${SAMP_PORT:-7777}; samp_max=${SAMP_MAX:-50}; region=${REGION:-Asia}

if [ ! -x ../server/mp_server ]; then
    CONFIG=${config} DISTRO=${distro} ./build.sh || exit 1
fi
mkdir -p run_${distro}/mp
cp ../server/mp_server ../server/run_24_7.sh run_${distro}/mp/ || { echo 'Error copying the MP server for the container build, exiting.'; exit 1; }
docker build -t mp/run:${distro} run_${distro}/
docker_error=$?
rm -rf run_${distro}/mp
[ $docker_error -ne 0 ] && { echo 'Error building the MP container, exiting.'; exit 1; }

[ ! -d './data' ] && mkdir data
ports=(-p ${mp_port}:${mp_port}/udp)
[ "$samp_port" != 0 ] && ports+=(-p ${samp_port}:${samp_port}/udp)
docker run \
    --rm \
    -ti \
    -w /data \
    -v "$PWD/data":/data \
    "${ports[@]}" \
    -e MP_PORT="${mp_port}" -e MP_NAME="${mp_name}" -e MP_MAX="${mp_max}" \
    -e SAMP_PORT="${samp_port}" -e SAMP_MAX="${samp_max}" -e REGION="${region}" \
    mp/run:${distro}
