# Testing

The module has two test scripts. Both need only Python 3.8 or newer.

## Live server test

`tests/live_test.py` checks the features on a running dev server with a headless
client (`tests/wowclient.py`), so no game client is needed. It starts the worldserver
five times with different options, logs in an Alliance and a Horde character and checks
what the server sends and saves.

| Phase | Options | What it checks |
| --- | --- | --- |
| A | every option on, `LearnLanguages = 2` | an Undead character keeps Common at its next login; GM commands work; new characters keep their own language and learn every other one; a Horde-only item is unlocked in the item query and can be equipped by an Alliance character; a Horde-only achievement is earned; an Alliance character rests in a Horde inn; Orcish gossip text and a Stormwind NPC's Common greeting arrive as Universal; Stormwind's NPCs are neutral to a Horde character; `.reload config` switches `NpcSpeechUniversal` off |
| B | startup options off, `NpcSpeechUniversal = 0`, `LearnLanguages = 1` | only the other faction's common language is kept and the other languages lose their skill and spell; the item is Horde-only again and cannot be equipped; a second Horde-only achievement is not earned; no rest in another Horde inn; gossip text and the greeting keep their language |
| C | `LearnLanguages = 0` | characters are back to their own language only |
| D | `playercreateinfo_skills` gives every race every language, `LearnLanguages = 2` | the characters created in phase A, which only know their own language now, learn every race language at login |
| E | as D, `LearnLanguages = 0` | languages the race gets at creation are kept |

### Running it

Use a dev server, never a live realm. The script:

- creates (or resets the password of) the GM accounts `CFCTESTA` and `CFCTESTH`,
- creates two new characters on each run,
- edits `etc/modules/mod_crossfactioncities.conf` while it runs and restores it at the
  end,
- adds rows marked `cfc-live-test` to the world database's `playercreateinfo_skills` for
  phases D and E and deletes them at the end,
- starts the authserver if it is not running, and starts and stops the worldserver
  itself (stop your worldserver first).

```sh
python3 tests/live_test.py --server-dir /path/to/azerothcore/env/dist
```

`--server-dir` is the folder with `bin/worldserver`, `bin/authserver` and `etc/`. The
script reads and writes the databases with the `mysql` command line client; pass
credentials with `--mysql`, for example `--mysql "mysql -h 127.0.0.1 -u acore -pacore"`.
`--auth-db`, `--char-db`, `--world-db`, `--host`, `--auth-port` and `--world-port` change
the defaults (`acore_auth`, `acore_characters`, `acore_world`, `127.0.0.1`, 3724, 8085).

The server needs the stock world database (the test uses the Razor Hill and Crossroads
inns, npc_text 34, item 15197, the Stormwind greeter 28347 and the Mag'har and Warsong
Outrider achievements) and `Updates.EnableDatabases` set so new characters are saved
normally. The output ends with a summary, and the exit code is 0 only when every check
passes.

### What it does not cover

The headless client does not move or read object updates, so player-to-player
friendliness, mailboxes, gameobjects and taxi are not checked, and the client side of
the item and gossip caches is not seen. Those need a real client.

## Client patch test

`tests/client_patch_test.py` checks `tools/client_patch.py` against
[StormLib](https://github.com/ladislav-zezula/StormLib), the reference MPQ library. It
builds a fake client with StormLib (format 1 and 2 archives, zlib and bzip2, sectors and
single units, encrypted files), runs the script on it and reads the patch back with
StormLib. It checks the three patched files field by field against an independent
expectation, the client's archive order (every `Data` patch overrides every locale patch,
names compared without case, two-character names not loaded), the choice of patch name
(after a `Data` patch that carries one of the files, skipping a foreign one), and the
safety rules: own patches are replaced or moved but never used as a source, foreign
patches are never touched, and an explicit `--patch-number` that another archive
overrides is reported.

It needs a StormLib shared library and the 3.3.5 DBC files (the server's `data/dbc`
folder works):

```sh
git clone https://github.com/ladislav-zezula/StormLib.git
cmake -S StormLib -B StormLib/build -DBUILD_SHARED_LIBS=ON -DSTORM_USE_BUNDLED_LIBRARIES=ON
cmake --build StormLib/build
python3 tests/client_patch_test.py --stormlib StormLib/build/libstorm.so \
    --dbc-dir /path/to/azerothcore/data/dbc
```

On Bazzite or another immutable Linux, build StormLib inside a distrobox or toolbox
container (which has a compiler and cmake) rather than layering build tools onto the
host. The test itself runs with the host's `python3`.
