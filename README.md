# mod-crossfactioncities

An [AzerothCore](https://www.azerothcore.org/) (WotLK 3.3.5a) module that makes
cities neutral ground: players of both factions can use each other's cities, NPCs,
mailboxes and flightmasters.

## Features

- Opposing-faction city NPCs are neutral and can be talked to.
- Guards do not aggro opposing players in cities (optional).
- Opposing players can interact normally, with no PvP except duels (optional).
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

- AzerothCore WotLK (3.3.5a). The patches apply cleanly to the `custom` branch of
  [azerothcore-wotlk-playerbots-custom](https://github.com/dudemcbro/azerothcore-wotlk-playerbots-custom)
  and were first made against the
  [mod-playerbots AzerothCore fork](https://github.com/mod-playerbots/azerothcore-wotlk)
  (`Playerbot` branch, commit 2d2a522, January 2026). Other cores may need them
  adjusted.
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
| `CrossFactionCities.IncludeGuards` | `1` |
| `CrossFactionCities.UseFactionGroup` | `1` |
| `CrossFactionCities.NeutralizeNonServiceNpcs` | `0` |
| `CrossFactionCities.KeepHostileToBothFactions` | `1` |
| `CrossFactionCities.FactionRoleAuditLog` | `1` |
| `CrossFactionCities.ClientFactionTemplateId` | `0` |
| `CrossFactionCities.PlayerNeutral` | `1` |
| `CrossFactionCities.PlayerNeutralOnlyInCities` | `0` |
| `CrossFactionCities.NeutralMailboxes` | `1` |
| `CrossFactionCities.MailboxIgnoreCityCheck` | `1` |
| `CrossFactionCities.NeutralGameObjects` | `1` |
| `CrossFactionCities.NeutralGameObjectsOnlyInCities` | `0` |
| `CrossFactionCities.AllowHostileQuestReputation` | `0` |
| `CrossFactionCities.NeutralZoneIds` | the playable race starting zones |
| `CrossFactionCities.NeutralAreaIds` | empty |
| `CrossFactionCities.AllianceCityFactionIds` | empty |
| `CrossFactionCities.HordeCityFactionIds` | empty |
| `CrossFactionCities.GuardFactionIds` | empty |
| `CrossFactionCities.NeverNeutralFactionTemplateIds` | empty |
| `CrossFactionCities.NeverNeutralCreatureEntryIds` | empty |

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
- NPCs whose neutrality comes from their faction group are neutralized by role
  (service, quest, gossip, guard, assist-player); factions used only for combat stay
  hostile.
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

## License

GNU General Public License v3.0, see [LICENSE](LICENSE).
