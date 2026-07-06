"""Recursively extract Westwood MIX archives (Tiberian Dawn + Red Alert formats,
including RA's Blowfish-encrypted headers).

Algorithms ported from OpenRA (GPL v3): MixFile.cs, PackageEntry.cs,
BlowfishKeyProvider.cs. Filenames resolved via XCC's "global mix database.dat"
(MIX archives store only filename hashes; unresolvable entries are written as
0x<hash> with a sniffed extension).

Usage: python mix_extract.py <extracted-root> <output-root> <global mix database.dat>
"""
import base64
import struct
import sys
import zlib
from pathlib import Path

from blowfish_tables import P_INIT, S_INIT

WESTWOOD_PUBLIC_KEY = "AihRvNoIbTn85FZRYNZRcT+i6KpU+maCsEqr3Q5q+LDB5tH7Tz2qQ38V"


class Blowfish:
    """Standard Blowfish, ECB, big-endian blocks (what the RA header uses)."""

    def __init__(self, key: bytes):
        self.P = list(P_INIT)
        self.S = [list(box) for box in S_INIT]
        j = 0
        for i in range(18):
            k = (key[j % len(key)] << 24 | key[(j + 1) % len(key)] << 16
                 | key[(j + 2) % len(key)] << 8 | key[(j + 3) % len(key)])
            self.P[i] ^= k
            j += 4
        l = r = 0
        for i in range(0, 18, 2):
            l, r = self._encrypt_block(l, r)
            self.P[i], self.P[i + 1] = l, r
        for box in self.S:
            for i in range(0, 256, 2):
                l, r = self._encrypt_block(l, r)
                box[i], box[i + 1] = l, r

    def _f(self, x):
        S = self.S
        h = (S[0][x >> 24 & 0xFF] + S[1][x >> 16 & 0xFF]) & 0xFFFFFFFF
        return ((h ^ S[2][x >> 8 & 0xFF]) + S[3][x & 0xFF]) & 0xFFFFFFFF

    def _encrypt_block(self, l, r):
        P = self.P
        for i in range(16):
            l ^= P[i]
            r ^= self._f(l)
            l, r = r, l
        l, r = r, l
        return l ^ P[17], r ^ P[16]

    def _decrypt_block(self, l, r):
        P = self.P
        for i in range(17, 1, -1):
            l ^= P[i]
            r ^= self._f(l)
            l, r = r, l
        l, r = r, l
        return l ^ P[0], r ^ P[1]

    def decrypt(self, data: bytes) -> bytes:
        out = bytearray()
        for off in range(0, len(data), 8):
            l, r = struct.unpack_from('>II', data, off)
            l, r = self._decrypt_block(l, r)
            out += struct.pack('>II', l, r)
        return bytes(out)


