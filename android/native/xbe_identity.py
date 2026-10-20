#!/usr/bin/env python3
# The code identity of an xbe: SHA-1 over its entry point, base and each section's address, size and stored digest.
# Two files with the same identity load the same code and data at the same addresses; a header patch (allowed media,
# region, signature) keeps it. app_android.cpp computes the same value from the game the player picks.
import hashlib
import struct
import sys

d = open(sys.argv[1], 'rb').read()
if d[:4] != b'XBEH':
    sys.exit('not an xbe')
base, = struct.unpack_from('<I', d, 0x104)
count, headers = struct.unpack_from('<II', d, 0x11C)
h = hashlib.sha1()
h.update(struct.pack('<II', base, struct.unpack_from('<I', d, 0x128)[0]))
for i in range(count):
    o = headers - base + i * 56
    flags, va, vsize, raw, rsize = struct.unpack_from('<5I', d, o)
    h.update(struct.pack('<III', va, vsize, rsize) + d[o + 36:o + 56])
print(h.hexdigest())
