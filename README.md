# mod-crossfactioncities

<img src="icon.png" alt="" width="64" align="right">

An [AzerothCore](https://www.azerothcore.org/) (WotLK 3.3.5a) module that lets the
Alliance and the Horde share the world. Cities become neutral ground, and optional
features remove faction limits from inns, languages, NPC speech, items and achievements.
Everything runs on the server and no DBC files or database tables are changed. The only
client patch is optional and built by each player from their own client; it is needed to
list the other faction's achievements and to use the extra languages.

- **Author:** [dudemcbro](https://github.com/dudemcbro)
- **License:** [GNU AGPL v3.0](LICENSE)
- **Works with:** AzerothCore `master` and the mod-playerbots fork (WoW 3.3.5a), with
  two small core patches ([details](docs/installation.md#requirements))

## Features

- **Neutral cities:** the other faction's NPCs in cities, towns and chosen zones do not
  attack and can be talked to, traded with and used as flight masters.
- **Friendly players:** no PvP between factions except duels. Battlegrounds, arenas,
  Wintergrasp and world PvP zones keep normal PvP.
- **Mailboxes, chests and cages** of the other faction can be used (core patch).
- **Cross-faction flight masters and taxi** (core patch).
- **Quests and reputation:** take the other faction's quests (optional SQL) and earn
  quest reputation with hostile factions (option).
- **Inns:** rest in the other faction's inns and rest areas.
- **Languages:** learn the other faction's language, or every race language (players
  need the optional client patch to use them).
- **NPC speech:** NPC chat and gossip text readable by everyone.
- **Faction-locked items:** faction mounts, PvP gear and quest items for everyone.
- **Achievements:** either faction can earn the other faction's achievements, with an
  optional client patch, built from each player's own client, to list them in the
  achievement window.

Each feature can be turned on or off in `mod_crossfactioncities.conf`.

## Quick start

```sh
cd <azerothcore>/modules
git clone https://github.com/dudemcbro/mod-crossfactioncities.git
cd <azerothcore>
modules/mod-crossfactioncities/patches/apply-core-gameobject-faction-hook.sh
modules/mod-crossfactioncities/patches/apply-core-taxi-crossfaction-hook.sh
```

Re-run CMake, rebuild, copy `etc/modules/mod_crossfactioncities.conf.dist` to
`mod_crossfactioncities.conf` and restart the worldserver. The full steps, the optional
quest SQL and uninstalling are in [docs/installation.md](docs/installation.md).

## Documentation

| Page | What's in it |
| --- | --- |
| [Installation](docs/installation.md) | Requirements, core patches, database setup, updating, uninstalling |
| [Configuration](docs/configuration.md) | Every option, when changes apply, example setups |
| [How it works](docs/how-it-works.md) | What each feature changes inside the server |
| [Compatibility](docs/compatibility.md) | Client cache, worldserver.conf settings, other modules |
| [Client patch](docs/client-patch.md) | The optional patch for the other faction's achievements and the extra languages |
| [Testing](docs/testing.md) | The live server test and the client patch test |
| [Compared with mod-Faction-Free](docs/comparison-faction-free.md) | How the two modules differ and which one to choose |
| [Core patches](patches/NOTE.md) | What each patch adds and how to check it against your core |

## mod-crossfactioncities or mod-Faction-Free?

[mod-Faction-Free](https://github.com/gitdalisar/mod-Faction-Free) makes the factions
friendly everywhere by replacing DBC files, patching every client and rewriting world
database tables. This module does it at runtime instead: per area, feature by feature,
with stock clients, and it can be switched off again without restoring backups. The
other faction's achievements (in the achievement window) and the extra languages need an
optional patch, which each player builds from their own client, and there is no teleport
network. See the
[full comparison](docs/comparison-faction-free.md).

## Contributing

Issues and pull requests are welcome. Work on a feature branch and open the pull
request against `main`. Commit messages follow
[Conventional Commits](https://www.conventionalcommits.org/) (`fix: ...`,
`feat: ...`, `docs: ...`). If you change a core patch, check that it still applies
with `git apply --check` from a clean core checkout.

## Credits

- [mod-Faction-Free](https://github.com/gitdalisar/mod-Faction-Free) by Dalisar
  (GitHub [gitdalisar](https://github.com/gitdalisar)), AGPL-3.0. Its approach to
  cross-faction play inspires this module's roadmap. These features take their idea
  from Faction-Free and are re-implemented here on the server side, without client
  patches and without any Faction-Free code or data: rest in every inn
  (`NeutralInns`), every language (`LearnLanguages`), readable NPC speech
  (`NpcSpeechUniversal`), faction-locked items (`UnlockFactionItems`) and achievements
  for both factions (`UnlockFactionAchievements`). Still on the roadmap: reputation
  with the other faction's factions beyond quests and a teleport network.

## License

Licensed under the GNU Affero General Public License v3.0. See [LICENSE](LICENSE). If you run a
modified version of this module on a server that players connect to, you must offer
those players the source code of your version.
