#!/usr/bin/env python3
"""Read or rewrite the version in localized InfoPlist.strings files.

Handles every encoding such files appear in: UTF-8/ASCII, UTF-16 (with BOM,
as Xcode and the editor sometimes save them) and the binary/XML plist form
Xcode produces in a built bundle.

  infoplist_strings.py read  FILE...          print every version found
  infoplist_strings.py write VERSION FILE...  force VERSION into text files
"""
import plistlib
import re
import sys

SHORT = re.compile(r'(CFBundleShortVersionString\s*=\s*")[^"]*(")')
INFO = re.compile(r'(CFBundleGetInfoString\s*=\s*"[^"]*?[Vv]ersion\s+)\d+\.\d+\.\d+')
VER = re.compile(r'\d+\.\d+\.\d+')


def decode(raw):
    if raw[:2] in (b'\xff\xfe', b'\xfe\xff'):
        return raw.decode('utf-16'), 'utf-16'
    if raw[:3] == b'\xef\xbb\xbf':
        return raw[3:].decode('utf-8'), 'utf-8-sig'
    return raw.decode('utf-8'), 'utf-8'


def read(path):
    raw = open(path, 'rb').read()
    try:
        d = plistlib.loads(raw)
        out = [d['CFBundleShortVersionString']] if 'CFBundleShortVersionString' in d else []
        m = VER.search(d.get('CFBundleGetInfoString', ''))
        return out + ([m.group(0)] if m else [])
    except Exception:
        pass
    text, _ = decode(raw)
    out = [m.group(0)[m.group(0).index('"') + 1:-1] for m in SHORT.finditer(text)]
    for m in re.finditer(r'CFBundleGetInfoString\s*=\s*"([^"]*)"', text):
        v = VER.search(m.group(1))
        if v:
            out.append(v.group(0))
    return out


def write(version, path):
    raw = open(path, 'rb').read()
    text, enc = decode(raw)
    new = SHORT.sub(lambda m: m.group(1) + version + m.group(2), text)
    new = INFO.sub(lambda m: m.group(1) + version, new)
    if enc == 'utf-16':
        data = b'\xff\xfe' + new.encode('utf-16-le')
    elif enc == 'utf-8-sig':
        data = b'\xef\xbb\xbf' + new.encode('utf-8')
    else:
        data = new.encode('utf-8')
    open(path, 'wb').write(data)


if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'read':
        for p in sys.argv[2:]:
            for v in read(p):
                print(v)
    elif cmd == 'write':
        for p in sys.argv[3:]:
            write(sys.argv[2], p)
    else:
        sys.exit(__doc__)
