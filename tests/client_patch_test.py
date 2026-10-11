#!/usr/bin/env python3
"""Tests tools/client_patch.py against MPQs written and read by StormLib.

Builds a fake client with StormLib (format 1 and 2 archives, zlib and bzip2, sectors and
single units, encrypted files), runs the script on it and reads the result back with
StormLib. See docs/testing.md.

    python3 tests/client_patch_test.py --stormlib /path/to/libstorm.so \\
        --dbc-dir /path/to/azerothcore/data/dbc
"""
import argparse
import ctypes
import os
import shutil
import struct
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser(description="Test tools/client_patch.py against StormLib.")
parser.add_argument("--stormlib", required=True, help="path to a StormLib shared library (libstorm.so)")
parser.add_argument("--dbc-dir", required=True, help="folder with the WotLK 3.3.5 DBC files, such as the server's data/dbc")
ARGS = parser.parse_args()

STORM = ctypes.CDLL(ARGS.stormlib)
TOOL = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools", "client_patch.py")
ROOT = os.path.join(tempfile.mkdtemp(prefix="cfc-patch-test-"), "client")
ACH = "DBFilesClient\\Achievement.dbc"
SKILL_LINE = "DBFilesClient\\SkillLine.dbc"
ABILITY = "DBFilesClient\\SkillLineAbility.dbc"
INFO = "DBFilesClient\\SkillRaceClassInfo.dbc"

HANDLE = ctypes.c_void_p
for name, args in {
    "SFileCreateArchive": [ctypes.c_char_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.POINTER(HANDLE)],
    "SFileAddFileEx": [HANDLE, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32],
    "SFileCloseArchive": [HANDLE],
    "SFileOpenArchive": [ctypes.c_char_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.POINTER(HANDLE)],
    "SFileOpenFileEx": [HANDLE, ctypes.c_char_p, ctypes.c_uint32, ctypes.POINTER(HANDLE)],
    "SFileGetFileSize": [HANDLE, ctypes.POINTER(ctypes.c_uint32)],
    "SFileReadFile": [HANDLE, ctypes.c_void_p, ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32), ctypes.c_void_p],
    "SFileCloseFile": [HANDLE],
    "SFileHasFile": [HANDLE, ctypes.c_char_p],
}.items():
    getattr(STORM, name).argtypes = args
    getattr(STORM, name).restype = ctypes.c_uint32 if name == "SFileGetFileSize" else ctypes.c_bool

COMPRESS, ENCRYPTED, FIX_KEY, SINGLE_UNIT, REPLACE = 0x200, 0x10000, 0x20000, 0x01000000, 0x80000000
ZLIB, BZIP2 = 0x02, 0x10
V1, V2, LISTFILE = 0x0, 0x01000000, 0x00100000


def storm_write(path, files, version=V1):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    handle = HANDLE()
    assert STORM.SFileCreateArchive(path.encode(), version | LISTFILE, 64, ctypes.byref(handle)), path
    for archived, (content, flags, compression) in files.items():
        temp = path + ".src"
        with open(temp, "wb") as f:
            f.write(content)
        assert STORM.SFileAddFileEx(handle, temp.encode(), archived.encode(), flags | REPLACE, compression, compression), archived
        os.remove(temp)
    STORM.SFileCloseArchive(handle)


def storm_read(path, archived):
    handle = HANDLE()
    assert STORM.SFileOpenArchive(path.encode(), 0, 0x100, ctypes.byref(handle)), "open " + path
    try:
        if not STORM.SFileHasFile(handle, archived.encode()):
            return None
        file_handle = HANDLE()
        assert STORM.SFileOpenFileEx(handle, archived.encode(), 0, ctypes.byref(file_handle))
        size = STORM.SFileGetFileSize(file_handle, None)
        buffer = ctypes.create_string_buffer(size)
        read = ctypes.c_uint32()
        assert STORM.SFileReadFile(file_handle, buffer, size, ctypes.byref(read), None)
        STORM.SFileCloseFile(file_handle)
        return buffer.raw[:read.value]
    finally:
        STORM.SFileCloseArchive(handle)


def records(dbc):
    """The DBC's records as lists of uint32 fields."""
    _, count, fields, size, _ = struct.unpack_from("<4sIIII", dbc)
    return [list(struct.unpack_from("<%dI" % fields, dbc, 20 + i * size)) for i in range(count)]


def with_records(dbc, rows):
    _, count, fields, size, _ = struct.unpack_from("<4sIIII", dbc)
    out = bytearray(dbc)
    for i, row in enumerate(rows):
        struct.pack_into("<%dI" % fields, out, 20 + i * size, *row)
    return bytes(out)


