#!/bin/sh
# Builds WLED with the domlamp usermod. WLED itself is not part of this repository: it is cloned at a
# fixed release next to it (default ~/.cache/domlamp-fw/WLED, or $DOMLAMP_WLED_DIR).
#
#   ./build.sh                       the ESP32 build
#   ./build.sh domlamp_esp32c3       another environment from platformio_override.ini
#   ./build.sh domlamp_esp32 -t upload --upload-port /dev/cu.usbserial-XXXX
#
# Needs: git, node/npm (WLED builds its web UI), PlatformIO (`uv tool install platformio`).
set -eu
WLED_TAG=v16.0.1
here=$(cd "$(dirname "$0")" && pwd)
wled=${DOMLAMP_WLED_DIR:-$HOME/.cache/domlamp-fw/WLED}
env=${1:-domlamp_esp32}
[ $# -gt 0 ] && shift

if [ ! -d "$wled/.git" ]; then
  mkdir -p "$(dirname "$wled")"
  git clone --depth 1 --branch "$WLED_TAG" https://github.com/wled/WLED "$wled"
fi
have=$(git -C "$wled" describe --tags --exact-match 2>/dev/null || echo unknown)
if [ "$have" != "$WLED_TAG" ]; then
  echo "WLED in $wled is at '$have', expected $WLED_TAG" >&2
  exit 1
fi
[ -d "$wled/node_modules" ] || (cd "$wled" && npm ci)

# WLED 16 takes a usermod as a PlatformIO library in usermods/<name>, chosen with custom_usermods.
ln -sfn "$here/usermod_domlamp" "$wled/usermods/domlamp"
cp "$here/platformio_override.ini" "$wled/platformio_override.ini"

(cd "$wled" && pio run -e "$env" "$@")

mkdir -p "$here/build"
cp "$wled/.pio/build/$env/firmware.bin" "$here/build/$env.bin"
echo "firmware: $here/build/$env.bin"
