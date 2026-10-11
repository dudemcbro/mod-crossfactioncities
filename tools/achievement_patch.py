#!/usr/bin/env python3
"""Build the optional client patch for CrossFactionCities.UnlockFactionAchievements.

The stock client lists only its own faction's achievements in the achievement window,
because Achievement.dbc marks many of them as Alliance-only or Horde-only. This script
reads Achievement.dbc from your own client's MPQ archives, marks every achievement as
"both factions" and writes it into a small patch MPQ in the client's locale folder.

No game data ships with the module: the patch is built from the client it is installed
into. Python 3.8 or newer, standard library only.

Usage:
    python3 achievement_patch.py "/path/to/World of Warcraft"        build and install
    python3 achievement_patch.py "/path/to/World of Warcraft" --check report only
    python3 achievement_patch.py "/path/to/World of Warcraft" --remove

See docs/client-patch.md for details.
"""

import argparse
import bz2
import mmap
import os
import re
import struct
import sys
import zlib

DBC_PATH = "DBFilesClient\\Achievement.dbc"
DEFAULT_PATCH_NUMBER = "4"
# Written into every patch this script builds, so it only ever replaces its own files.
MARKER_PATH = "mod-crossfactioncities.txt"
MARKER_TEXT = b"Built by mod-crossfactioncities tools/achievement_patch.py\r\n"

ACHIEVEMENT_FIELD_COUNT = 62
FACTION_FIELD_OFFSET = 4          # field 1, after the uint32 ID
FACTION_BOTH = -1

# MPQ file flags
MPQ_FILE_IMPLODE = 0x00000100
MPQ_FILE_COMPRESS = 0x00000200
MPQ_FILE_ENCRYPTED = 0x00010000
MPQ_FILE_FIX_KEY = 0x00020000
MPQ_FILE_SINGLE_UNIT = 0x01000000
MPQ_FILE_DELETE_MARKER = 0x02000000
MPQ_FILE_SECTOR_CRC = 0x04000000
MPQ_FILE_EXISTS = 0x80000000

HASH_ENTRY_EMPTY = 0xFFFFFFFF
HASH_ENTRY_DELETED = 0xFFFFFFFE

MASK32 = 0xFFFFFFFF


class PatchError(Exception):
    pass


def _build_crypt_table():
    table = [0] * 0x500
    seed = 0x00100001
    for index1 in range(0x100):
        index2 = index1
        for _ in range(5):
            seed = (seed * 125 + 3) % 0x2AAAAB
            high = (seed & 0xFFFF) << 16
            seed = (seed * 125 + 3) % 0x2AAAAB
            low = seed & 0xFFFF
            table[index2] = high | low
            index2 += 0x100
    return table


CRYPT_TABLE = _build_crypt_table()

HASH_TABLE_OFFSET = 0
HASH_NAME_A = 1
HASH_NAME_B = 2
HASH_FILE_KEY = 3


def hash_string(name, hash_type):
    seed1 = 0x7FED7FED
    seed2 = 0xEEEEEEEE
    for char in name.replace("/", "\\").upper().encode("ascii"):
        seed1 = (CRYPT_TABLE[(hash_type << 8) + char] ^ (seed1 + seed2)) & MASK32
        seed2 = (char + seed1 + seed2 + (seed2 << 5) + 3) & MASK32
    return seed1


def decrypt(data, key):
    count = len(data) // 4
    values = struct.unpack("<%dI" % count, data[:count * 4])
    seed = 0xEEEEEEEE
    out = []
    for value in values:
        seed = (seed + CRYPT_TABLE[0x400 + (key & 0xFF)]) & MASK32
        plain = (value ^ (key + seed)) & MASK32
        key = ((((~key) << 0x15) + 0x11111111) & MASK32) | (key >> 0x0B)
        seed = (plain + seed + (seed << 5) + 3) & MASK32
        out.append(plain)
    return struct.pack("<%dI" % count, *out) + data[count * 4:]


def encrypt(data, key):
    count = len(data) // 4
    values = struct.unpack("<%dI" % count, data[:count * 4])
    seed = 0xEEEEEEEE
    out = []
    for value in values:
        seed = (seed + CRYPT_TABLE[0x400 + (key & 0xFF)]) & MASK32
        out.append((value ^ (key + seed)) & MASK32)
        key = ((((~key) << 0x15) + 0x11111111) & MASK32) | (key >> 0x0B)
        seed = (value + seed + (seed << 5) + 3) & MASK32
    return struct.pack("<%dI" % count, *out) + data[count * 4:]


