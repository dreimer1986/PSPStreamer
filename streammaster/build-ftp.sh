#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
baseline="$repo/streammaster/build-bluetooth-qio80-iram/sdkconfig"
config="$repo/streammaster/build-bluetooth-ftp/sdkconfig"
if [[ ! -f "$config" ]]; then
    [[ -f "$baseline" ]] || { echo 'Build the stable Onju Bluetooth QIO80/IRAM firmware first.' >&2; exit 1; }
    mkdir -p "$(dirname -- "$config")"
    cp -- "$baseline" "$config"
fi
idf.py -C "$repo/streammaster" -B "$repo/build-bluetooth-ftp" \
    -D "SDKCONFIG=$config" -D STREAMMASTER_BLUETOOTH=ON \
    -D STREAMMASTER_QIO=ON -D STREAMMASTER_FLASH80=ON \
    -D STREAMMASTER_HOT_IRAM=ON -D STREAMMASTER_FTP=ON build
