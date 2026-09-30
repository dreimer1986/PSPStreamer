#!/usr/bin/env bash
# Activate ESP-IDF v5.5.1 first. Separate configs keep the stable build intact.
set -euo pipefail
cd "$(dirname "$0")"
mode_selection="${1:-qio}"
case "$mode_selection" in
    qio|dio|qio80) modes=("$mode_selection");;
    all) modes=(qio dio);;
    *) echo "Usage: $0 [qio|dio|qio80|all] (default: qio, all: stable variants)" >&2; exit 2;;
esac
for mode in "${modes[@]}"; do
    build_dir="build-bluetooth"
    defaults="sdkconfig.defaults;sdkconfig.bluetooth"
    qio=OFF
    flash80=OFF
    if [[ "$mode" == qio || "$mode" == qio80 ]]; then
        build_dir="build-bluetooth-$mode"
        defaults="$defaults;sdkconfig.qio"
        qio=ON
    fi
    if [[ "$mode" == qio80 ]]; then
        defaults="$defaults;sdkconfig.flash80"
        flash80=ON
    fi
    idf.py -B "$build_dir" -D "SDKCONFIG=$build_dir/sdkconfig" \
        -D "SDKCONFIG_DEFAULTS=$defaults" -D STREAMMASTER_BLUETOOTH=ON \
        -D "STREAMMASTER_QIO=$qio" -D "STREAMMASTER_FLASH80=$flash80" build
done