def decompress(data, expected_size):
    """Decompresses one sector or single-unit file (first byte: compression mask)."""
    mask = data[0]
    payload = data[1:]
    if mask == 0x02:
        result = zlib.decompress(payload)
    elif mask == 0x10:
        result = bz2.decompress(payload)
    elif mask == 0x12:
        raise PatchError("LZMA compressed file, not used by 3.3.5 clients")
    else:
        raise PatchError("unsupported MPQ compression 0x%02X" % mask)
    if len(result) != expected_size:
        raise PatchError("decompressed size mismatch")
    return result


class MpqArchive:
    """Read-only access to MPQ format 1 and 2 archives, enough for WotLK client files."""

    def __init__(self, path):
        self.path = path
        # Memory-mapped: the base archives are several gigabytes and only a few kilobytes are read.
        with open(path, "rb") as handle:
            self.data = mmap.mmap(handle.fileno(), 0, access=mmap.ACCESS_READ)
        self._read_header()

    def _read_header(self):
        data = self.data
        offset = 0
        while offset + 32 <= len(data):
            magic = data[offset:offset + 4]
            if magic == b"MPQ\x1a":
                break
            if magic == b"MPQ\x1b":   # user data header points at the real header
                offset += struct.unpack_from("<I", data, offset + 8)[0]
                continue
            offset += 0x200
        else:
            raise PatchError("not an MPQ archive")

        (header_size, _archive_size, version, sector_shift, hash_pos, block_pos,
         hash_count, block_count) = struct.unpack_from("<IIHHIIII", data, offset + 4)
        hash_pos_high = block_pos_high = 0
        if version >= 1 and header_size >= 44:
            _hi_block_pos, hash_pos_high, block_pos_high = struct.unpack_from("<QHH", data, offset + 32)
        self.base = offset
        self.sector_size = 0x200 << sector_shift
        hash_pos = offset + (hash_pos | (hash_pos_high << 32))
        block_pos = offset + (block_pos | (block_pos_high << 32))

        hash_table = decrypt(data[hash_pos:hash_pos + hash_count * 16], hash_string("(hash table)", HASH_FILE_KEY))
        block_table = decrypt(data[block_pos:block_pos + block_count * 16], hash_string("(block table)", HASH_FILE_KEY))
        self.hashes = [struct.unpack_from("<IIHHI", hash_table, i * 16) for i in range(hash_count)]
        self.blocks = [struct.unpack_from("<IIII", block_table, i * 16) for i in range(block_count)]

    def _find_block(self, name):
        if not self.hashes:
            return None
        count = len(self.hashes)
        start = hash_string(name, HASH_TABLE_OFFSET) % count
        name_a = hash_string(name, HASH_NAME_A)
        name_b = hash_string(name, HASH_NAME_B)
        found = None
        for step in range(count):
            hash_a, hash_b, locale, _platform, block_index = self.hashes[(start + step) % count]
            if block_index == HASH_ENTRY_EMPTY:
                break
            if block_index == HASH_ENTRY_DELETED or hash_a != name_a or hash_b != name_b:
                continue
            # Prefer the neutral locale, as the client does when its own is missing.
            if found is None or locale == 0:
                found = block_index
        if found is None or found >= len(self.blocks):
            return None
        return self.blocks[found]

    def contains(self, name):
        block = self._find_block(name)
        return block is not None and block[3] & MPQ_FILE_EXISTS and not block[3] & MPQ_FILE_DELETE_MARKER

    def read(self, name):
        block = self._find_block(name)
        if block is None or not block[3] & MPQ_FILE_EXISTS or block[3] & MPQ_FILE_DELETE_MARKER:
            raise PatchError("%s not found in %s" % (name, self.path))
        file_pos, compressed_size, file_size, flags = block
        if flags & MPQ_FILE_IMPLODE:
            raise PatchError("PKWARE imploded file, not supported")
        start = self.base + file_pos
        raw = self.data[start:start + compressed_size]

        key = None
        if flags & MPQ_FILE_ENCRYPTED:
            key = hash_string(name.split("\\")[-1], HASH_FILE_KEY)
            if flags & MPQ_FILE_FIX_KEY:
                key = ((key + file_pos) ^ file_size) & MASK32

        if flags & MPQ_FILE_SINGLE_UNIT:
            if key is not None:
                raw = decrypt(raw, key)
            if flags & MPQ_FILE_COMPRESS and compressed_size < file_size:
                return decompress(raw, file_size)
            return raw[:file_size]

        sector_count = (file_size + self.sector_size - 1) // self.sector_size
        if not flags & MPQ_FILE_COMPRESS:
            if key is None:
                return raw[:file_size]
            out = bytearray()
            for i in range(sector_count):
                out += decrypt(raw[i * self.sector_size:(i + 1) * self.sector_size], (key + i) & MASK32)
            return bytes(out[:file_size])

        offset_count = sector_count + 1 + (1 if flags & MPQ_FILE_SECTOR_CRC else 0)
        offset_table = raw[:offset_count * 4]
        if key is not None:
            offset_table = decrypt(offset_table, (key - 1) & MASK32)
        offsets = struct.unpack("<%dI" % offset_count, offset_table)

        out = bytearray()
        for i in range(sector_count):
            sector = raw[offsets[i]:offsets[i + 1]]
            if key is not None:
                sector = decrypt(sector, (key + i) & MASK32)
            expected = min(self.sector_size, file_size - i * self.sector_size)
            out += decompress(sector, expected) if len(sector) < expected else sector
        return bytes(out)


