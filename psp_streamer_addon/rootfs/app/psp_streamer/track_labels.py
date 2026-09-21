"""Small, unambiguous subtitle labels from existing container metadata only."""
from collections import defaultdict

FORMATS = {'hdmv_pgs_subtitle': 'PGS', 'dvd_subtitle': 'VobSub',
           'dvb_subtitle': 'DVB', 'subrip': 'SRT', 'mov_text': 'TX3G',
           'webvtt': 'VTT', 'ass': 'ASS', 'ssa': 'SSA'}


def compact(value, limit=40):
    text = ' '.join(str(value or '').replace('"', "'").replace('\\', '/').split())
    return text.encode('utf-8')[:limit].decode('utf-8', errors='ignore')


def positive_tag(tags, name):
    # Matroska statistics may be language-suffixed. Prefer the unsuffixed
    # value; never read packets or extract a subtitle merely to count it.
    for key in (name, name + '-eng'):
        value = str(tags.get(key, ''))
        if value.isascii() and value.isdecimal() and 0 < len(value) <= 18:
            number = int(value)
            if number > 0:
                return number
    return None


def subtitle_labels(streams):
    rows, groups = [], defaultdict(list)
    for stream in streams:
        if stream.get('codec_type') != 'subtitle':
            continue
        tags = {key.lower(): value for key, value in stream.get('tags', {}).items()}
        disposition = stream.get('disposition', {})
        row = {'n': str(len(rows)), 'l': compact(tags.get('language', 'und'), 15)}
        title = compact(tags.get('title'))
        flags = []
        for key, label in (('forced', 'Forced'), ('hearing_impaired', 'SDH'), ('default', 'Default')):
            if str(disposition.get(key, 0)) == '1' and label.casefold() not in title.casefold():
                flags.append(label)
        codec = str(stream.get('codec_name') or '')
        form = FORMATS.get(codec, compact(codec.upper(), 12))
        for name, tag in (('bytes', 'number_of_bytes'), ('entries', 'number_of_frames')):
            count = positive_tag(tags, tag)
            if count is not None:
                row[name] = count
        rows.append(row)
        language = {'deu': 'ger', 'de': 'ger', 'en': 'eng', 'ja': 'jpn'}.get(row['l'].lower(), row['l'].lower())
        groups[language].append((row, flags, form, title))
    for group in groups.values():
        # Byte sizes are only comparable within the same codec. Missing stats
        # must not be mistaken for zero, nor small tracks labelled "Forced".
        comparable = len(group) > 1 and len({item[2] for item in group}) == 1
        sizes = [item[0].get('bytes') for item in group]
        smallest = min(sizes) if comparable and all(sizes) and len(set(sizes)) > 1 else None
        for row, flags, form, title in group:
            parts = ([f"#{int(row['n'])+1}"] if len(group) > 1 else []) + flags
            if smallest is not None and row.get('bytes') == smallest:
                parts.append('smaller')
            if form:
                parts.append(form)
            # Identity/flags stay visible even when a long title is truncated.
            row['t'] = compact(' '.join(parts) + (' | ' if parts and title else '') + title)
    return rows
