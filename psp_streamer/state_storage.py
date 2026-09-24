"""Durable catalogue settings, separate from the password-management policy."""
import os
from pathlib import Path
import tempfile


def state_directory():
    directory = Path(os.environ.get('PSP_STREAMER_SETTINGS_DIR') or
                     os.environ.get('PSP_STREAMER_STATE_DIR') or
                     str(Path.home() / '.cache/psp-streamer'))
    directory.mkdir(parents=True, exist_ok=True)
    legacy = os.environ.get('PSP_STREAMER_LEGACY_STATE_DIR')
    if legacy and Path(legacy).resolve() != directory.resolve():
        # HA used its disposable container cache for these files. Preserve any
        # surviving files on restart; never replace settings already in /data.
        for name in ('plex.json', 'jellyfin.json', 'player-id.txt', 'radio.json'):
            source, target = Path(legacy) / name, directory / name
            if target.exists() or not source.is_file():
                continue
            fd, temporary = tempfile.mkstemp(prefix='.migrate-', dir=directory)
            try:
                with os.fdopen(fd, 'wb') as output:
                    output.write(source.read_bytes())
                    output.flush()
                    os.fsync(output.fileno())
                try:
                    os.link(temporary, target)  # Atomic, and never overwrites.
                except FileExistsError:
                    pass
            finally:
                os.unlink(temporary)
    return directory
