#!/usr/bin/env python3
"""One folder/ZIP per component, in a NEW staging directory. Never deletes.

Requires completed PSP builds and all four current ESP builds. Run under the
ESP-IDF Python environment. --seed supplies existing licensed runtime assets;
only an explicit asset allowlist is copied, never old binaries/configurations.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
COMPONENTS = ("PSPStreamer", "PSPConsolizer", "StreamerOC", "FuSaFullscreen", "StreamMaster")


def copy(source, dest):
    source = Path(source)
    if not source.is_file() or not source.stat().st_size:
        raise ValueError(f"Missing/empty release input: {source}")
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, dest)


def digest(path):
    with path.open("rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seed", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    out = args.output.resolve()
    if out.exists():
        raise SystemExit("Output must not exist: use a fresh staging path.")
    seed = args.seed.resolve()
    out.mkdir(parents=True)
    for name in COMPONENTS:
        (out / name).mkdir()
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True))
    version = re.search(r'set\(STREAMMASTER_VERSION "([^"]+)"\)',
                        (ROOT / "streammaster/CMakeLists.txt").read_text()).group(1)

    app = out / "PSPStreamer"
    for source, name in [
        ("psp-client/EBOOT.PBP", "EBOOT.PBP"),
        ("psp-client/PSPStreamer.prx", "PSPStreamer.prx"),
        ("psp-client/streammaster_usb/StreamMasterUSB.prx", "StreamMasterUSB.prx"),
        ("psp-client/assets/subtitle_font.raw", "subtitle_font.raw"),
        ("psp-client/PSPStreamer.cfg.example", "PSPStreamer.cfg.example"),
        ("README.md", "README.md"),
        ("psp-client/assets/cave_ship.CREDITS.md", "cave_ship.CREDITS.md"),
        ("psp-client/assets/ships/CREDITS.md", "Flight-models-CREDITS.md"),
        ("psp-client/assets/shield/CREDITS.md", "AstroShield-CREDITS.md"),
        ("streammaster/PSPLINK-license.txt", "PSPLINK-license.txt"),
        ("LICENSE", "LICENSE")]:
        copy(ROOT / source, app / name)
    for name in ("cooleyesBridge.prx", "dvemgr.prx", "MilkDrop2-license.txt", "PNG-zlib-license.txt"):
        copy(seed / "PSPStreamer" / name, app / name)
    for name in ("presets", "monkey"):
        shutil.copytree(seed / "PSPStreamer" / name, app / name)

    for folder, sources in {
        "PSPConsolizer": ["psp-controller/PSPConsolizer.prx",
            "psp-controller/bridge/PSPConsolizerUSB.prx", "psp-controller/PSPConsolizer.ini.example",
            "psp-controller/PSPConsolizer-rules.ini.example", "psp-controller/PSPConsolizer-rumble.ini.example",
            "psp-controller/RUMBLE.md", "psp-controller/README.md"],
        "StreamerOC": ["psp-overclock/StreamerOC.prx", "psp-overclock/StreamerOC.ini.example",
            "psp-overclock/StreamerOC-rules.ini.example", "psp-overclock/LICENSE", "psp-overclock/README.md"],
        "FuSaFullscreen": ["psp-fusa-probe/FuSaFullscreen.prx", "psp-fusa-probe/FULLSCREEN.md",
            "psp-fusa-probe/PLUGINS-fullscreen-example.TXT"]
    }.items():
        for source in sources:
            name = Path(source).name
            if name == "FULLSCREEN.md": name = "README.md"
            if name == "PLUGINS-fullscreen-example.TXT": name = "PLUGINS-example.TXT"
            copy(ROOT / source, out / folder / name)
    copy(ROOT / "psp-fusa-probe/FuSaFullscreen.ini", out / "FuSaFullscreen/FuSaFullscreen.ini.example")
    copy(seed / "PSPStreamer/dvemgr.prx", out / "FuSaFullscreen/dvemgr.prx")
    for folder in ("PSPConsolizer", "FuSaFullscreen"):
        copy(ROOT / "LICENSE", out / folder / "LICENSE")

    fw = out / "StreamMaster"
    for source in ("README.md", "GENERIC.md", "BLUETOOTH.md", "SPDIF.md", "INTEGRATION.md", "CHANGELOG.md", "GPL-3.0.txt", "PSPLINK-license.txt"):
        copy(ROOT / "streammaster" / source, fw / source)
    firmwares = {}
    builds = {
        "Onju-V3": "build-bluetooth-qio80-iram",
        "ESP32-S3-QUAD-UNTESTED": "build-generic-s3-quad-qio80",
        "ESP32-S3-OCTAL-UNTESTED": "build-generic-s3-octal-qio80",
        "ESP32-S2-UNTESTED": "build-generic-s2-qio80",
    }
    for name, build_name in builds.items():
        build = ROOT / "streammaster" / build_name
        desc = json.loads((build / "project_description.json").read_text())
        flash = json.loads((build / "flasher_args.json").read_text())
        image = (build / "streammaster_onju_v3.bin").read_bytes()
        if struct.unpack_from("<I", image, 32)[0] != 0xABCD5432:
            raise ValueError(f"Missing ESP app descriptor: {name}")
        embedded = image[48:80].split(b"\0")[0].decode()
        if embedded != desc["project_version"] or not embedded.startswith(version + "-"):
            raise ValueError(f"Stale firmware: {name}: {embedded}")
        config = (build / "sdkconfig").read_text()
        for required in ("CONFIG_ESPTOOLPY_FLASHMODE_QIO=y", "CONFIG_ESPTOOLPY_FLASHFREQ_80M=y", "CONFIG_SPIRAM_SPEED_80M=y", "CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y"):
            if required not in config: raise ValueError(f"Wrong hardware config: {name}: {required}")
        dest = fw / name
        dest.mkdir()
        pairs = []
        for offset, relative in sorted(flash["flash_files"].items(), key=lambda p: int(p[0], 0)):
            rel = Path(relative)
            if rel.is_absolute() or ".." in rel.parts: raise ValueError("Unsafe flash path")
            copy(build / rel, dest / rel)
            pairs += [offset, relative]
        options = flash["write_flash_args"]
        (dest / "flash_args").write_text(" ".join(options) + "\n" +
            "\n".join(f"{pairs[i]} {pairs[i+1]}" for i in range(0, len(pairs), 2)) + "\n")
        subprocess.run([sys.executable, "-m", "esptool", "--chip", desc["target"], "merge_bin",
                        *options, "-o", "factory.bin", *pairs], cwd=dest, check=True)
        status = "UNTESTED hardware variant" if "UNTESTED" in name else "Established Onju hardware configuration; new optical audio awaits hardware verification"
        (dest / "README.md").write_text(f"# {name}\n\nFirmware: `{embedded}`\n\n{status}.\n\n"
            f"From this folder, update with:\n\n```sh\nesptool --chip {desc['target']} --port /dev/ttyACM0 --baud 460800 write_flash @flash_args\n```\n\n"
            "This writes bootloader/partition table/app, preserving NVS profiles and bonds.\n"
            "`factory.bin` is for fresh installations at offset 0x0; it overwrites the NVS gap.\n"
            "The initial DIO image header is intentional; the bootloader enables QIO80.\n"
            "Read ../GENERIC.md before using a generic board. No generic Bluetooth support is claimed.\n")
        firmwares[name] = {"version": embedded, "target": desc["target"], "status": status,
                           "sdkconfig_sha256": digest(build / "sdkconfig")}
        idf = Path(desc["idf_path"])
    for source, target in {
        "LICENSE": "ESP-IDF.txt", "components/lwip/lwip/COPYING": "lwip.txt",
        "components/freertos/FreeRTOS-Kernel/LICENSE.md": "FreeRTOS.txt",
        "components/mbedtls/mbedtls/LICENSE": "mbedTLS.txt", "components/esp_wifi/lib/LICENSE": "esp-wifi.txt",
        "components/esp_phy/lib/LICENSE": "esp-phy.txt", "components/wpa_supplicant/COPYING": "wpa-supplicant.txt",
        "components/bt/common/tinycrypt/LICENSE": "bt-tinycrypt.txt",
    }.items(): copy(idf / source, fw / "licenses" / target)
    for source, target in [("docs/RELEASE_LAYOUT.md", "README.md"), ("docs/RELEASE_2.3.md", "RELEASE-2.3.md"), ("CHANGELOG.md", "CHANGELOG.md")]:
        copy(ROOT / source, out / target)
    todo = seed / "Probleme und Ideen.txt"
    if todo.exists(): copy(todo, out / todo.name)
    for name in COMPONENTS:
        folder = out / name
        paths = sorted(p for p in folder.rglob("*") if p.is_file())
        (folder / "SHA256SUMS").write_text("".join(f"{digest(p)}  {p.relative_to(folder).as_posix()}\n" for p in paths))
        with zipfile.ZipFile(out / f"{name}.zip", "w", zipfile.ZIP_DEFLATED) as archive:
            for p in sorted(folder.rglob("*")):
                if p.is_file(): archive.write(p, p.relative_to(out))
        with zipfile.ZipFile(out / f"{name}.zip") as archive:
            if archive.testzip(): raise ValueError(f"Corrupt archive: {name}")
    (out / "MANIFEST.json").write_text(json.dumps({"source_revision": revision, "dirty": dirty,
        "firmware_base": version, "firmwares": firmwares,
        "archives": {name: digest(out / f"{name}.zip") for name in COMPONENTS}}, indent=2) + "\n")
    print(f"Ready: {out}")


if __name__ == "__main__":
    main()
