"""Reuse successful local ffprobe results, never remote/live URLs."""
from pathlib import Path
import subprocess
from .work_cache import WorkCache

_cache = WorkCache(entries=32, jobs=8)


def file_identity(source):
    if not isinstance(source, Path):
        return None
    try:
        stat = source.stat()
        if not source.is_file():
            return None
        return (str(source.resolve()), stat.st_dev, stat.st_ino, stat.st_size,
                stat.st_mtime_ns, stat.st_ctime_ns)
    except OSError:
        return None


def probe(source, arguments, timeout=20):
    def run():
        return subprocess.run(['ffprobe','-v','error',*arguments,str(source)],
                              capture_output=True,text=True,timeout=timeout,check=False)
    identity = file_identity(source)
    if identity is None:
        return run()
    return _cache.get((identity, tuple(arguments)), run,
                      cache_if=lambda result: result.returncode == 0 and len(result.stdout)+len(result.stderr) < 256*1024)
