#!/usr/bin/env python3
"""Tests tools/achievement_patch.py against MPQs written and read by StormLib.

Builds a fake client with StormLib (format 1 and 2 archives, zlib and bzip2, sectors and
single units, encrypted files), runs the script on it and reads the result back with
StormLib. See docs/testing.md.

    python3 tests/achievement_patch_test.py --stormlib /path/to/libstorm.so \\
        --dbc /path/to/azerothcore/data/dbc/Achievement.dbc
"""
import argparse
import ctypes
import os
import shutil
import struct
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser(description="Test tools/achievement_patch.py against StormLib.")
parser.add_argument("--stormlib", required=True, help="path to a StormLib shared library (libstorm.so)")
parser.add_argument("--dbc", required=True, help="a WotLK 3.3.5 Achievement.dbc, such as the server's data/dbc copy")
ARGS = parser.parse_args()

STORM = ctypes.CDLL(ARGS.stormlib)
TOOL = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools", "achievement_patch.py")
SERVER_DBC = ARGS.dbc
ROOT = os.path.join(tempfile.mkdtemp(prefix="cfc-patch-test-"), "client")
DBC = "DBFilesClient\\Achievement.dbc"

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


def factions(dbc):
    _, count, _, size, _ = struct.unpack_from("<4sIIII", dbc)
    return [struct.unpack_from("<i", dbc, 20 + i * size + 4)[0] for i in range(count)]


def run(*args, expect=0):
    result = subprocess.run([sys.executable, "-I", TOOL, ROOT] + list(args), capture_output=True, text=True)
    out = result.stdout + result.stderr
    print("$ achievement_patch.py %s -> exit %d\n%s" % (" ".join(args), result.returncode, out.rstrip()))
    assert result.returncode == expect, "unexpected exit code"
    return out


passed = 0


def check(condition, label):
    global passed
    assert condition, "FAILED: " + label
    passed += 1
    print("  ok: " + label)


original = open(SERVER_DBC, "rb").read()
# patch-enUS-3 gets a slightly different copy, to prove the newest source wins.
newer = bytearray(original)
struct.pack_into("<i", newer, 20 + 4, 1)     # first achievement: Alliance only
newer = bytes(newer)
other_file = b"x" * 10000

shutil.rmtree(ROOT, ignore_errors=True)
data = os.path.join(ROOT, "Data")
storm_write(os.path.join(data, "common.MPQ"), {"Interface\\dummy.txt": (other_file, COMPRESS, ZLIB)})
storm_write(os.path.join(data, "enUS", "locale-enUS.MPQ"), {DBC: (original, COMPRESS | ENCRYPTED | FIX_KEY, ZLIB)})
storm_write(os.path.join(data, "patch-3.MPQ"), {DBC: (original, COMPRESS | SINGLE_UNIT, BZIP2)}, V2)
storm_write(os.path.join(data, "enUS", "Patch-enUS-3.mpq"), {DBC: (newer, COMPRESS, BZIP2)}, V2)

print("== independent reads agree with the source data")
check(storm_read(os.path.join(data, "enUS", "locale-enUS.MPQ"), DBC) == original, "StormLib reads its own archive")
sys.path.insert(0, os.path.dirname(TOOL))
import achievement_patch as tool  # noqa: E402
check(tool.MpqArchive(os.path.join(data, "enUS", "locale-enUS.MPQ")).read(DBC) == original, "tool reads zlib + encrypted + fix-key sectors")
check(tool.MpqArchive(os.path.join(data, "patch-3.MPQ")).read(DBC) == original, "tool reads bzip2 single-unit, format 2")
check(tool.MpqArchive(os.path.join(data, "enUS", "Patch-enUS-3.mpq")).read(DBC) == newer, "tool reads bzip2 sectors, format 2")
check(tool.MpqArchive(os.path.join(data, "common.MPQ")).read("Interface\\dummy.txt") == other_file, "tool reads another file")

print("== --check writes nothing")
out = run("--check")
patch = os.path.join(data, "enUS", "patch-enUS-4.MPQ")
check("Patch-enUS-3.mpq" in out and not os.path.exists(patch), "source is the newest archive (mixed case name), nothing written")

print("== build")
out = run()
check(os.path.exists(patch), "patch-enUS-4.MPQ written")
built = storm_read(patch, DBC)
check(built is not None, "StormLib opens the patch and finds Achievement.dbc")
check(all(f == -1 for f in factions(built)), "every achievement is both factions")
expected = bytearray(newer)
for i in range(len(factions(newer))):
    struct.pack_into("<i", expected, 20 + i * 248 + 4, -1)
check(built == bytes(expected), "all other bytes unchanged from the newest source")
check(storm_read(patch, "(listfile)") is not None, "listfile present")
check("Achievements set to both factions: %d" % (sum(1 for f in factions(newer) if f != -1)) in out, "count reported")

print("== rebuild replaces its own patch")
run()
check(storm_read(patch, DBC) == bytes(expected), "rebuild gives the same result")

print("== a later archive with Achievement.dbc triggers a warning")
storm_write(os.path.join(data, "enUS", "patch-enUS-5.MPQ"), {DBC: (original, COMPRESS, ZLIB)})
out = run("--check")
check("WARNING" in out and "patch-enUS-5.MPQ" in out, "warning names the overriding archive")
os.remove(os.path.join(data, "enUS", "patch-enUS-5.MPQ"))

print("== a foreign patch file is never replaced")
foreign = os.path.join(data, "enUS", "patch-enUS-6.MPQ")
storm_write(foreign, {"Interface\\other.txt": (other_file, COMPRESS, ZLIB)})
before = open(foreign, "rb").read()
run("--patch-number", "6", expect=1)
check(open(foreign, "rb").read() == before, "foreign patch untouched")
run("--patch-number", "6", "--remove", expect=1)
check(os.path.exists(foreign), "foreign patch not removed")

print("== other patch numbers and bad input")
out = run("--patch-number", "z", "--check")
check("Patch-enUS-3.mpq" in out, "own patches are never used as the source")
run("--patch-number", "z")
check(storm_read(os.path.join(data, "enUS", "patch-enUS-Z.MPQ"), DBC) == bytes(expected), "letter patch number works")
run("--patch-number", "z", "--remove")
run("--patch-number", "3", expect=1)
run("--locale", "deDE", expect=1)

print("== remove")
run("--remove")
check(not os.path.exists(patch), "patch removed")
run("--remove")

shutil.rmtree(os.path.dirname(ROOT), ignore_errors=True)
print("\nALL %d CHECKS PASSED" % passed)
