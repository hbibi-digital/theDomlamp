#!/bin/sh
# Builds the two files of a release for the Seeed Studio XIAO ESP32-C3 into build/release/:
#   theDomlamp-<version>-xiao-esp32c3-factory.bin   bootloader, partition table and firmware in one image,
#                                                   for a first flash at address 0x0
#   theDomlamp-<version>-xiao-esp32c3-update.bin    the firmware alone, for WLED's own "Manual OTA Update"
# and SHA256SUMS.txt. The version is DOMLAMP_VERSION in platformio_override.ini.
# Needs what build.sh needs. Publishing the files (a tag, a release) stays a step by hand.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
wled=${DOMLAMP_WLED_DIR:-$HOME/.cache/domlamp-fw/WLED}
env=domlamp_esp32c3
version=$(sed -n "s/.*-D DOMLAMP_VERSION='\"\([^\"]*\)\"'.*/\1/p" "$here/platformio_override.ini")
[ -n "$version" ] || { echo "no DOMLAMP_VERSION in platformio_override.ini" >&2; exit 1; }
sh "$here/test/run.sh"
"$here/build.sh" "$env"
build=$wled/.pio/build/$env
pio_home=${PLATFORMIO_CORE_DIR:-$HOME/.platformio}
out=$here/build/release
mkdir -p "$out"
name=theDomlamp-$version-xiao-esp32c3
# the layout the framework itself flashes: bootloader 0x0, partitions 0x8000, boot_app0 0xe000, firmware 0x10000
pio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32c3 merge_bin -o "$out/$name-factory.bin" \
  --flash_mode keep --flash_freq keep --flash_size keep \
  0x0 "$build/bootloader.bin" 0x8000 "$build/partitions.bin" \
  0xe000 "$pio_home/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin" 0x10000 "$build/firmware.bin"
cp "$build/firmware.bin" "$out/$name-update.bin"
(cd "$out" && shasum -a 256 "$name-factory.bin" "$name-update.bin" > SHA256SUMS.txt && cat SHA256SUMS.txt)
