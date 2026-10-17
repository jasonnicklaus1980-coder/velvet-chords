#!/bin/sh
# One-command ARM build + emulated test + release zip, using Docker (any PC/Mac with Docker installed).
#   sh scripts/build-arm.sh
# Debian bullseye's toolchain keeps glibc symbol versions old enough for Gen1 MPC OS; scripts/check_so.sh enforces it.
set -e
cd "$(dirname "$0")/.."
docker run --rm -v "$PWD":/src -w /src debian:bullseye sh -c '
  apt-get update -qq &&
  apt-get install -y -qq gcc gcc-arm-linux-gnueabihf libc6-dev-armhf-cross binutils-arm-linux-gnueabihf make python3 zip qemu-user >/dev/null &&
  make clean && make test && make arm-test && make release'
