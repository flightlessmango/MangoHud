#!/usr/bin/env bash
# Usage example: $0 master soldier 0.20210618.0
set -u

if [ $# -eq 2 ]; then
  echo Specify runtime version too
  exit 1
fi

SRCDIR=$PWD
BRANCH="${1:-master}"
# soldier 0.20210618.0 or newer
# scout 0.20210630.0 or newer
RUNTIME="${2:-soldier}"
VERSION="${3:-0.20210618.0}"
IMAGE="steamrt_${RUNTIME}_${VERSION}_amd64:mango-${RUNTIME}"
BASEURL="https://repo.steampowered.com/steamrt-images-${RUNTIME}/snapshots/${VERSION}"
CACHEDIR="./cache/steamrt-images-${RUNTIME}/snapshots/${VERSION}"

mkdir -p "${CACHEDIR}"

echo -e "\e[1mBuilding branch \e[92m${BRANCH}\e[39m using \e[92m${RUNTIME}:${VERSION}\e[39m runtime\e[0m"

if ! docker inspect --type=image ${IMAGE} 2>&1 >/dev/null ; then
  rm -fr ./cache/empty
  set -e
  mkdir -p ./cache/empty
  sed "s/%RUNTIME%/${RUNTIME}/g" steamrt.Dockerfile.in  > ./cache/steamrt.Dockerfile

  wget -P "${CACHEDIR}" -c ${BASEURL}/com.valvesoftware.SteamRuntime.Sdk-amd64,i386-${RUNTIME}-sysroot.tar.gz
  cp --reflink=always "${CACHEDIR}/com.valvesoftware.SteamRuntime.Sdk-amd64,i386-${RUNTIME}-sysroot.tar.gz" ./cache/empty/
  docker build -f ./cache/steamrt.Dockerfile -t ${IMAGE} ./cache/empty
fi

docker run --entrypoint=/bin/sh --rm -i -v "${SRCDIR}/srt-output:/output" ${IMAGE} << EOF
export RUNTIME=${RUNTIME}
export SRT_VERSION=${VERSION}
git clone git://github.com/flightlessmango/MangoHud.git . --branch ${BRANCH} --recurse-submodules --progress
./build-srt.sh clean build package release
cp -v build-srt/MangoHud*tar.gz /output/
EOF
