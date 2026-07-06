"""One-shot: lift the standard Blowfish P/S tables out of OpenRA's Blowfish.cs
into a Python module, so we don't hand-type 1042 hex constants."""
import re
import sys
from pathlib import Path

src = Path(sys.argv[1]).read_text()
tables = src[src.index('lookupMfromP ='):]
vals = [int(n, 16) for n in re.findall(r'0x[0-9a-fA-F]{1,8}', tables)]

P = vals[:18]
S_flat = vals[18:]
assert len(S_flat) == 1024, f"expected 1024 S-box values, got {len(S_flat)}"
assert P[0] == 0x243F6A88, "P[0] should be the first hex digits of pi"

S = [S_flat[i * 256:(i + 1) * 256] for i in range(4)]

out = Path(sys.argv[2])
with out.open('w') as f:
    f.write('# Standard Blowfish constant tables (hex digits of pi), extracted from\n')
    f.write('# OpenRA Blowfish.cs by gen_blowfish_tables.py. Do not edit.\n\n')
    f.write('P_INIT = [\n')
    for i in range(0, 18, 6):
        f.write('    ' + ', '.join(f'0x{v:08X}' for v in P[i:i + 6]) + ',\n')
    f.write(']\n\nS_INIT = [\n')
    for box in S:
        f.write('    [\n')
        for i in range(0, 256, 6):
            f.write('        ' + ', '.join(f'0x{v:08X}' for v in box[i:i + 6]) + ',\n')
        f.write('    ],\n')
    f.write(']\n')
print(f"wrote {out}")
