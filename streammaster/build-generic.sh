#!/usr/bin/env bash
# Activate ESP-IDF v5.5.1 first. Never touches the Onju build/sdkconfig.
set -euo pipefail
cd "$(dirname "$0")"
variant=${1:?Use s3-quad, s3-octal or s2}
case "$variant" in
    s3-quad|s3-octal) target=esp32s3; boot_offset=0x0;;
    s2) target=esp32s2; boot_offset=0x1000;;
    *) echo "Unsupported generic variant" >&2; exit 1;;
esac
build_dir="build-generic-$variant"
flash_profile=${2:-dio40}
case "$flash_profile" in
    dio40) flash_freq=40m; suffix="";;
    qio80) flash_freq=80m; suffix="-QIO80"; build_dir="$build_dir-qio80";;
    *) echo "Flash profile must be dio40 or qio80" >&2; exit 1;;
esac
defaults="$PWD/sdkconfig.generic"
if [ "$target" = esp32s3 ]; then defaults="$defaults;$PWD/sdkconfig.generic-$variant"; fi
if [ "$flash_profile" = qio80 ]; then defaults="$defaults;$PWD/sdkconfig.qio;$PWD/sdkconfig.flash80"; fi
idf.py -B "$build_dir" -DIDF_TARGET="$target" \
    -DSTREAMMASTER_BOARD=generic -DSTREAMMASTER_O3=OFF \
    -DSTREAMMASTER_BLUETOOTH=OFF -DSTREAMMASTER_HOT_IRAM=OFF \
    -DSTREAMMASTER_QIO=$([ "$flash_profile" = qio80 ] && echo ON || echo OFF) \
    -DSTREAMMASTER_FLASH80=$([ "$flash_profile" = qio80 ] && echo ON || echo OFF) \
    -DSDKCONFIG="$PWD/$build_dir/sdkconfig" -DSDKCONFIG_DEFAULTS="$defaults" build
name="StreamMaster-ESP32-${variant^^}${suffix}-UNTESTED"
package="release/$name"
mkdir -p "$package"
cp "$build_dir/bootloader/bootloader.bin" "$build_dir/partition_table/partition-table.bin" "$package/"
cp "$build_dir/streammaster_onju_v3.bin" "$package/streammaster-$variant-untested-app.bin"
cp GENERIC.md "$package/README.md"
cp CHANGELOG.md INTEGRATION.md PSPLINK-license.txt ../LICENSE "$package/"
cp "$IDF_PATH/LICENSE" "$package/ESP-IDF-license.txt"
mkdir -p "$package/licenses"
cp "$IDF_PATH/components/lwip/lwip/COPYING" "$package/licenses/lwip.txt"
cp "$IDF_PATH/components/freertos/FreeRTOS-Kernel/LICENSE.md" "$package/licenses/FreeRTOS.txt"
cp "$IDF_PATH/components/mbedtls/mbedtls/LICENSE" "$package/licenses/mbedTLS.txt"
cp "$IDF_PATH/components/esp_wifi/lib/LICENSE" "$package/licenses/esp-wifi.txt"
cp "$IDF_PATH/components/esp_phy/lib/LICENSE" "$package/licenses/esp-phy.txt"
cp "$IDF_PATH/components/wpa_supplicant/COPYING" "$package/licenses/wpa-supplicant.txt"
python -m esptool --chip "$target" merge_bin --flash_mode dio --flash_size 4MB --flash_freq "$flash_freq" \
    -o "$package/streammaster-$variant-untested-factory.bin" \
    "$boot_offset" "$build_dir/bootloader/bootloader.bin" \
    0x8000 "$build_dir/partition_table/partition-table.bin" \
    0x10000 "$build_dir/streammaster_onju_v3.bin"
(cd "$package" && sha256sum ./*.bin > SHA256SUMS)
(cd release && zip -q -r "$name.zip" "$name")
