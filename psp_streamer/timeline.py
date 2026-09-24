"""Bounded, seconds-based chapter/skip data for browser controls only."""
import math


def number(value):
    try:
        result = float(value)
        return result if math.isfinite(result) and 0 <= result <= 604800 else None
    except (TypeError, ValueError, OverflowError):
        return None


def chapters(rows, start='start_time', scale=1, title='title'):
    output = {}
    for row in rows[:2048] if isinstance(rows, list) else []:
        if not isinstance(row, dict):
            continue
        try:
            at = number(float(row.get(start)) / scale)
        except (TypeError, ValueError, OverflowError):
            continue
        if at is not None:
            label = row.get(title) or (row.get('tags') or {}).get('title') or ''
            output[at] = {'start': at, 'title': str(label)[:160]}
    return [output[key] for key in sorted(output)]


def plex_markers(rows):
    output = []
    for row in rows[:128] if isinstance(rows, list) else []:
        if not isinstance(row, dict) or row.get('type') not in ('intro', 'credits'):
            continue
        try:
            start = number(float(row.get('startTimeOffset')) / 1000)
            end = number(float(row.get('endTimeOffset')) / 1000)
        except (TypeError, ValueError, OverflowError):
            continue
        if start is not None and end is not None and end > start:
            output.append({'type': row['type'], 'start': start, 'end': end})
    return sorted(output, key=lambda marker: marker['start'])


def jellyfin_markers(rows):
    converted = []
    for row in rows[:128] if isinstance(rows, list) else []:
        if not isinstance(row, dict) or row.get('Type') not in ('Intro', 'Outro'):
            continue
        try:
            converted.append({'type': 'intro' if row['Type'] == 'Intro' else 'credits',
                              'startTimeOffset': float(row.get('StartTicks')) / 10000,
                              'endTimeOffset': float(row.get('EndTicks')) / 10000})
        except (TypeError, ValueError, OverflowError):
            continue
    return plex_markers(converted)
