# docker/ (5.02)

Same layout as [open.mp's docker folder](https://github.com/openmultiplayer/open.mp/tree/master/docker), for the MP server
(`server/mp_server`, with the built-in open.mp / SA-MP crossplay). Both Ubuntu 26.04 and Debian 12 are here.

| Folder / script | What it does |
|---|---|
| `build.sh` | Builds `server/mp_server` in a `build_<distro>` container (repo mounted at `/code`). `DISTRO=debian-12`, `UBUNTU_VERSION=26.04` (default), `CONFIG=Debug`, `BUILD_TESTS=1` (+ `test/sampgate_test`) |
| `run.sh` | Builds if needed, then runs the server in a `run_<distro>` container. UDP 1922 = phones, UDP 7777 = PC players (SA-MP 0.3.7 / open.mp launcher). `MP_PORT`, `MP_NAME`, `MP_MAX`, `SAMP_PORT` (0 = off), `SAMP_MAX`, `REGION`. Data in `docker/data/` |
| `build_ubuntu-26.04/`, `build_debian-12/` | Build images (g++, make) + `docker-entrypoint.sh` |
| `run_ubuntu-26.04/`, `run_debian-12/` | Small run images |
| `omp/run_omp.sh` | A real open.mp server (official Linux build, 32-bit, i386 runtimes) in Ubuntu 26.04 / Debian 12, for crossplay tests (`OMP_PORT`, `OMP_VERSION`) |
| `iso/get_iso.sh` | Downloads `ubuntu-26.04.1-live-server-amd64.iso` (the [archive.org](https://archive.org/details/ubuntu-26.04.1-live-server-amd64) one): from the wagu release v4.13.45 mirror (2 parts, SHA-256), or archive.org (SHA-1) |
| `iso/run_qemu.sh` | Boots that ISO in QEMU (KVM when available) with a disk; UDP 1922 / 7777 and SSH (2222) forwarded |

```
cd docker
./build.sh                         # Ubuntu 26.04
DISTRO=debian-12 ./build.sh        # Debian 12
./run.sh                           # MP LAN Server on 1922 + PC players on 7777
omp/run_omp.sh                     # an open.mp server on 7777 (for testing, use SAMP_PORT=7778 ./run.sh next to it)
BUILD_TESTS=1 ./build.sh && ../test/sampgate_test 127.0.0.1 7777
iso/get_iso.sh && iso/run_qemu.sh  # full Ubuntu 26.04.1 VM
```

`run.sh` uses an existing `server/mp_server`: build it with `build.sh` for the same distro (a binary built on a newer
system may not start in the Debian 12 image).
