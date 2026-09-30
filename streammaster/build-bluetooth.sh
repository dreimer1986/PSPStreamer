#!/usr/bin/env bash
# Activate ESP-IDF v5.5.1 first. Separate configs keep the stable build intact.
set -euo pipefail
cd "$(dirname "$0")"
for mode in dio qio; do
    build_dir="build-bluetooth"
    defaults="sdkconfig.defaults;sdkconfig.bluetooth"
    qio=OFF
    if [[ "$mode" == qio ]]; then
        build_dir=build-bluetooth-qio
        defaults="$defaults;sdkconfig.qio"
        qio=ON
    fi
    idf.py -B "$build_dir" -D "SDKCONFIG=$build_dir/sdkconfig" \
        -D "SDKCONFIG_DEFAULTS=$defaults" -D STREAMMASTER_BLUETOOTH=ON \
        -D "STREAMMASTER_QIO=$qio" build
done