def write_mpq(path, files):
    """Writes a format 1 MPQ with the given {name: bytes} stored uncompressed."""
    hash_count = 16
    while hash_count < len(files) * 2:
        hash_count *= 2
    header_size = 32
    sector_shift = 3

    body = bytearray()
    blocks = []
    hashes = [(HASH_ENTRY_EMPTY, HASH_ENTRY_EMPTY, 0xFFFF, 0xFFFF, HASH_ENTRY_EMPTY)] * hash_count
    for index, (name, content) in enumerate(files.items()):
        blocks.append((header_size + len(body), len(content), len(content), MPQ_FILE_EXISTS))
        body += content
        slot = hash_string(name, HASH_TABLE_OFFSET) % hash_count
        while hashes[slot][4] != HASH_ENTRY_EMPTY:
            slot = (slot + 1) % hash_count
        hashes[slot] = (hash_string(name, HASH_NAME_A), hash_string(name, HASH_NAME_B), 0, 0, index)

    hash_table = b"".join(struct.pack("<IIHHI", *entry) for entry in hashes)
    block_table = b"".join(struct.pack("<IIII", *entry) for entry in blocks)
    hash_pos = header_size + len(body)
    block_pos = hash_pos + len(hash_table)
    archive_size = block_pos + len(block_table)

    header = struct.pack("<4sIIHHIIII", b"MPQ\x1a", header_size, archive_size, 0, sector_shift,
                         hash_pos, block_pos, hash_count, len(blocks))
    temp_path = path + ".tmp"
    with open(temp_path, "wb") as handle:
        handle.write(header)
        handle.write(body)
        handle.write(encrypt(hash_table, hash_string("(hash table)", HASH_FILE_KEY)))
        handle.write(encrypt(block_table, hash_string("(block table)", HASH_FILE_KEY)))
    os.replace(temp_path, path)


def patch_achievement_dbc(data):
    """Returns (patched data, number of achievements changed)."""
    if len(data) < 20:
        raise PatchError("Achievement.dbc is too short")
    magic, record_count, field_count, record_size, string_size = struct.unpack_from("<4sIIII", data)
    if magic != b"WDBC" or field_count != ACHIEVEMENT_FIELD_COUNT or record_size != field_count * 4:
        raise PatchError("Achievement.dbc is not the WotLK 3.3.5 layout (%d fields)" % field_count)
    if len(data) != 20 + record_count * record_size + string_size:
        raise PatchError("Achievement.dbc size does not match its header")

    patched = bytearray(data)
    changed = 0
    for i in range(record_count):
        offset = 20 + i * record_size + FACTION_FIELD_OFFSET
        if struct.unpack_from("<i", patched, offset)[0] != FACTION_BOTH:
            struct.pack_into("<i", patched, offset, FACTION_BOTH)
            changed += 1
    return bytes(patched), changed


def find_entry(directory, name):
    """Case-insensitive lookup of one directory entry (Linux installs vary in case)."""
    try:
        for entry in os.listdir(directory):
            if entry.lower() == name.lower():
                return os.path.join(directory, entry)
    except FileNotFoundError:
        pass
    return None


def find_locales(data_dir):
    return sorted(entry for entry in os.listdir(data_dir)
                  if re.fullmatch(r"[a-z]{2}[A-Z]{2}", entry) and os.path.isdir(os.path.join(data_dir, entry)))


PATCH_SUFFIXES = "23456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"


