#!/usr/bin/env python3
"""Build the optional client patch for mod-crossfactioncities.

The stock client checks two things on its own, from its own DBC files:

- Achievement.dbc marks many achievements as Alliance-only or Horde-only, and the
  achievement window lists only the player's own faction's achievements
  (CrossFactionCities.UnlockFactionAchievements).
- SkillLineAbility.dbc and SkillRaceClassInfo.dbc tie each race language to its native
  races (and leave out Death Knights), and the client only offers a language when they
  allow it for the player's race and class, whatever the server grants
  (CrossFactionCities.LearnLanguages).

This script reads those files from your own client's MPQ archives, marks every
achievement as "both factions", opens the race languages to every race and class, and
writes the result into a small patch MPQ in the client's Data folder.

No game data ships with the module: the patch is built from the client it is installed
into. Python 3.8 or newer, standard library only.

Usage:
    python3 client_patch.py "/path/to/World of Warcraft"        build and install
    python3 client_patch.py "/path/to/World of Warcraft" --check report only
    python3 client_patch.py "/path/to/World of Warcraft" --remove

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

ACHIEVEMENT_DBC = "DBFilesClient\\Achievement.dbc"
SKILL_LINE_DBC = "DBFilesClient\\SkillLine.dbc"
SKILL_LINE_ABILITY_DBC = "DBFilesClient\\SkillLineAbility.dbc"
SKILL_RACE_CLASS_INFO_DBC = "DBFilesClient\\SkillRaceClassInfo.dbc"
PATCHED_FILES = (ACHIEVEMENT_DBC, SKILL_LINE_ABILITY_DBC, SKILL_RACE_CLASS_INFO_DBC)
# Written into every patch this script builds, so it only ever replaces its own files.
MARKER_PATH = "mod-crossfactioncities.txt"
MARKER_TEXT = b"Built by mod-crossfactioncities tools/client_patch.py\r\n"

# WotLK 3.3.5 DBC layouts: field counts and the fields used here.
ACHIEVEMENT_FIELDS = 62
ACHIEVEMENT_FACTION = 1           # -1 both, 0 Horde, 1 Alliance
FACTION_BOTH = -1
SKILL_LINE_FIELDS = 56
SKILL_LINE_CATEGORY = 1
SKILL_CATEGORY_LANGUAGES = 10
SKILL_LINE_ABILITY_FIELDS = 14
ABILITY_SKILL, ABILITY_RACE_MASK = 1, 3
SKILL_RACE_CLASS_INFO_FIELDS = 8
INFO_SKILL, INFO_RACE_MASK, INFO_CLASS_MASK, INFO_SKILL_TIER = 1, 2, 3, 6
PLAYABLE_RACE_MASK = 0x6FF        # races 1-8, 10 and 11

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


class Dbc:
    """A WDBC file whose records are read and written as uint32 fields."""

    def __init__(self, name, data, field_count):
        short = name.split("\\")[-1]
        if len(data) < 20:
            raise PatchError("%s is too short" % short)
        magic, self.count, fields, record_size, string_size = struct.unpack_from("<4sIIII", data)
        if magic != b"WDBC" or fields != field_count or record_size != field_count * 4:
            raise PatchError("%s is not the WotLK 3.3.5 layout (%d fields)" % (short, fields))
        if len(data) != 20 + self.count * record_size + string_size:
            raise PatchError("%s size does not match its header" % short)
        self.data = bytearray(data)
        self.fields = field_count

    def get(self, row, field):
        return struct.unpack_from("<I", self.data, 20 + (row * self.fields + field) * 4)[0]

    def set(self, row, field, value):
        """Sets a field, returns True when it changed."""
        if self.get(row, field) == value & MASK32:
            return False
        struct.pack_into("<I", self.data, 20 + (row * self.fields + field) * 4, value & MASK32)
        return True


def patch_achievements(data):
    """Every achievement for both factions. Returns (patched data, achievements changed)."""
    dbc = Dbc(ACHIEVEMENT_DBC, data, ACHIEVEMENT_FIELDS)
    changed = sum(dbc.set(row, ACHIEVEMENT_FACTION, FACTION_BOTH) for row in range(dbc.count))
    return bytes(dbc.data), changed


def race_languages(skill_line_data, info_data):
    """{skill: native race mask} of the race languages: skills in the languages category
    with a native speaker row (no skill tier) that does not cover every playable race."""
    skill_line = Dbc(SKILL_LINE_DBC, skill_line_data, SKILL_LINE_FIELDS)
    languages = {skill_line.get(row, 0) for row in range(skill_line.count)
                 if skill_line.get(row, SKILL_LINE_CATEGORY) == SKILL_CATEGORY_LANGUAGES}
    info = Dbc(SKILL_RACE_CLASS_INFO_DBC, info_data, SKILL_RACE_CLASS_INFO_FIELDS)
    native = {}
    for row in range(info.count):
        skill = info.get(row, INFO_SKILL)
        if skill in languages and not info.get(row, INFO_SKILL_TIER):
            native[skill] = native.get(skill, 0) | (info.get(row, INFO_RACE_MASK) or PLAYABLE_RACE_MASK)
    return {skill: mask & PLAYABLE_RACE_MASK for skill, mask in native.items()
            if mask & PLAYABLE_RACE_MASK != PLAYABLE_RACE_MASK}


def patch_language_abilities(data, languages):
    """The language spells' SkillLineAbility rows allow every race (race mask 0), as the
    client only counts a language whose row matches the player's race. Returns
    (patched data, rows changed)."""
    dbc = Dbc(SKILL_LINE_ABILITY_DBC, data, SKILL_LINE_ABILITY_FIELDS)
    changed = sum(dbc.set(row, ABILITY_RACE_MASK, 0) for row in range(dbc.count)
                  if dbc.get(row, ABILITY_SKILL) in languages)
    return bytes(dbc.data), changed


def patch_language_skill_rows(data, languages):
    """The race languages' rows for other races (with a skill tier) allow every class and
    every playable race that is not a native speaker, the same rows the module opens on
    the server. Native rows stay as they are. Returns (patched data, rows changed)."""
    dbc = Dbc(SKILL_RACE_CLASS_INFO_DBC, data, SKILL_RACE_CLASS_INFO_FIELDS)
    changed = 0
    for row in range(dbc.count):
        skill = dbc.get(row, INFO_SKILL)
        if skill in languages and dbc.get(row, INFO_SKILL_TIER):
            race = dbc.set(row, INFO_RACE_MASK, PLAYABLE_RACE_MASK & ~languages[skill])
            cls = dbc.set(row, INFO_CLASS_MASK, 0)
            changed += race or cls
    return bytes(dbc.data), changed


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
    """{locale: folder path}, with the locale in the client's own spelling (enUS) whatever the
    folder's case (Linux installs often have data/enus)."""
    return {entry[:2].lower() + entry[2:].upper(): os.path.join(data_dir, entry)
            for entry in sorted(os.listdir(data_dir))
            if re.fullmatch(r"[a-zA-Z]{4}", entry) and os.path.isdir(os.path.join(data_dir, entry))}


