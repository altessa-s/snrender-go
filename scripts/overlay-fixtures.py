#!/usr/bin/env python3
# Copyright 2026 ALTESSA SOLUTIONS INC.
# Use of this source code is governed by an MIT-style license that can be
# found in the LICENSE file.
"""Copy overlay inputs and run the pinned upstream PDF fixture generator.

Requires pypdf only when regenerating, never when running Go tests.
Usage: python3 scripts/overlay-fixtures.py ../snrender-lib
Reads immutable Git objects; no checkout or worktree changes are needed.
"""
import ast
import base64
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile

from pypdf import PdfWriter
from pypdf.generic import (ArrayObject, BooleanObject, DecodedStreamObject,
                          DictionaryObject, FloatObject, NameObject, TextStringObject)

PIN = '032b7048d29617c8b9861256b59c3cf9a7bf578f'
ROOT = Path(__file__).resolve().parent.parent
SOURCE = Path(sys.argv[1]).resolve()
DEST = ROOT / 'testdata/overlay'
DEST.mkdir(parents=True, exist_ok=True)
(ROOT / 'build').mkdir(exist_ok=True)


def read(name):
    return subprocess.check_output(['git', '-C', str(SOURCE), 'show', PIN + ':' + name])


# Run exactly the upstream make() function, its cases and its initial fixture
# setup loop, stopping before the first invocation of the C probe. This writes
# blue markers using the device's document rectangles and tagged source pages.
tree = ast.parse(read('tests/check_overlay_geometry.py'))
selected = []
for node in tree.body:
    if isinstance(node, ast.FunctionDef) and node.name == 'make':
        selected.append(node)
    if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'cases' for t in node.targets):
        selected.append(node)
    if isinstance(node, ast.For) and isinstance(node.iter, ast.Name) and node.iter.id == 'cases':
        for i, child in enumerate(node.body):
            if isinstance(child, ast.Expr) and isinstance(child.value, ast.Call) and isinstance(child.value.func, ast.Name) and child.value.func.id == 'run':
                node.body = node.body[:i]
                break
        selected.append(node)
namespace = dict(globals(), root=ROOT / 'testdata', work=DEST)
# Upstream expects a directory named fixtures below root.
# Its setup loop alone needs that path, so use a separate staging root.
with tempfile.TemporaryDirectory(dir=ROOT / 'build') as temporary:
    staging = Path(temporary)
    shutil.copytree(ROOT / 'testdata/samples', staging / 'fixtures/samples')
    namespace['root'] = staging
    exec(compile(ast.Module(body=selected, type_ignores=[]), 'check_overlay_geometry.py', 'exec'), namespace)

# The preservation test uses four mixed-size pages, with rotation and a nonzero
# CropBox on page two, source text, links and an outline.
tree = ast.parse(read('tests/check_overlay.py'))
make = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'make')
raw = (ROOT / 'testdata/samples/pdf-highlights-n6-20260016.mark').read_bytes()
info = json.loads(base64.b64decode(raw[28:28 + struct.unpack_from('<I', raw, 24)[0]]))
namespace['markers'] = [((r['x0'] + r['x1']) / 2, (r['y0'] + r['y1']) / 2)
                        for item in info['0'] if item['annotationType'] == 0
                        for r in item['mupdfRectList']]
exec(compile(ast.Module(body=[make], type_ignores=[]), 'check_overlay.py', 'exec'), namespace)
namespace['make'](DEST / 'mixed.pdf', markers=True)
namespace['make'](DEST / 'encrypted.pdf', encrypted=True)
namespace['make'](DEST / 'huge.pdf', huge=True)
namespace['make'](DEST / 'empty.pdf', count=0)

# Reuse the signature and appearance-only setup blocks verbatim as well.
geometry = read('tests/check_overlay_geometry.py').decode()
signature = geometry.split('# A signature field is reported', 1)[1]
signature = signature[signature.index('writer=PdfWriter()'):signature.index("out=work/'signed-out.pdf'")]
appearance = geometry.split('# Appearance-only pages', 1)[1]
appearance = appearance[appearance.index('writer=PdfWriter()'):appearance.index("out=work/'appearance.png'")]
exec(compile(signature, 'check_overlay_geometry.py', 'exec'), namespace)
exec(compile(appearance, 'check_overlay_geometry.py', 'exec'), namespace)

header = read('tests/source_pdf.h').decode()
array = header.split('source_pdf[] = {', 1)[1].split('}', 1)[0]
(DEST / 'source.pdf').write_bytes(bytes(map(int, re.findall(r'\d+', array))))
for name in ('acroform', 'appearances', 'form', 'incremental', 'inherited', 'rich'):
    (DEST / (name + '.pdf')).write_bytes(read('tests/data/pdf-seeds/' + name + '.pdf'))
(DEST / 'SHA256SUMS').write_text(''.join(
    f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n'
    for p in sorted(DEST.glob('*.pdf'))))
print(f'Generated or copied {len(list(DEST.glob("*.pdf")))} upstream PDFs from {PIN}')
