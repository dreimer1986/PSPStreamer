"""Restore IDF's existing bonded HID cache loader, in the build tree only."""
import hashlib
from pathlib import Path
import sys

def generate(source):
    if hashlib.sha256(source).hexdigest() != 'e348f93c7cc9cca7ecc8b20607bfdca0cfcf32bba876efe04862387a855467b9':
        raise ValueError('Review HID cache restoration for this ESP-IDF version first')
    old = b'// btc_storage_load_bonded_hid_info();'
    if source.count(old) != 1:
        raise ValueError('Expected exactly one disabled bonded HID restore call')
    return source.replace(old, b'btc_storage_load_bonded_hid_info();')

if __name__ == '__main__':
    Path(sys.argv[2]).write_bytes(generate(Path(sys.argv[1]).read_bytes()))
