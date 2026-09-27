#!/usr/bin/env bash
# Run after building with ESP-IDF v5.5.1 and activating its environment.
set -euo pipefail
cd "$(dirname "$0")"
for file in build/bootloader/bootloader.bin build/partition_table/partition-table.bin build/streammaster_onju_v3.bin; do
    test -s "$file"
done
mkdir -p release/StreamMaster-Onju-V3
cp build/bootloader/bootloader.bin release/StreamMaster-Onju-V3/
cp build/partition_table/partition-table.bin release/StreamMaster-Onju-V3/
cp build/streammaster_onju_v3.bin README.md CHANGELOG.md PSPLINK-license.txt release/StreamMaster-Onju-V3/
cp ../LICENSE release/StreamMaster-Onju-V3/GPL-2.0.txt
cp "${IDF_PATH:?Activate ESP-IDF first}/LICENSE" release/StreamMaster-Onju-V3/ESP-IDF-license.txt
mkdir -p release/StreamMaster-Onju-V3/licenses
cp "$IDF_PATH/components/lwip/lwip/COPYING" release/StreamMaster-Onju-V3/licenses/lwip.txt
cp "$IDF_PATH/components/freertos/FreeRTOS-Kernel/LICENSE.md" release/StreamMaster-Onju-V3/licenses/FreeRTOS.txt
cp "$IDF_PATH/components/mbedtls/mbedtls/LICENSE" release/StreamMaster-Onju-V3/licenses/mbedTLS.txt
cp "$IDF_PATH/components/esp_wifi/lib/LICENSE" release/StreamMaster-Onju-V3/licenses/esp-wifi.txt
cp "$IDF_PATH/components/esp_phy/lib/LICENSE" release/StreamMaster-Onju-V3/licenses/esp-phy.txt
cp "$IDF_PATH/components/wpa_supplicant/COPYING" release/StreamMaster-Onju-V3/licenses/wpa-supplicant.txt
python -m esptool --chip esp32s3 merge_bin --flash_mode dio --flash_size 16MB --flash_freq 40m \
    -o release/StreamMaster-Onju-V3/streammaster-onju-v3-factory.bin \
    0x0 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin \
    0x10000 build/streammaster_onju_v3.bin
cd release/StreamMaster-Onju-V3
sha256sum ./*.bin > SHA256SUMS
cd ..
zip -q -r StreamMaster-Onju-V3.zip StreamMaster-Onju-V3