def decrypt_blowfish_key(keyblock: bytes) -> bytes:
    """The 80-byte key block at the start of an encrypted MIX header hides a
    56-byte Blowfish key: two 40-byte little-endian blocks, each raised to
    0x10001 mod Westwood's public modulus (BlowfishKeyProvider.cs, deobfuscated)."""
    der = base64.b64decode(WESTWOOD_PUBLIC_KEY)
    assert der[0] == 2 and der[1] < 0x80
    klen = der[1]
    modulus = int.from_bytes(der[2:2 + klen], 'big')
    a = (modulus.bit_length() - 2) // 8
    dest = bytearray()
    pre_len = (55 // a + 1) * (a + 1)
    off = 0
    while a + 1 <= pre_len:
        block = int.from_bytes(keyblock[off:off + a + 1], 'little')
        dest += pow(block, 0x10001, modulus).to_bytes(a + 8, 'little')[:a]
        pre_len -= a + 1
        off += a + 1
    return bytes(dest[:56])


def hash_classic(name: str) -> int:
    """TD/RA filename hash: rotate-left-1 + add over 4-byte LE words of the
    uppercased, NUL-padded name."""
    data = name.upper().encode('latin-1')
    data += b'\0' * (-len(data) % 4)
    result = 0
    for (word,) in struct.iter_unpack('<I', data):
        result = (((result << 1) | (result >> 31)) + word) & 0xFFFFFFFF
    return result


def hash_crc32(name: str) -> int:
    """TS/RA2 filename hash: CRC32 over the uppercased name with Westwood's
    odd padding scheme."""
    data = bytearray(name.upper().encode('latin-1'))
    length = len(data)
    if length % 4 != 0:
        rounded = length // 4 * 4
        data.append(length - rounded)
        data += bytes([data[rounded]]) * (3 - length % 4)
    return zlib.crc32(bytes(data)) & 0xFFFFFFFF


def load_name_maps(gmd_path: Path):
    """Parse XCC's global mix database: repeated [int32 count, count * (name NUL
    comment NUL)] groups. Returns hash->name maps for both hash schemes."""
    data = gmd_path.read_bytes()
    classic, crc = {}, {}
    pos = 0
    while pos + 4 <= len(data):
        (count,) = struct.unpack_from('<i', data, pos)
        pos += 4
        for _ in range(count):
            end = data.index(0, pos)
            name = data[pos:end].decode('latin-1')
            pos = end + 1
            pos = data.index(0, pos) + 1  # skip comment
            classic[hash_classic(name)] = name
            crc[hash_crc32(name)] = name
    return classic, crc


def parse_entries(header: bytes, offset: int):
    (count,) = struct.unpack_from('<H', header, offset)
    entries = []
    pos = offset + 6  # skip count + dataSize
    for _ in range(count):
        entries.append(struct.unpack_from('<III', header, pos))  # hash, offset, length
        pos += 12
    return entries, pos


def parse_mix(data: bytes):
    """Returns (entries, data_start). Raises ValueError if not a MIX."""
    if len(data) < 6:
        raise ValueError("too short")
    (first,) = struct.unpack_from('<H', data, 0)
    if first != 0:  # C&C (TD) format: header starts immediately
        entries, data_start = parse_entries(data, 0)
    else:  # RA/TS format: uint16 zero, uint16 flags
        (flags,) = struct.unpack_from('<H', data, 2)
        if flags & 2:  # encrypted header
            fish = Blowfish(decrypt_blowfish_key(data[4:84]))
            (num_files,) = struct.unpack_from('<H', fish.decrypt(data[84:92]), 0)
            block_count = (13 + num_files * 12) // 8
            header = fish.decrypt(data[84:84 + block_count * 8])
            entries, _ = parse_entries(header, 0)
            data_start = 84 + block_count * 8
        else:
            entries, data_start = parse_entries(data, 4)
    # sanity check offsets
    for _, off, length in entries:
        if data_start + off + length > len(data) + 8:  # small slack for padding
            raise ValueError("entry out of bounds — probably not a MIX")
    return entries, data_start


def sniff_extension(content: bytes) -> str:
    if content[:4] == b'FORM':
        return '.vqa' if content[8:12] == b'WVQA' else '.iff'
    if len(content) == 768 and all(b < 64 for b in content[:96]):
        return '.pal'
    if len(content) > 12:
        rate, size, _, _, typ = struct.unpack_from('<HiiBB', content, 0)
        if typ in (1, 99) and 4000 <= rate <= 48000 and size == len(content) - 12:
            return '.aud'
    return '.bin'


class Stats:
    mixes = 0
    files = 0
    named = 0
    unknown = 0
    failed = []


def extract_mix(data: bytes, out_dir: Path, classic_map, crc_map, stats, label):
    entries, data_start = parse_mix(data)
    # pick the hash scheme that resolves more names (OpenRA does the same)
    resolved_c = [classic_map.get(h) for h, _, _ in entries]
    resolved_x = [crc_map.get(h) for h, _, _ in entries]
    resolved = resolved_c if sum(n is not None for n in resolved_c) >= sum(
        n is not None for n in resolved_x) else resolved_x

    stats.mixes += 1
    out_dir.mkdir(parents=True, exist_ok=True)
    for (hsh, off, length), name in zip(entries, resolved):
        content = data[data_start + off:data_start + off + length]
        if name is None:
            name = f'0x{hsh:08x}{sniff_extension(content)}'
            stats.unknown += 1
        else:
            stats.named += 1
        stats.files += 1
        target = out_dir / name
        if name.lower().endswith('.mix'):
            try:
                extract_mix(content, out_dir / Path(name).stem, classic_map,
                            crc_map, stats, f'{label}/{name}')
                continue  # extracted recursively; don't keep the container
            except ValueError:
                pass  # not actually a mix — fall through and write it as a file
        target.write_bytes(content)


def main():
    root, out_root, gmd = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    classic_map, crc_map = load_name_maps(gmd)
    print(f'name database: {len(classic_map):,} known filenames')

    stats = Stats()
    mix_files = sorted(root.rglob('*.mix'), key=lambda p: str(p).lower())
    # also catch uppercase .MIX on case-sensitive globs (Windows rglob is
    # case-insensitive, so this is just belt-and-braces)
    for mix_path in mix_files:
        rel = mix_path.relative_to(root)
        out_dir = out_root / rel.parent / rel.stem
        if out_dir.exists():
            print(f'skip (done)   {rel}')
            continue
        print(f'extracting    {rel}', flush=True)
        try:
            extract_mix(mix_path.read_bytes(), out_dir, classic_map, crc_map,
                        stats, str(rel))
        except (ValueError, struct.error) as e:
            stats.failed.append((str(rel), str(e)))
            print(f'  FAILED: {e}')

    print(f'\n{stats.mixes} MIX archives -> {stats.files:,} files '
          f'({stats.named:,} named, {stats.unknown:,} unknown-hash)')
    if stats.failed:
        print('failed archives:')
        for rel, err in stats.failed:
            print(f'  {rel}: {err}')


if __name__ == '__main__':
    main()
