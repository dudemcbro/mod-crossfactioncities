# Optional client patch

Two options need a matching change in the client, because the client checks them itself
from its own DBC files and the server cannot change those copies:

- **`UnlockFactionAchievements`:** players earn the other faction's achievements, but the
  stock client does not list them in its achievement window. It builds that window from
  its own `Achievement.dbc`, which marks 248 achievements as Alliance-only or Horde-only.
- **`LearnLanguages`:** the server gives players the other races' languages (skill and
  spell), but the stock client only offers a language in the chat menu, lets the player
  speak it and understands it when two of its own files allow it for the player's race
  and class: `SkillLineAbility.dbc` ties each race language's spell to its native races,
  and the rows of `SkillRaceClassInfo.dbc` for other races leave out Death Knights (and,
  for Common, Undead). Without the patch the languages show in the skill window at
  300/300, but the client still only offers the player's own race languages.

`tools/client_patch.py` builds one small patch that fixes both on one client. It reads the
three files from the client's own MPQ archives and changes only these fields:

| File | Change |
| --- | --- |
| `Achievement.dbc` | Every achievement is set to both factions. |
| `SkillLineAbility.dbc` | The ten race language spells allow every race. |
| `SkillRaceClassInfo.dbc` | The race languages' rows for other races allow every class and every playable race that is not a native speaker (the same rows the module opens on the server). Native rows stay as they are. |

It writes the result to `Data/patch-<N>.MPQ` (see [Archive order](#archive-order)). The
patch is optional:

- Players without it still earn and keep the other faction's achievements (the server
  also sends the usual "achievement earned" message), but do not see them in the
  achievement window. With `LearnLanguages` they cannot use the extra languages.
- Players with it see the other faction's achievements on any server. On a server
  without `UnlockFactionAchievements` they are listed but cannot be earned. The language
  change does nothing on its own: the client still needs the server to give the language.

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
python3 tools/client_patch.py "/path/to/World of Warcraft"
```

```text
Client: /path/to/World of Warcraft (locale enUS)
Source of Achievement.dbc: /path/to/World of Warcraft/Data/enUS/patch-enUS-3.MPQ
Source of SkillLineAbility.dbc: /path/to/World of Warcraft/Data/enUS/patch-enUS-3.MPQ
Source of SkillRaceClassInfo.dbc: /path/to/World of Warcraft/Data/enUS/patch-enUS-2.MPQ
Achievements set to both factions: 248
Race languages opened to every race and class: 10 (10 spell rows, 10 skill rows)
Wrote /path/to/World of Warcraft/Data/patch-4.MPQ. Restart the client to load it.
```

| Option | What it does |
| --- | --- |
| `--check` | Shows what would be done without writing anything. |
| `--remove` | Deletes the patches this script built. |
| `--locale deDE` | Picks the locale when the client has more than one. |
| `--patch-number 5` | Writes `Data/patch-5.MPQ` (4 to 9 or A to Z) instead of choosing a name. |

Folder and archive names are matched in any case, so a Linux install with `data/enus`
works too.

Run the script again after you change or update the client's other patches; it replaces
its own patch, and moves it to a new name when another patch now needs to be overridden.

## Archive order

The 3.3.5a client looks for `Data/patch-?.MPQ` and `Data/<locale>/patch-<locale>-?.MPQ`,
where `?` is exactly one character, sorts all of them by their path without regard to
case, and gives the path that sorts last the highest priority. So every `Data/patch-X.MPQ`
overrides every locale patch, `Data/patch-B.MPQ` overrides `Data/patch-A.MPQ`, and
`patch-zz.MPQ` is not loaded at all. `Data/patch.MPQ` and
`Data/<locale>/patch-<locale>.MPQ` come below those, and the base archives below them.

Many clients (HD repacks among them) carry a `Data/patch-<letter>.MPQ` with their own copy
of one of the three files, which overrides any locale patch. That is why the script writes
to `Data`: it picks the first free `Data/patch-<N>.MPQ` from 4 to 9 and A to Z that sorts
after every other archive containing one of the three files. If no name is free it stops
and says which archives are in the way.

## Safety

- The script only ever writes or deletes patches it built itself. It marks each patch
  with a small `mod-crossfactioncities.txt` file and never touches a `patch-<N>.MPQ`
  without it.
- It reads the copy of each file the client would use (following the client's archive
  order) and ignores patches it built earlier. When it writes a patch under a new name it
  removes its old ones, including the `Data/<locale>/patch-<locale>-4.MPQ` that earlier
  versions wrote.
- With `--patch-number`, it warns when an archive that sorts after the patch also
  contains one of the files, because that archive would hide the change.
- The client's own archives are only read.

## Server owners

You can build the patch once and share the `.MPQ` with players who use the same client
locale and the same set of patches, instead of having every player run the script.
Whether you may share patched client files is up to you and your community; the module
itself never distributes them.

## Tested

The MPQ reader and writer are checked against [StormLib](https://github.com/ladislav-zezula/StormLib),
the reference MPQ library: archives in format 1 and 2, files compressed with zlib or
bzip2, sector and single-unit storage, and encrypted files with an adjusted key. StormLib
opens the patches the script writes ([testing.md](testing.md)). The archive order and the
client's language check were read from the 3.3.5a client (build 12340) and confirmed in
game on an HD client: with the patch, a Human Warrior, an Undead Warrior and a Human
Death Knight each listed and spoke all ten race languages, and the achievement window
listed both factions' achievements. The script has not been tried with every client
repack; if it reports an unsupported compression, please open an issue with the name of
the archive.