PATCH_SUFFIXES = "456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"


def archives_in_load_order(data_dir, locale, locale_dir):
    """Every archive the client loads, lowest priority first, as paths. Later archives
    override earlier ones.

    The 3.3.5a client (12340) finds Data/patch-?.MPQ and Data/<locale>/patch-<locale>-?.MPQ
    (one character for the ?), sorts them by their path without regard to case, and gives
    the path that sorts last the highest priority. So every Data/patch-X.MPQ overrides
    every locale patch. Data/patch.MPQ and Data/<locale>/patch-<locale>.MPQ come below
    those, and the base archives below them."""
    names = [(data_dir, n) for n in ("common.MPQ", "common-2.MPQ", "expansion.MPQ", "lichking.MPQ")]
    names += [(locale_dir, n % locale) for n in ("locale-%s.MPQ", "speech-%s.MPQ", "expansion-locale-%s.MPQ",
                                                 "lichking-locale-%s.MPQ", "expansion-speech-%s.MPQ",
                                                 "lichking-speech-%s.MPQ")]
    names += [(locale_dir, "patch-%s.MPQ" % locale), (data_dir, "patch.MPQ")]
    paths = [find_entry(directory, name) or os.path.join(directory, name) for directory, name in names]
    return paths + sorted(wildcard_patches(data_dir, locale, locale_dir), key=lambda path: patch_sort_key(data_dir, path))


def wildcard_patches(data_dir, locale, locale_dir):
    """The existing files the client finds with patch-?.MPQ and patch-<locale>-?.MPQ."""
    found = []
    for directory, pattern in ((data_dir, r"patch-.\.mpq"), (locale_dir, r"patch-%s-.\.mpq" % re.escape(locale.lower()))):
        found += [os.path.join(directory, entry) for entry in os.listdir(directory)
                  if re.fullmatch(pattern, entry.lower()) and os.path.isfile(os.path.join(directory, entry))]
    return found


def patch_sort_key(data_dir, path):
    """The path the client sorts by (Data\\name or Data\\<locale>\\name), without case."""
    return ("data\\" + os.path.relpath(path, data_dir).replace(os.sep, "\\")).lower()


def is_own_patch(path):
    try:
        return MpqArchive(path).contains(MARKER_PATH)
    except (PatchError, OSError, struct.error):
        return False


def newest_source(archives, name):
    """The archive whose copy of the file the client uses, ignoring patches built here."""
    for path in reversed(archives):
        if os.path.exists(path) and not is_own_patch(path) and MpqArchive(path).contains(name):
            return path
    raise PatchError("no archive in the client contains %s" % name)


