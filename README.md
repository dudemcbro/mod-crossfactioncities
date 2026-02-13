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
- AzerothCore WotLK.
- Core hook for GameObject faction updates (provided as a patch in this module).
- Core hook for cross-faction taxi node/mount selection (provided as a patch in this module).
- Optional: gossip fallback for faction-locked taxi nodes (provided as a patch in this module).

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

4) Apply SQL (world DB) to allow quest visibility across factions:

```
mysql -u <user> -p <db> < modules/mod-crossfactioncities/sql/world/base/allow_crossfaction_quests.sql
```

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
- CrossFactionCities.IncludeGuards
- CrossFactionCities.UseFactionGroup
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

## Idempotency
- `patches/apply-core-gameobject-faction-hook.sh` can be run multiple times safely.
- `patches/apply-core-taxi-crossfaction-hook.sh` can be run multiple times safely.
- `allow_crossfaction_quests.sql` is repeat safe and preserves original masks in a backup table.
- `restore_crossfaction_quests.sql` is repeat safe and removes its backup table.

## Compatibility notes
- Player-owned pets, guardians, and charmed units inherit the owner's neutrality checks (guards should not aggro when a pet is summoned).
- Some client builds hide faction-locked taxi nodes entirely (e.g. Azuremyst/Bloodmyst flight points for Horde). If your flight map is empty on those nodes, apply `apply-core-taxi-crossfaction-gossip-fallback.sh`.
- If `InstantFlightPaths` is enabled in `worldserver.conf`, taxi travel may teleport instantly (core behavior). With `InstantFlightPaths = 2`, use the flightmaster "Toggle instant flight" option to disable it.
- This module does not modify client data (no DBC edits).
- By default it does not change reputation/achievements; `CrossFactionCities.AllowHostileQuestReputation` can enable quest rep gains for hostile factions.
- Enable `AllowTwoSide.Interaction.*` in `worldserver.conf` for additional cross-faction systems (chat, mail, etc.).
- If you run mod-playerbots, set no-PvP zones/areas so bots do not attack in towns/cities:
  - `AiPlayerbot.PvpProhibitedZoneIds` and `AiPlayerbot.PvpProhibitedAreaIds`.

## Credits
- No external assets are bundled.
