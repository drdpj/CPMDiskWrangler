#!/usr/bin/env python3
# Shared, unchanged, by Acorn Disk Wrangler and CP/M Disk Wrangler.
# Copyright (C) 2026 Daniel Jameson. GPL v3 or later.
#
# Qt's official binaries (the macOS build here) carry their own copies of third-party code - libpng, libjpeg,
# HarfBuzz, PCRE2, double-conversion, MD4C and so on - compiled into Qt's frameworks. Qt's source records each
# one in a qt_attribution.json, with its licence text beside it or in the module's LICENSES folder (REUSE
# layout). This writes one file per component - name, version, copyright, licence, then the licence texts -
# so ci/collect-licences.sh can ship them with the program.
#
#   qt-attributions.py <qt module source, e.g. a checkout of qtbase at the matching tag> <output dir>
#
# Everything outside tests and examples is included, even what a given platform's build leaves out: listing
# a licence that does not apply is harmless; missing one that does is not. Fails if a licence text is missing.
import glob, json, os, re, sys

src, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
SKIP = re.compile(r'(^|/)(tests|examples|util|src/testlib|src/testinternal)/')
missing = []
count = 0

def as_list(v):
    return v if isinstance(v, list) else ([v] if v else [])

for path in sorted(glob.glob(os.path.join(src, '**', 'qt_attribution.json'), recursive=True)):
    rel = os.path.relpath(path, src)
    if SKIP.search(rel):
        continue
    with open(path, encoding='utf-8') as f:
        data = json.loads(f.read(), strict=False)      # some files have raw tabs in strings
    here = os.path.dirname(path)
    for e in as_list(data):
        ident = e.get('LicenseId', '')
        texts = []                                     # (title, text)
        for lf in as_list(e.get('LicenseFile')) + as_list(e.get('LicenseFiles')):
            p = os.path.normpath(os.path.join(here, lf))
            if os.path.isfile(p):
                texts.append((os.path.basename(p), open(p, encoding='utf-8', errors='replace').read()))
        if not texts:                                  # REUSE layout: LICENSES/<SPDX id or LicenseRef>.txt
            for lid in re.findall(r'[A-Za-z0-9.+-]+', ident):
                if lid in ('AND', 'OR', 'WITH'):
                    continue
                p = os.path.join(src, 'LICENSES', lid + '.txt')
                if os.path.isfile(p):
                    texts.append((lid, open(p, encoding='utf-8', errors='replace').read()))
        if not texts:
            missing.append(f"{e.get('Name', e.get('Id'))} ({rel}: {ident})")
            continue
        name = re.sub(r'[^A-Za-z0-9._-]+', '-', e.get('Id') or e.get('Name')).strip('-') + '.txt'
        with open(os.path.join(out, name), 'w', encoding='utf-8') as o:
            o.write(f"{e.get('Name', '')}\n")
            for k in ('Version', 'Homepage', 'QtUsage'):
                if e.get(k):
                    o.write(f"{k}: {e[k]}\n")
            for c in as_list(e.get('Copyright')):
                o.write(f"Copyright: {c}\n" if not c.lower().startswith('copyright') else f"{c}\n")
            o.write(f"License: {e.get('License', '')} ({ident})\n")
            for title, text in texts:
                o.write(f"\n---------------- {title}\n\n{text.rstrip()}\n")
        count += 1

if missing:
    sys.exit('qt-attributions: no licence text for: ' + '; '.join(missing))
print(f'qt-attributions: {count} components')