def main():
    parser = argparse.ArgumentParser(description="Build the optional client patch for mod-crossfactioncities.")
    parser.add_argument("client", help="World of Warcraft 3.3.5a folder (the one containing Data and Wow.exe)")
    parser.add_argument("--locale", help="client locale, for example enUS (detected when there is only one)")
    parser.add_argument("--patch-number",
                        help="write Data/patch-<N>.MPQ, 4-9 or A-Z (default: the first free one that "
                             "overrides every other archive with the same files)")
    parser.add_argument("--check", action="store_true", help="only report what would be done")
    parser.add_argument("--remove", action="store_true", help="delete the patches this script built")
    args = parser.parse_args()

    data_dir = find_entry(args.client, "Data")
    if not data_dir:
        raise PatchError("no Data folder in %s" % args.client)

    locales = find_locales(data_dir)
    locale = args.locale or (next(iter(locales)) if len(locales) == 1 else None)
    if not locale:
        raise PatchError("several locales found (%s), choose one with --locale" % ", ".join(locales) if locales
                         else "no locale folder (such as enUS) found in Data")
    locale = locale[:2].lower() + locale[2:].upper()
    if locale not in locales:
        raise PatchError("locale %s not found in Data" % locale)

    archives = archives_in_load_order(data_dir, locale, locales[locale])
    own = [path for path in archives if os.path.exists(path) and is_own_patch(path)]

    if args.remove:
        if not own:
            print("Nothing to remove: no patch built by this script was found.")
        for path in own:
            if args.check:
                print("Would remove %s." % path)
            else:
                os.remove(path)
                print("Removed %s." % path)
        return 0

    sources = {name: newest_source(archives, name) for name in PATCHED_FILES + (SKILL_LINE_DBC,)}
    # Archives (not built here) whose copy of a patched file the patch has to override.
    rivals = [path for path in archives
              if os.path.exists(path) and path not in own and any(MpqArchive(path).contains(n) for n in PATCHED_FILES)]

    def candidate(suffix):
        path = find_entry(data_dir, "patch-%s.MPQ" % suffix) or os.path.join(data_dir, "patch-%s.MPQ" % suffix)
        overriding = [r for r in rivals if patch_sort_key(data_dir, r) > patch_sort_key(data_dir, path)
                      and r in wildcard_patches(data_dir, locale, locales[locale])]
        return path, overriding

    if args.patch_number:
        suffix = args.patch_number.upper()
        if len(suffix) != 1 or suffix not in PATCH_SUFFIXES:
            raise PatchError("--patch-number must be one of 4-9 or A-Z")
        patch_path, overriding = candidate(suffix)
        if os.path.exists(patch_path) and patch_path not in own:
            raise PatchError("%s exists and was not built by this script; choose another --patch-number" % patch_path)
    else:
        for suffix in PATCH_SUFFIXES:
            patch_path, overriding = candidate(suffix)
            if not overriding and (not os.path.exists(patch_path) or patch_path in own):
                break
        else:
            raise PatchError("no free Data/patch-<N>.MPQ overrides %s; rename one of them or choose --patch-number"
                             % ", ".join(os.path.basename(r) for r in rivals))

    achievements, achievements_changed = patch_achievements(MpqArchive(sources[ACHIEVEMENT_DBC]).read(ACHIEVEMENT_DBC))
    languages = race_languages(MpqArchive(sources[SKILL_LINE_DBC]).read(SKILL_LINE_DBC),
                               MpqArchive(sources[SKILL_RACE_CLASS_INFO_DBC]).read(SKILL_RACE_CLASS_INFO_DBC))
    abilities, abilities_changed = patch_language_abilities(
        MpqArchive(sources[SKILL_LINE_ABILITY_DBC]).read(SKILL_LINE_ABILITY_DBC), languages)
    skill_rows, skill_rows_changed = patch_language_skill_rows(
        MpqArchive(sources[SKILL_RACE_CLASS_INFO_DBC]).read(SKILL_RACE_CLASS_INFO_DBC), languages)

    print("Client: %s (locale %s)" % (args.client, locale))
    for name in PATCHED_FILES:
        print("Source of %s: %s" % (name.split("\\")[-1], sources[name]))
    print("Achievements set to both factions: %d" % achievements_changed)
    print("Race languages opened to every race and class: %d (%d spell rows, %d skill rows)"
          % (len(languages), abilities_changed, skill_rows_changed))
    for path in overriding:
        print("WARNING: %s also contains patched files and overrides %s; leave out --patch-number."
              % (path, os.path.basename(patch_path)))
    old = [path for path in own if path != patch_path]

    if args.check:
        print("Check only: %s %s." % (patch_path, "would be replaced" if os.path.exists(patch_path) else "would be written"))
        for path in old:
            print("Check only: old patch %s would be removed." % path)
        return 0

    files = {ACHIEVEMENT_DBC: achievements, SKILL_LINE_ABILITY_DBC: abilities,
             SKILL_RACE_CLASS_INFO_DBC: skill_rows, MARKER_PATH: MARKER_TEXT}
    files["(listfile)"] = "".join(name + "\r\n" for name in files).encode("ascii")
    write_mpq(patch_path, files)
    for path in old:
        os.remove(path)
        print("Removed old patch %s." % path)
    print("Wrote %s. Restart the client to load it." % patch_path)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except PatchError as error:
        print("ERROR: %s" % error, file=sys.stderr)
        sys.exit(1)
