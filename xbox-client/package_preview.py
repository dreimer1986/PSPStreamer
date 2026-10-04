"""Complete the existing bin/ preview in place, without another release copy."""
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parent
sdk = root.parent / '.toolchain/nxdk'
out = root / 'bin'
if not (out / 'default.xbe').is_file():
    raise SystemExit('Build the XBE first')
for name in ('README.md', 'server.cfg.example'):
    shutil.copy2(root / name, out / name)
(out / 'source').mkdir(exist_ok=True)
for name in ('player_main.c', 'video_backend.c', 'video_overlay.h', 'yuy2_pack.h', 'net.h', 'http_status.h', 'catalog.h', 'audio.h', 'player.h', 'mpeg_video.h', 'display.h', 'settings.h', 'report.h', 'remote.h', 'server_settings.h', 'spectrum.h', 'render_clip.h', 'Makefile', 'server.cfg.example', 'README.md', 'package_preview.py'):
    shutil.copy2(root / name, out / 'source' / name)
shutil.copytree(root / 'vendor', out / 'source/vendor', dirs_exist_ok=True, ignore=shutil.ignore_patterns('*.obj','*.d'))
(out / 'source/shared').mkdir(exist_ok=True)
for name in ('spectrum_analysis.h','spectrum_analysis_impl.h','spectrum_paint.h','monkey_audio.h'):
    shutil.copy2(root.parent / 'psp-client' / name, out / 'source/shared' / name)
shutil.copytree(root / 'tools', out / 'source/tools', dirs_exist_ok=True, ignore=shutil.ignore_patterns('__pycache__'))
shutil.copy2(root.parent / 'psp-client/assets/menu_skin_tv.png', out / 'theme.png')
shutil.copy2('/usr/share/fonts/TTF/DejaVuSans.ttf', out / 'font.ttf')
# Replace the obsolete connection-only source in this generated package.
old_source = out / 'source/main.c'
if old_source.exists():
    old_source.unlink()
licenses = out / 'licenses'
licenses.mkdir(exist_ok=True)
shutil.copy2(root.parent / 'licenses/MilkDrop2.txt', licenses / 'MilkDrop2.txt')
for source, name in (
    (root.parent / 'LICENSE', 'GPL-2.0.txt'),
    (root.parent / 'NOTICE', 'PSPStreamer-NOTICE.txt'),
    (sdk / 'LICENSES/MIT.txt', 'nxdk-MIT.txt'),
    (sdk / 'LICENSES/CC0-1.0.txt', 'nxdk-CC0.txt'),
    (sdk / 'LICENSES/NCSA.txt', 'compiler-rt-NCSA.txt'),
    (sdk / 'lib/pdclib/COPYING.CC0', 'PDCLib-CC0.txt'),
    (sdk / 'lib/net/lwip/COPYING', 'lwIP.txt'),
    (sdk / 'lib/sdl/SDL2/COPYING.txt', 'SDL2.txt'),
    (sdk / 'lib/sdl/SDL_ttf/COPYING.txt', 'SDL_ttf.txt'),
    (sdk / 'lib/sdl/SDL2_image/COPYING.txt', 'SDL_image.txt'),
    (sdk / 'lib/sdl/SDL_ttf/external/freetype-2.4.12/docs/FTL.TXT', 'FreeType.txt'),
    (sdk / 'lib/libpng/libpng/LICENSE', 'libpng.txt'),
    (sdk / 'lib/zlib/zlib/LICENSE', 'zlib.txt'),
    (Path('/usr/share/licenses/ttf-dejavu/LICENSE'), 'DejaVu.txt'),
    (Path('/usr/share/licenses/spdx/GPL-3.0-only.txt'), 'GPL-3.0.txt'),
    (sdk / 'lib/usb/libusbohci/LICENSE.txt', 'USB-Apache-2.0.txt'),
    (sdk / 'lib/usb/libusbohci/DISCLAIMER.md', 'USB-DISCLAIMER.md'),
):
    shutil.copy2(source, licenses / name)
# Preserve SPDX copyright/license notices with the small nxdk glue sources.
for folder in ('hal', 'nxdk', 'xboxrt', 'winapi', 'usb', 'net'):
    for source in (sdk / 'lib' / folder).rglob('*'):
        if source.is_file() and source.suffix in ('.c', '.cpp', '.h', '.S') and not any(
                excluded in source.relative_to(sdk / 'lib').parts for excluded in ('lwip', 'libusbohci')):
            target = out / 'source/nxdk-notices' / source.relative_to(sdk / 'lib')
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
revision = subprocess.check_output(['git', '-C', str(sdk), 'rev-parse', 'HEAD'], text=True).strip()
# The app's lifecycle adapter compiles this exact upstream SDL source.
driver_notice = out / 'source/nxdk-notices/sdl'
driver_notice.mkdir(parents=True, exist_ok=True)
shutil.copy2(sdk / 'lib/sdl/SDL2/src/video/xbox/SDL_xbvideo.c', driver_notice / 'SDL_xbvideo.c')
(out / 'SDK-REVISION.txt').write_text('https://github.com/XboxDev/nxdk\n' + revision + '\n', encoding='utf-8')
print(out)
