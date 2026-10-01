#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
# Bluetooth control needs the EP0-capable PSP kernel bridge as well as the
# application. Always ship the three together, including firmware-only updates.
for file in ../psp-client/EBOOT.PBP ../psp-client/PSPStreamer.prx ../psp-client/streammaster_usb/StreamMasterUSB.prx; do
    test -s "$file"
done
mkdir -p release/PSPStreamer
cp ../psp-client/EBOOT.PBP ../psp-client/PSPStreamer.prx ../psp-client/streammaster_usb/StreamMasterUSB.prx release/PSPStreamer/
mode_selection="${1:-qio80}"
case "$mode_selection" in
    all) modes=(dio qio qio80);;
    dio|qio|qio80|qio80-iram) modes=("$mode_selection");;
    *) echo "Usage: $0 [all|dio|qio|qio80]" >&2; exit 2;;
esac
for mode in "${modes[@]}"; do
    build_dir=build-bluetooth
    flash_freq=40m
    [[ "$mode" == dio ]] || build_dir="build-bluetooth-$mode"
    [[ "$mode" != qio80* ]] || flash_freq=80m
    release_dir="release/StreamMaster-Bluetooth-${mode^^}"
    for file in bootloader/bootloader.bin partition_table/partition-table.bin streammaster_onju_v3.bin flash_args; do
        test -s "$build_dir/$file"
        mkdir -p "$release_dir/$(dirname "$file")"
        cp "$build_dir/$file" "$release_dir/$file"
    done
    mkdir -p "$release_dir/licenses"
    cp BLUETOOTH.md CHANGELOG.md GPL-3.0.txt "$release_dir/"
    cp "${IDF_PATH:?Activate ESP-IDF first}/LICENSE" "$release_dir/ESP-IDF-license.txt"
    cp "$IDF_PATH/components/lwip/lwip/COPYING" "$release_dir/licenses/lwip.txt"
    cp "$IDF_PATH/components/freertos/FreeRTOS-Kernel/LICENSE.md" "$release_dir/licenses/FreeRTOS.txt"
    cp "$IDF_PATH/components/mbedtls/mbedtls/LICENSE" "$release_dir/licenses/mbedTLS.txt"
    cp "$IDF_PATH/components/esp_wifi/lib/LICENSE" "$release_dir/licenses/esp-wifi.txt"
    cp "$IDF_PATH/components/esp_phy/lib/LICENSE" "$release_dir/licenses/esp-phy.txt"
    cp "$IDF_PATH/components/wpa_supplicant/COPYING" "$release_dir/licenses/wpa-supplicant.txt"
    cp "$IDF_PATH/components/bt/common/tinycrypt/LICENSE" "$release_dir/licenses/bt-tinycrypt.txt"
    git rev-parse HEAD > "$release_dir/SOURCE_REVISION.txt"
    git status --short > "$release_dir/SOURCE_STATUS.txt"
    # QIO is enabled by the compiled bootloader; its initial image header is DIO.
    python -m esptool --chip esp32s3 merge_bin --flash_mode dio --flash_size 16MB --flash_freq "$flash_freq" \
        -o "$release_dir/streammaster-onju-v3-factory.bin" \
        0x0 "$build_dir/bootloader/bootloader.bin" 0x8000 "$build_dir/partition_table/partition-table.bin" \
        0x10000 "$build_dir/streammaster_onju_v3.bin"
    (cd "$release_dir" && sha256sum bootloader/bootloader.bin partition_table/partition-table.bin ./*.bin > SHA256SUMS)
done
# Published Onju default: tested QIO 80 MHz image.
if [[ "$mode_selection" == all || "$mode_selection" == qio80 ]]; then
    mkdir -p release/StreamMaster-Onju-V3
    cp -a release/StreamMaster-Bluetooth-QIO80/. release/StreamMaster-Onju-V3/
fi