def run(*args, expect=0):
    result = subprocess.run([sys.executable, "-I", TOOL, ROOT] + list(args), capture_output=True, text=True)
    out = result.stdout + result.stderr
    print("$ client_patch.py %s -> exit %d\n%s" % (" ".join(args), result.returncode, out.rstrip()))
    assert result.returncode == expect, "unexpected exit code"
    return out


passed = 0


def check(condition, label):
    global passed
    assert condition, "FAILED: " + label
    passed += 1
    print("  ok: " + label)


def dbc_file(name):
    return open(os.path.join(ARGS.dbc_dir, name.split("\\")[-1]), "rb").read()


achievements, skill_line, abilities, info = (dbc_file(n) for n in (ACH, SKILL_LINE, ABILITY, INFO))
# Data/Patch-2.mpq gets a slightly different Achievement.dbc than the locale patch with the
# higher number, to prove that a Data patch overrides every locale patch.
newer = records(achievements)
newer[0][1] = 1                                    # first achievement: Alliance only
newer = with_records(achievements, newer)

# The expected result, worked out here from the race languages of 3.3.5 rather than the
# tool's own logic.
RACE_LANGUAGES = {98, 109, 111, 113, 115, 137, 313, 315, 673, 759}
expected_achievements = records(newer)
for row in expected_achievements:
    row[1] = 0xFFFFFFFF
expected_achievements = with_records(newer, expected_achievements)
expected_abilities = records(abilities)
for row in expected_abilities:
    if row[1] in RACE_LANGUAGES:
        row[3] = 0
expected_abilities = with_records(abilities, expected_abilities)
expected_info = records(info)
native = {}
for row in expected_info:
    if row[1] in RACE_LANGUAGES and not row[6]:
        native[row[1]] = native.get(row[1], 0) | row[2]
for row in expected_info:
    if row[1] in RACE_LANGUAGES and row[6]:
        row[2], row[3] = 0x6FF & ~native[row[1]], 0
expected_info = with_records(info, expected_info)
other_file = b"x" * 10000

shutil.rmtree(ROOT, ignore_errors=True)
data = os.path.join(ROOT, "Data")
storm_write(os.path.join(data, "common.MPQ"), {"Interface\\dummy.txt": (other_file, COMPRESS, ZLIB)})
storm_write(os.path.join(data, "enUS", "locale-enUS.MPQ"), {
    ACH: (achievements, COMPRESS | ENCRYPTED | FIX_KEY, ZLIB), SKILL_LINE: (skill_line, COMPRESS, ZLIB),
    ABILITY: (abilities, COMPRESS, ZLIB), INFO: (info, COMPRESS | ENCRYPTED, ZLIB)})
storm_write(os.path.join(data, "enUS", "patch-enUS-3.MPQ"), {ACH: (achievements, COMPRESS, BZIP2)}, V2)
storm_write(os.path.join(data, "Patch-2.mpq"), {ACH: (newer, COMPRESS | SINGLE_UNIT, BZIP2)}, V2)
# A Data patch carrying SkillLineAbility.dbc (HD clients have one), and a foreign patch
# just above it: the patch has to sort after both.
storm_write(os.path.join(data, "patch-B.MPQ"), {ABILITY: (abilities, COMPRESS, ZLIB)})
storm_write(os.path.join(data, "patch-C.MPQ"), {"Interface\\other.txt": (other_file, COMPRESS, ZLIB)})
# Two characters after "patch-": the client does not load it.
storm_write(os.path.join(data, "patch-zz.MPQ"), {ACH: (achievements, COMPRESS, ZLIB)})
patch = os.path.join(data, "patch-D.MPQ")

print("== independent reads agree with the source data")
check(storm_read(os.path.join(data, "enUS", "locale-enUS.MPQ"), ACH) == achievements, "StormLib reads its own archive")
sys.path.insert(0, os.path.dirname(TOOL))
import client_patch as tool  # noqa: E402
check(tool.MpqArchive(os.path.join(data, "enUS", "locale-enUS.MPQ")).read(ACH) == achievements, "tool reads zlib + encrypted + fix-key sectors")
check(tool.MpqArchive(os.path.join(data, "enUS", "locale-enUS.MPQ")).read(INFO) == info, "tool reads encrypted sectors without fix-key")
check(tool.MpqArchive(os.path.join(data, "Patch-2.mpq")).read(ACH) == newer, "tool reads bzip2 single-unit, format 2")
check(tool.MpqArchive(os.path.join(data, "enUS", "patch-enUS-3.MPQ")).read(ACH) == achievements, "tool reads bzip2 sectors, format 2")
check(tool.MpqArchive(os.path.join(data, "common.MPQ")).read("Interface\\dummy.txt") == other_file, "tool reads another file")

