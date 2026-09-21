"""Bounded transport of a complete text-subtitle display timeline."""
import heapq

PAGE_CUES = 256


def display_timeline(cues):
    """Preserve the PSP's first-active-cue rule, merging identical neighbours.

    ASS animation/karaoke exports often repeat the same stripped text thousands
    of times. Resolve overlaps once on the server, not in the PSP render loop.
    Each interval is disjoint, so page boundaries never discard an active cue.
    """
    rows = sorted(cues, key=lambda cue: cue[0])
    events = {}
    for index, (start, end, text) in enumerate(rows):
        if not 0 <= start < end <= 2147483647:
            continue
        # The client stores 159 UTF-8 bytes plus NUL, not 159 code points.
        text = text.encode('utf-8')[:159].decode('utf-8', errors='ignore')
        if not text:
            continue
        rows[index] = [start, end, text]
        events.setdefault(start, []).append(index)
        events.setdefault(end, [])
    active, output = [], []
    previous, text = None, None
    for timestamp in sorted(events):
        if text is not None and previous < timestamp:
            if output and output[-1][1] == previous and output[-1][2] == text:
                output[-1][1] = timestamp
            else:
                output.append([previous, timestamp, text])
        for index in events[timestamp]:
            heapq.heappush(active, index)
        while active and rows[active[0]][1] <= timestamp:
            heapq.heappop(active)
        text = rows[active[0]][2] if active else None
        previous = timestamp
    return output


def subtitle_page(payload, offset=None, at_ms=0):
    if payload['t'] != 'text':
        return payload
    cues = payload['c']  # canonical, disjoint millisecond display timeline
    if offset is None:
        lo, hi = 0, len(cues)
        while lo < hi:
            mid = (lo + hi) // 2
            if cues[mid][1] <= at_ms:
                lo = mid + 1
            else:
                hi = mid
        offset = lo
    if not 0 <= offset <= len(cues):
        raise ValueError('Invalid subtitle page offset')
    end = min(len(cues), offset + PAGE_CUES)
    return {'t': 'text', 'paged': 1, 'offset': offset,
            'next': end if end < len(cues) else -1,
            'until': cues[end][0] if end < len(cues) else 2147483647,
            'c': cues[offset:end]}
