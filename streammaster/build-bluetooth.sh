#!/usr/bin/env bash
# Activate ESP-IDF v5.5.1 first. Separate configs keep the stable build intact.
set -euo pipefail
cd "$(dirname "$0")"
mode_selection="${1:-qio80-iram}"
case "$mode_selection" in
    qio|dio|qio80|qio80-iram) modes=("$mode_selection");;
    all) modes=(qio80-iram qio80 qio dio);;
    *) echo "Usage: $0 [qio|dio|qio80|qio80-iram|all] (default: qio80-iram)" >&2; exit 2;;
esac
for mode in "${modes[@]}"; do
    build_dir="build-bluetooth"
    defaults="sdkconfig.defaults;sdkconfig.bluetooth"
    qio=OFF
    flash80=OFF
    hot_iram=OFF
    if [[ "$mode" == qio* ]]; then
        build_dir="build-bluetooth-$mode"
        defaults="$defaults;sdkconfig.qio"
        qio=ON
    fi
    if [[ "$mode" == qio80* ]]; then
        defaults="$defaults;sdkconfig.flash80"
        flash80=ON
    fi
    [[ "$mode" != qio80-iram ]] || hot_iram=ON
    idf.py -B "$build_dir" -D "SDKCONFIG=$build_dir/sdkconfig" \
        -D "SDKCONFIG_DEFAULTS=$defaults" -D STREAMMASTER_BLUETOOTH=ON \
        -D "STREAMMASTER_QIO=$qio" -D "STREAMMASTER_FLASH80=$flash80" \
        -D "STREAMMASTER_HOT_IRAM=$hot_iram" build
done
