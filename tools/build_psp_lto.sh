#!/usr/bin/env bash
# Separate object tree: never mix LTO objects into the normal O3 build.
set -euo pipefail
project_root=$(cd -- "$(dirname -- "$0")/.." && pwd)
build_root=$(mktemp -d "${TMPDIR:-/tmp}/pspstreamer-lto.XXXXXX")
printf 'LTO work directory: %s\n' "$build_root"
rsync -a --exclude release --exclude '*.o' --exclude '*.elf' --exclude '*.prx' \
    --exclude '*.PBP' --exclude PARAM.SFO "$project_root/psp-client/" "$build_root/psp-client/"
ln -s "$project_root/streammaster" "$build_root/streammaster"
ln -s "$project_root/psp-overclock" "$build_root/psp-overclock"
make -C "$build_root/psp-client" -j4 LTO=1 OPT_LEVEL=-O3
if [[ $# -gt 0 ]]; then
    mkdir -p -- "$1"
    cp -- "$build_root/psp-client/EBOOT.PBP" "$build_root/psp-client/PSPStreamer.prx" "$1/"
fi
printf 'LTO artifacts: %s/psp-client (retained for inspection)\n' "$build_root"
