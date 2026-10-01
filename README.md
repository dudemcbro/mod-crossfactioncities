# mod-crossfactioncities

An [AzerothCore](https://www.azerothcore.org/) (WotLK 3.3.5a) module that makes
cities neutral ground: players of both factions can use each other's cities, NPCs,
mailboxes and flightmasters.

## Features

- Opposing-faction NPCs in cities, towns and chosen zones are neutral: they do not
  attack and can be talked to, traded with and used as flightmasters.
- Server and client always agree on which NPCs are hostile (see
  [How NPC neutrality works](#how-npc-neutrality-works)).
- Opposing players are friendly, with no PvP except duels (optional). Battlegrounds,
  arenas, Wintergrasp and world PvP zones keep normal PvP.
- Opposing-faction mailboxes are usable (optional).
- Opposing-faction gameobjects (chests, cages and the like) can be made usable
  (optional, needs a core patch).
- Quest reputation with hostile factions can be allowed (optional).
- Cross-faction flightmasters and taxi (core patch, active when
  `AllowTwoSide.Interaction.Group = 1` in `worldserver.conf`).
- For faction-locked taxi nodes (a `MountCreatureID` of 0 for one faction) the client
  flight map can be empty; an optional core patch adds a gossip list of destinations
  instead.
- No reputation tables and no client DBC files are changed.

## Requirements

- AzerothCore WotLK (3.3.5a). The patches apply cleanly to stock
  [azerothcore-wotlk](https://github.com/azerothcore/azerothcore-wotlk) `master`
  (commit 51c32b5, September 2026), to the
  [mod-playerbots AzerothCore fork](https://github.com/mod-playerbots/azerothcore-wotlk)
  (`Playerbot` branch, January and September 2026) and to the `custom` branch of
  [azerothcore-wotlk-playerbots-custom](https://github.com/dudemcbro/azerothcore-wotlk-playerbots-custom).
  Other cores may need them adjusted.
- bash and git (the apply scripts use `git apply`).
- Core patches shipped in [`patches/`](patches/NOTE.md):
  - `core-gameobject-faction-hook.patch`: a GameObject faction hook. Without it the
    module still builds, but the gameobject part of `NeutralMailboxes` and all of
    `NeutralGameObjects` silently do nothing.
  - `core-taxi-crossfaction-hook.patch`: cross-faction taxi node and mount selection.
  - `core-taxi-crossfaction-gossip-fallback.patch` (optional): the gossip destination
    list for faction-locked taxi nodes. It relies on the taxi hook patch, so apply that
    one first.

See [patches/NOTE.md](patches/NOTE.md) for how to check the patches against your core.

## Installation

1. Clone the module into the core's `modules` directory:

   ```sh
   cd <azerothcore>/modules
   git clone https://github.com/dudemcbro/mod-crossfactioncities.git
   ```

2. Apply the core patches from the root of the AzerothCore source tree. Each script
   applies its patch once and does nothing if it is already applied:

   ```sh
   cd <azerothcore>
   modules/mod-crossfactioncities/patches/apply-core-gameobject-faction-hook.sh
   modules/mod-crossfactioncities/patches/apply-core-taxi-crossfaction-hook.sh
   modules/mod-crossfactioncities/patches/apply-core-taxi-crossfaction-gossip-fallback.sh
   ```

3. Re-run CMake and rebuild the core.
4. Copy the config (below) and set up the database.
5. Restart `worldserver`.

## Configuration

The build installs `mod_crossfactioncities.conf.dist` to your server's `etc/modules`
directory. Copy it to `mod_crossfactioncities.conf` there and edit it; each option is
described in the file. Options (value as shipped):

| Option | Shipped value |
| --- | --- |
| `CrossFactionCities.Enable` | `1` |
| `CrossFactionCities.OnlyInCities` | `1` |
| `CrossFactionCities.AllAreasNeutral` | `0` |
| `CrossFactionCities.NeutralZoneIds` | the playable race starting zones |
| `CrossFactionCities.NeutralAreaIds` | empty |
| `CrossFactionCities.HostileZoneIds` | empty |
| `CrossFactionCities.HostileAreaIds` | `372,404,2157` (Tiragarde Keep, Bael'dun Digsite, Bael'dun Keep) |
| `CrossFactionCities.NeutralInInstances` | `0` |
| `CrossFactionCities.NpcReactionRank` | `3` (neutral) |
| `CrossFactionCities.ExtraAllianceFactionIds` | empty |
| `CrossFactionCities.ExtraHordeFactionIds` | empty |
| `CrossFactionCities.NeverNeutralFactionIds` | empty |
| `CrossFactionCities.PlayerNeutral` | `1` |
| `CrossFactionCities.PlayerNeutralOnlyInCities` | `0` |
| `CrossFactionCities.WorldPvPZonesHostile` | `1` |
| `CrossFactionCities.ClientFactionTemplateId` | `0` |
| `CrossFactionCities.NeutralMailboxes` | `1` |
| `CrossFactionCities.MailboxIgnoreCityCheck` | `1` |
| `CrossFactionCities.NeutralGameObjects` | `1` |
| `CrossFactionCities.NeutralGameObjectsOnlyInCities` | `0` |
| `CrossFactionCities.AllowHostileQuestReputation` | `0` |

Changes made with `.reload config` apply to each player at their next area change or
login.

### Options removed in the forced reaction rework

NPC neutrality now works per faction instead of per NPC role, so these options no
longer exist and can be deleted from your `mod_crossfactioncities.conf`:
`IncludeGuards`, `UseFactionGroup`, `NeutralizeNonServiceNpcs`,
`KeepHostileToBothFactions`, `FactionRoleAuditLog`, `AllianceCityFactionIds`,
`HordeCityFactionIds`, `GuardFactionIds`, `NeverNeutralFactionTemplateIds` and
`NeverNeutralCreatureEntryIds`. Use `ExtraAllianceFactionIds`,
`ExtraHordeFactionIds` and `NeverNeutralFactionIds` (Faction.dbc IDs) for faction
lists, and `HostileAreaIds` to keep enemy NPCs hostile in a quest area.

## How NPC neutrality works

The module makes the other faction's NPCs neutral with forced reactions, the mechanism
disguise spells use. The core checks forced reactions before anything else when it
decides whether two units are hostile, and the server sends the same list to the
client, so what a player sees always matches what the NPCs do.

- The NPC factions are found at startup from the DBC data: every Faction.dbc faction
  whose faction templates are all in the Alliance group, or all in the Horde group.
  The worldserver log lists them (`Alliance NPC factions`, `Horde NPC factions`).
- A player gets the forced reactions while standing in a neutral area (a city, a town
  or a zone or area from `NeutralZoneIds` and `NeutralAreaIds`) and loses them on
  leaving it. The check runs at every area change.
- `HostileZoneIds` and `HostileAreaIds` win over every other rule. The defaults keep
  the Kul Tiras marines at Tiragarde Keep and the Bael'dun dwarves hostile, since Horde
  quests send players to kill them inside neutral starting zones.
- Battlegrounds, arenas and Wintergrasp never get forced reactions, and dungeons and
  raids only with `NeutralInInstances = 1`.
- Neutral NPCs (`NpcReactionRank = 3`) do not attack but can be attacked, so quest
  targets stay killable. Reputation that already gives the configured reaction or
  better is left to decide the reaction on its own.

## Database setup

`sql/world/base/allow_crossfaction_quests.sql` lets players see and take the other
faction's quests. The worldserver's updater only imports a module's `data/sql/world`,
`data/sql/characters` and `data/sql/auth` folders, so this file is not imported
automatically. Run it by hand on the world database (usually `acore_world`), from the
root of the AzerothCore source tree:

```sh
mysql -u <user> -p acore_world < modules/mod-crossfactioncities/sql/world/base/allow_crossfaction_quests.sql
```

| File | What it does |
| --- | --- |
| `sql/world/base/allow_crossfaction_quests.sql` | Saves every quest's `AllowableRaces` in `quest_template_allowableraces_backup`, then sets `quest_template.AllowableRaces = 0` for all quests. Safe to run again: the first backup of each quest is kept. |
| `sql/world/base/restore_crossfaction_quests.sql` | Restores `AllowableRaces` from the backup table and drops it. Safe to run again. |

## Compatibility notes

- Player-owned pets, guardians and charmed units follow their owner's neutrality, so
  guards do not aggro when a pet is out.
- NPC neutrality is per faction: every NPC of a listed faction is neutral in a neutral
  area, guards and quest targets included. Keep quest areas hostile with
  `HostileAreaIds`.
- `AllowTwoSide.Interaction.Group = 1` lets a mixed-faction group queue for a
  battleground together. The core then puts every member on the leader's team without
  changing anyone's faction, so a member from the other faction is hostile to their own
  team. Arenas handle this with their team auras; for battlegrounds use a cross-faction
  battleground module such as
  [mod-cfbg](https://github.com/azerothcore/mod-cfbg), or queue as single-faction
  groups.
- Some clients hide faction-locked taxi nodes entirely (for example the Azuremyst and
  Bloodmyst flight points for Horde). If the flight map is empty there, apply the
  gossip fallback patch.
- With `InstantFlightPaths` enabled in `worldserver.conf`, taxi flights may be instant
  (core behavior). With `InstantFlightPaths = 2`, the flightmaster's "Toggle instant
  flight" option turns it off.
- By default reputation and achievements are unchanged;
  `CrossFactionCities.AllowHostileQuestReputation` enables quest reputation with
  hostile factions.
- The core's `AllowTwoSide.Interaction.*` options in `worldserver.conf` cover other
  cross-faction systems (chat, mail and so on).
- With mod-playerbots, list the cities in `AiPlayerbot.PvpProhibitedZoneIds` and
  `AiPlayerbot.PvpProhibitedAreaIds` so bots do not attack in towns.

## Updating

```sh
cd <azerothcore>/modules/mod-crossfactioncities
git pull
```

Run the apply scripts again (they skip patches that are already applied; if a patch
changed, reverse the old version with `git apply -R` first), re-run CMake and rebuild.

## Uninstall

1. Restore the quest race masks (from the root of the AzerothCore source tree):

   ```sh
   mysql -u <user> -p acore_world < modules/mod-crossfactioncities/sql/world/base/restore_crossfaction_quests.sql
   ```

2. Reverse the core patches you applied, from the core root:

   ```sh
   git apply -R modules/mod-crossfactioncities/patches/core-taxi-crossfaction-gossip-fallback.patch
   git apply -R modules/mod-crossfactioncities/patches/core-taxi-crossfaction-hook.patch
   git apply -R modules/mod-crossfactioncities/patches/core-gameobject-faction-hook.patch
   ```

3. Delete `modules/mod-crossfactioncities` and your server's
   `etc/modules/mod_crossfactioncities.conf`, re-run CMake and rebuild.

## Contributing

Issues and pull requests are welcome. Work on a feature branch and open the pull
request against `main`. Commit messages follow
[Conventional Commits](https://www.conventionalcommits.org/) (`fix: ...`,
`feat: ...`, `docs: ...`). If you change a core patch, check that it still applies
with `git apply --check` from a clean core checkout.

## Credits

- [mod-Faction-Free](https://github.com/gitdalisar/mod-Faction-Free) by Dalisar
  (GitHub [gitdalisar](https://github.com/gitdalisar)), AGPL-3.0. Its approach to
  cross-faction play inspires this module's roadmap: reputation with the other
  faction, quests, inns, languages, faction-locked gear, PvP flags in enemy
  capitals, a teleport network and achievements for both factions. Those ideas are
  re-implemented here on the server side, without client patches; no Faction-Free
  code or data is included. Each feature that takes an idea from Faction-Free names
  it here when it is added.

## License

GNU Affero General Public License v3.0, see [LICENSE](LICENSE). If you run a
modified version of this module on a server that players connect to, you must offer
those players the source code of your version.