print("== load order")
order = [os.path.basename(p) for p in tool.archives_in_load_order(data, "enUS", os.path.join(data, "enUS"))]
check(order.index("patch-enUS-3.MPQ") < order.index("Patch-2.mpq") < order.index("patch-B.MPQ") < order.index("patch-C.MPQ"),
      "every Data patch loads after every locale patch, Data patches in name order without case")
check("patch-zz.MPQ" not in order, "patch names with two characters after the dash are not loaded")

print("== --check writes nothing")
out = run("--check")
check("Source of Achievement.dbc: %s" % os.path.join(data, "Patch-2.mpq") in out, "Achievement.dbc comes from the Data patch, not the locale patch")
check("Source of SkillLineAbility.dbc: %s" % os.path.join(data, "patch-B.MPQ") in out, "SkillLineAbility.dbc comes from patch-B")
check("patch-D.MPQ would be written" in out and not os.path.exists(patch), "picks patch-D (after patch-B, skipping the foreign patch-C), nothing written")

print("== build")
out = run()
check(os.path.exists(patch), "patch-D.MPQ written")
check(storm_read(patch, ACH) == expected_achievements, "Achievement.dbc: every achievement both factions, nothing else changed")
check(storm_read(patch, ABILITY) == expected_abilities, "SkillLineAbility.dbc: race language spells allow every race, nothing else changed")
check(storm_read(patch, INFO) == expected_info, "SkillRaceClassInfo.dbc: other-race language rows open, native rows unchanged")
check(storm_read(patch, "(listfile)") is not None and storm_read(patch, "mod-crossfactioncities.txt") is not None, "listfile and marker present")
check("Achievements set to both factions: %d" % sum(1 for r in records(newer) if r[1] != 0xFFFFFFFF) in out, "achievement count reported")
check("Race languages opened to every race and class: 10" in out, "language count reported")

print("== rebuild replaces its own patch")
run()
check(storm_read(patch, ACH) == expected_achievements and storm_read(patch, ABILITY) == expected_abilities, "rebuild gives the same result")

print("== old patches of this script elsewhere are replaced")
old = os.path.join(data, "enUS", "patch-enUS-4.MPQ")
shutil.copyfile(patch, old)
out = run()
check(not os.path.exists(old) and "Removed old patch" in out and os.path.exists(patch), "the old locale patch is removed")
out = run("--patch-number", "4")
check("WARNING" in out and "patch-B.MPQ" in out, "an explicit number below patch-B warns and names it")
check(os.path.exists(os.path.join(data, "patch-4.MPQ")) and not os.path.exists(patch), "patch-4 written, patch-D removed")
run()
check(os.path.exists(patch) and not os.path.exists(os.path.join(data, "patch-4.MPQ")), "default build moves it back to patch-D")

print("== a later Data patch with a patched file moves the patch up")
rival = os.path.join(data, "patch-E.MPQ")
storm_write(rival, {ACH: (achievements, COMPRESS, ZLIB)})
out = run()
check(os.path.exists(os.path.join(data, "patch-F.MPQ")) and not os.path.exists(patch), "patch-F written after patch-E, patch-D removed")
os.remove(rival)
run()
check(os.path.exists(patch) and not os.path.exists(os.path.join(data, "patch-F.MPQ")), "back to patch-D")

print("== a foreign patch file is never replaced or removed")
foreign = os.path.join(data, "patch-C.MPQ")
before = open(foreign, "rb").read()
run("--patch-number", "c", expect=1)
check(open(foreign, "rb").read() == before, "foreign patch untouched")
run("--remove", "--check")
check(os.path.exists(foreign), "foreign patch not removed")

print("== other patch numbers and bad input")
out = run("--patch-number", "z", "--check")
check("Source of Achievement.dbc: %s" % os.path.join(data, "Patch-2.mpq") in out, "own patches are never used as the source")
run("--patch-number", "3", expect=1)
run("--locale", "deDE", expect=1)

print("== remove")
run("--remove")
check(not os.path.exists(patch), "patch removed")
out = run("--remove")
check("Nothing to remove" in out, "second remove finds nothing")

print("== lowercase folders (Linux installs: data/enus)")
os.rename(os.path.join(data, "enUS"), os.path.join(data, "enus"))
os.rename(data, os.path.join(ROOT, "data"))
data = os.path.join(ROOT, "data")
out = run()
patch = os.path.join(data, "patch-D.MPQ")
check("(locale enUS)" in out and storm_read(patch, ACH) == expected_achievements, "locale found in a lowercase folder, patch written")
run("--locale", "enus", "--remove")
check(not os.path.exists(patch), "--locale accepts any case")

shutil.rmtree(os.path.dirname(ROOT), ignore_errors=True)
print("\nALL %d CHECKS PASSED" % passed)
