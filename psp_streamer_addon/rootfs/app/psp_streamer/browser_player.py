"""Browser muxing, deliberately separate from the PSP decoder contract."""

import re


def browser_command(command: list[str], audio_only: bool) -> list[str]:
    """Reuse track/subtitle/source handling, not PSP audio or frame-rate limits."""
    result = list(command)
    if audio_only:
        return result
    result[result.index('-c:a') + 1] = 'aac'
    # AAC does not use LAME's VBR quality scale.
    if '-q:a' in result:
        index = result.index('-q:a')
        result[index:index + 2] = ['-b:a', '160k']
    index = result.index('-flvflags')
    result[index:index + 2] = ['-movflags', '+frag_keyframe+empty_moov+default_base_moof']
    result[result.index('-f') + 1] = 'mp4'
    for flag in ('-vf', '-filter_complex'):
        if flag in result:
            index = result.index(flag) + 1
            result[index] = re.sub(r'fps=[0-9/]+,', '', result[index])
    # Short fragments avoid several seconds of startup wait. Keep source FPS.
    index = result.index('-x264-params')
    result[index + 1] = 'keyint=30:min-keyint=30:scenecut=0:bframes=0:cabac=1:weightp=0'
    return result