def archives_in_load_order(data_dir, locale):
    """Every archive name the client loads, lowest priority first, as paths (existing or not).
    Later archives override earlier ones."""
    locale_dir = os.path.join(data_dir, locale)
    names = [(data_dir, n) for n in ("common.MPQ", "common-2.MPQ", "expansion.MPQ", "lichking.MPQ")]
    names += [(locale_dir, n % locale) for n in ("locale-%s.MPQ", "speech-%s.MPQ", "expansion-locale-%s.MPQ",
                                                 "lichking-locale-%s.MPQ", "expansion-speech-%s.MPQ",
                                                 "lichking-speech-%s.MPQ")]
    names += [(data_dir, "patch.MPQ")] + [(data_dir, "patch-%s.MPQ" % s) for s in PATCH_SUFFIXES]
    names += [(locale_dir, "patch-%s.MPQ" % locale)] + [(locale_dir, "patch-%s-%s.MPQ" % (locale, s)) for s in PATCH_SUFFIXES]

    return [find_entry(directory, name) or os.path.join(directory, name) for directory, name in names]


def is_own_patch(path):
    try:
        return MpqArchive(path).contains(MARKER_PATH)
    except (PatchError, OSError, struct.error):
        return False


def main():
    parser = argparse.ArgumentParser(description="Build the optional achievement client patch for mod-crossfactioncities.")
    parser.add_argument("client", help="World of Warcraft 3.3.5a folder (the one containing Data and Wow.exe)")
    parser.add_argument("--locale", help="client locale, for example enUS (detected when there is only one)")
    parser.add_argument("--patch-number", default=DEFAULT_PATCH_NUMBER,
                        help="patch-<locale>-<N>.MPQ to write, 4-9 or A-Z (default: %(default)s)")
    parser.add_argument("--check", action="store_true", help="only report what would be done")
    parser.add_argument("--remove", action="store_true", help="delete the patch this script built")
    args = parser.parse_args()

    data_dir = find_entry(args.client, "Data")
    if not data_dir:
        raise PatchError("no Data folder in %s" % args.client)

    locales = find_locales(data_dir)
    locale = args.locale or (locales[0] if len(locales) == 1 else None)
    if not locale:
        raise PatchError("several locales found (%s), choose one with --locale" % ", ".join(locales) if locales
                         else "no locale folder (such as enUS) found in Data")
    if locale not in locales:
        raise PatchError("locale %s not found in Data" % locale)

    suffix = args.patch_number.upper()
    if len(suffix) != 1 or suffix not in PATCH_SUFFIXES[2:]:
        raise PatchError("--patch-number must be one of 4-9 or A-Z")
    patch_name = "patch-%s-%s.MPQ" % (locale, suffix)
    archives = archives_in_load_order(data_dir, locale)
    patch_index = [os.path.basename(path).lower() for path in archives].index(patch_name.lower())
    patch_path = archives[patch_index]
    patch_exists = os.path.exists(patch_path)

    if patch_exists and not is_own_patch(patch_path):
        raise PatchError("%s exists and was not built by this script; choose another --patch-number" % patch_path)

    if args.remove:
        if not patch_exists:
            print("Nothing to remove: %s does not exist." % patch_path)
        elif args.check:
            print("Would remove %s." % patch_path)
        else:
            os.remove(patch_path)
            print("Removed %s." % patch_path)
        return 0

    # The newest Achievement.dbc the client would load without patches built by this script.
    source = None
    for path in reversed(archives):
        if os.path.exists(path) and not is_own_patch(path) and MpqArchive(path).contains(DBC_PATH):
            source = path
            break
    if not source:
        raise PatchError("no archive in %s contains %s" % (data_dir, DBC_PATH))

    # Our patch only wins if no archive loaded after it also carries Achievement.dbc.
    overriding = [path for path in archives[patch_index + 1:]
                  if os.path.exists(path) and MpqArchive(path).contains(DBC_PATH)]

    patched, changed = patch_achievement_dbc(MpqArchive(source).read(DBC_PATH))
    print("Client: %s (locale %s)" % (args.client, locale))
    print("Source: %s" % source)
    print("Achievements set to both factions: %d" % changed)
    for path in overriding:
        print("WARNING: %s also contains Achievement.dbc and loads after %s; use a later --patch-number." % (path, patch_name))

    if args.check:
        print("Check only: %s %s." % (patch_path, "would be replaced" if patch_exists else "would be written"))
        return 0

    write_mpq(patch_path, {DBC_PATH: patched, MARKER_PATH: MARKER_TEXT,
                           "(listfile)": ("%s\r\n%s\r\n" % (DBC_PATH, MARKER_PATH)).encode("ascii")})
    print("Wrote %s. Restart the client to load it." % patch_path)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except PatchError as error:
        print("ERROR: %s" % error, file=sys.stderr)
        sys.exit(1)
