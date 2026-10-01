#!/usr/bin/env bash
# Run after building with ESP-IDF v5.5.1 and activating its environment.
set -euo pipefail
cd "$(dirname "$0")"
if [ "$#" -eq 0 ]; then exec bash package-bluetooth.sh qio80; fi
build_dir=${1:-build}
release_name=${2:-StreamMaster-Onju-V3}
case "$release_name" in *[!a-zA-Z0-9_-]*|'') echo "Invalid release directory name" >&2; exit 1;; esac
for file in "$build_dir/bootloader/bootloader.bin" "$build_dir/partition_table/partition-table.bin" "$build_dir/streammaster_onju_v3.bin"; do
    test -s "$file"
done
release_dir="release/$release_name"
mkdir -p "$release_dir/licenses"
cp "$build_dir/bootloader/bootloader.bin" "$build_dir/partition_table/partition-table.bin" "$build_dir/streammaster_onju_v3.bin" "$release_dir/"
cp README.md GENERIC.md INTEGRATION.md CHANGELOG.md PSPLINK-license.txt "$release_dir/"
cp ../LICENSE "$release_dir/GPL-2.0.txt"
cp "${IDF_PATH:?Activate ESP-IDF first}/LICENSE" "$release_dir/ESP-IDF-license.txt"
cp "$IDF_PATH/components/lwip/lwip/COPYING" "$release_dir/licenses/lwip.txt"
cp "$IDF_PATH/components/freertos/FreeRTOS-Kernel/LICENSE.md" "$release_dir/licenses/FreeRTOS.txt"
cp "$IDF_PATH/components/mbedtls/mbedtls/LICENSE" "$release_dir/licenses/mbedTLS.txt"
cp "$IDF_PATH/components/esp_wifi/lib/LICENSE" "$release_dir/licenses/esp-wifi.txt"
cp "$IDF_PATH/components/esp_phy/lib/LICENSE" "$release_dir/licenses/esp-phy.txt"
cp "$IDF_PATH/components/wpa_supplicant/COPYING" "$release_dir/licenses/wpa-supplicant.txt"
python -m esptool --chip esp32s3 merge_bin --flash_mode dio --flash_size 16MB --flash_freq 40m \
    -o "$release_dir/streammaster-onju-v3-factory.bin" \
    0x0 "$build_dir/bootloader/bootloader.bin" 0x8000 "$build_dir/partition_table/partition-table.bin" \
    0x10000 "$build_dir/streammaster_onju_v3.bin"
cd "$release_dir"
sha256sum ./*.bin > SHA256SUMS
cd ..
zip -q -r "$release_name.zip" "$release_name"
