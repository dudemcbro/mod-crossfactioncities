# Optional achievement client patch

With `CrossFactionCities.UnlockFactionAchievements = 1`, players earn the other faction's
achievements, but the stock client does not list them in its achievement window. The
client builds that window from its own copy of `Achievement.dbc`, which marks 248
achievements as Alliance-only or Horde-only, and the server cannot change that copy.

`tools/achievement_patch.py` builds a small patch that fixes this on one client. It reads
`Achievement.dbc` from the client's own MPQ archives, marks every achievement as "both
factions" and writes the result to `Data/<locale>/patch-<locale>-4.MPQ`. Nothing else in
the file changes. The patch is optional:

- Players without it still earn and keep the other faction's achievements (the server
  also sends the usual "achievement earned" message). They just do not see them in the
  achievement window.
- Players with it see them, on any server. On a server without
  `UnlockFactionAchievements` they are listed but cannot be earned.

The module ships no game data. Each patch is built from the client it goes into, so it
always matches that client's locale and version.

## Requirements

- A WotLK 3.3.5a (build 12340) client.
- Python 3.8 or newer. Nothing else: the script uses only the standard library.

On Bazzite or another immutable Linux, the system `python3` is enough; nothing needs to
be layered or installed. On Windows, install Python from python.org or the Microsoft
Store.

## Usage

Close the game first, then run the script with the client folder (the one that contains
`Data` and `Wow.exe`):

```sh
python3 tools/achievement_patch.py "/path/to/World of Warcraft"
```

```text
Client: /path/to/World of Warcraft (locale enUS)
Source: /path/to/World of Warcraft/Data/enUS/patch-enUS-3.MPQ
Achievements set to both factions: 248
Wrote /path/to/World of Warcraft/Data/enUS/patch-enUS-4.MPQ. Restart the client to load it.
```

| Option | What it does |
| --- | --- |
| `--check` | Shows what would be done without writing anything. |
| `--remove` | Deletes the patch this script built. |
| `--locale deDE` | Picks the locale when the client has more than one. |
| `--patch-number 5` | Writes `patch-<locale>-5.MPQ` instead (4 to 9 or A to Z). |

Run the script again after you change or update the client's other patches; it replaces
its own patch.

## Safety

- The script only ever writes or deletes a patch it built itself. It marks each patch
  with a small `mod-crossfactioncities.txt` file and refuses to touch a
  `patch-<locale>-N.MPQ` without it, so pick another `--patch-number` if yours is taken.
- It reads the newest `Achievement.dbc` the client would load (following the client's
  archive order) and ignores patches it built earlier.
- It warns when an archive that loads after its patch also contains `Achievement.dbc`,
  because that archive would hide the change. Use a later `--patch-number` then.
- The client's own archives are only read.

## Server owners

You can build the patch once and share the `.MPQ` with players who use the same client
locale, instead of having every player run the script. Whether you may share patched
client files is up to you and your community; the module itself never distributes them.

## Tested

The MPQ reader and writer are checked against [StormLib](https://github.com/ladislav-zezula/StormLib),
the reference MPQ library: archives in format 1 and 2, files compressed with zlib or
bzip2, sector and single-unit storage, and encrypted files with an adjusted key. StormLib
opens the patches the script writes. The script has not been tried with every client
repack; if it reports an unsupported compression, please open an issue with the name of
the archive.
