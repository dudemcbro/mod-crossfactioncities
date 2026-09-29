# mod-crossfactioncities

Cross-faction city neutrality and interaction for AzerothCore WotLK.

## Features
- Opposing-faction city NPCs are neutral and interactable.
- Guards do not aggro opposing players in cities (optional).
- Opposing players can interact normally (no PvP except duels, optional).
- Opposing-faction mailboxes are usable (optional).
- Opposing-faction-owned gameobjects (chests, cages, etc.) can be made interactable (optional).
- Hostile-faction quest reputation gains can be allowed (optional).
- Cross-faction flightmasters/taxi can be enabled (core patches; gated by `AllowTwoSide.Interaction.Group`).
- For faction-locked taxi nodes (MountCreatureID=0 for one faction), the client flight map can appear empty; an optional core patch adds a gossip-based destination list fallback.
- No reputation DB tables are modified.
- No client DBC edits are required.

## Requirements
- AzerothCore WotLK (3.3.5a). The patches were checked against the mod-playerbots
  fork of azerothcore-wotlk (commit 2d2a522, January 2026).
- bash and git (the apply scripts use `git apply`).
- Core hook for GameObject faction updates (provided as a patch in this module).
  Without it the module still builds, but the gameobject part of
  `NeutralMailboxes` and `NeutralGameObjects` silently does nothing.
- Core hook for cross-faction taxi node/mount selection (provided as a patch in this module).
- Optional: gossip fallback for faction-locked taxi nodes (provided as a patch in this module).
  It relies on the taxi hook patch above, so apply that one first.

See `patches/NOTE.md` for integration notes.

## Installation
1) Copy the module into your core:

```
modules/mod-crossfactioncities
```

2) From the AzerothCore repo root, apply core patches (idempotent):

```
modules/mod-crossfactioncities/patches/apply-core-gameobject-faction-hook.sh
modules/mod-crossfactioncities/patches/apply-core-taxi-crossfaction-hook.sh
modules/mod-crossfactioncities/patches/apply-core-taxi-crossfaction-gossip-fallback.sh
```

3) Copy the config:

```
cp modules/mod-crossfactioncities/conf/mod_crossfactioncities.conf.dist \
  env/dist/etc/modules/mod_crossfactioncities.conf
```

4) Apply SQL to the world database (usually `acore_world`) to allow quest visibility
   across factions. It is not auto-imported by the AzerothCore DB updater; run it by hand:

```
mysql -u <user> -p acore_world < modules/mod-crossfactioncities/sql/world/base/allow_crossfaction_quests.sql
```

   To undo it later, run `sql/world/base/restore_crossfaction_quests.sql` the same way.

5) Rebuild and restart `worldserver`.

## SQL
- `sql/world/base/allow_crossfaction_quests.sql`
  - Sets `quest_template.AllowableRaces = 0` for all quests so opposing-faction players can see quest givers.
  - Saves previous masks in `quest_template_allowableraces_backup` for rollback.
- `sql/world/base/restore_crossfaction_quests.sql`
  - Restores `AllowableRaces` from the backup table and drops the backup table.

## Config
- CrossFactionCities.Enable
- CrossFactionCities.OnlyInCities
- CrossFactionCities.AllAreasNeutral
- CrossFactionCities.IncludeGuards
- CrossFactionCities.UseFactionGroup
- CrossFactionCities.NeutralizeNonServiceNpcs
- CrossFactionCities.KeepHostileToBothFactions
- CrossFactionCities.FactionRoleAuditLog
- CrossFactionCities.ClientFactionTemplateId
- CrossFactionCities.PlayerNeutral
- CrossFactionCities.PlayerNeutralOnlyInCities
- CrossFactionCities.NeutralMailboxes
- CrossFactionCities.MailboxIgnoreCityCheck
- CrossFactionCities.NeutralGameObjects
- CrossFactionCities.NeutralGameObjectsOnlyInCities
- CrossFactionCities.AllowHostileQuestReputation
- CrossFactionCities.NeutralZoneIds
- CrossFactionCities.NeutralAreaIds
- CrossFactionCities.AllianceCityFactionIds
- CrossFactionCities.HordeCityFactionIds
- CrossFactionCities.GuardFactionIds
- CrossFactionCities.NeverNeutralFactionTemplateIds
- CrossFactionCities.NeverNeutralCreatureEntryIds

## Idempotency
- `patches/apply-core-gameobject-faction-hook.sh` can be run multiple times safely.
- `patches/apply-core-taxi-crossfaction-hook.sh` can be run multiple times safely.
- `patches/apply-core-taxi-crossfaction-gossip-fallback.sh` can be run multiple times safely.
- `allow_crossfaction_quests.sql` is repeat safe and preserves original masks in a backup table.
- `restore_crossfaction_quests.sql` is repeat safe and removes its backup table.

## Compatibility notes
- Player-owned pets, guardians, and charmed units inherit the owner's neutrality checks (guards should not aggro when a pet is summoned).
- Role-based neutrality is used for faction-group-derived NPCs (service/quest/gossip/guard/assist-player roles); combat-only factions are kept hostile automatically.
- Some client builds hide faction-locked taxi nodes entirely (e.g. Azuremyst/Bloodmyst flight points for Horde). If your flight map is empty on those nodes, apply `apply-core-taxi-crossfaction-gossip-fallback.sh`.
- If `InstantFlightPaths` is enabled in `worldserver.conf`, taxi travel may teleport instantly (core behavior). With `InstantFlightPaths = 2`, use the flightmaster "Toggle instant flight" option to disable it.
- This module does not modify client data (no DBC edits).
- By default it does not change reputation/achievements; `CrossFactionCities.AllowHostileQuestReputation` can enable quest rep gains for hostile factions.
- Enable `AllowTwoSide.Interaction.*` in `worldserver.conf` for additional cross-faction systems (chat, mail, etc.).
- If you run mod-playerbots, set no-PvP zones/areas so bots do not attack in towns/cities:
  - `AiPlayerbot.PvpProhibitedZoneIds` and `AiPlayerbot.PvpProhibitedAreaIds`.

## Credits
- No external assets are bundled.
