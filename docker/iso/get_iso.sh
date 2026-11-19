#!/bin/bash
# 5.02: downloads the Ubuntu 26.04.1 live server ISO (amd64, 2.9 GB) into docker/iso/ - the same one as
# https://archive.org/details/ubuntu-26.04.1-live-server-amd64 - for a full VM (run_qemu.sh) next to the
# Docker images. Fast path: the 2 parts mirrored in the wagu release v4.13.45 (GitHub CDN), joined and
# checked with SHA-256. Fallback: archive.org directly (SHA-1 check).
#   ./get_iso.sh            mirror first, then archive.org
#   SOURCE=archive ./get_iso.sh
cd "$(dirname "$0")" || exit 1
NAME=ubuntu-26.04.1-live-server-amd64.iso
REL=https://github.com/ThirtyFoxMCx00E1/wagu/releases/download/v4.13.45
ARCHIVE=https://archive.org/download/ubuntu-26.04.1-live-server-amd64/$NAME
SHA256=cc8a95cde20f6ced61a322420de00f10cc3c90ced545daa46cb9c1a117f1d927
SHA1=b23aa96b8398cccf2204b4a435139be691c46785
if [ -f "$NAME" ] && echo "$SHA256  $NAME" | sha256sum -c - >/dev/null 2>&1; then echo "$NAME is already here (checked)."; exit 0; fi
ok=0
if [ "$SOURCE" != archive ]; then
    echo "Downloading $NAME from the wagu release mirror (2 parts)..."
    if curl -fL --retry 5 -C - -o "$NAME.part00" "$REL/$NAME.part00" && curl -fL --retry 5 -C - -o "$NAME.part01" "$REL/$NAME.part01"; then
        cat "$NAME.part00" "$NAME.part01" > "$NAME" && rm -f "$NAME.part00" "$NAME.part01"
        echo "$SHA256  $NAME" | sha256sum -c - && ok=1
    fi
fi
if [ $ok = 0 ]; then
    echo "Downloading $NAME from archive.org..."
    curl -fL --retry 5 -C - -o "$NAME" "$ARCHIVE" || exit 1
    echo "$SHA1  $NAME" | sha1sum -c - || { echo "Checksum mismatch - delete $NAME and try again."; exit 1; }
fi
echo "Ready: docker/iso/$NAME  (boot it: ./run_qemu.sh)"
