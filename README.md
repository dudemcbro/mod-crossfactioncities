# mod-crossfactioncities

Cross-faction city neutrality and interaction for AzerothCore (Vanilla/WotLK compatible core behavior).

## Features

* Opposing-faction city NPCs are neutral and fully interactable.
* Guards do not aggro opposing players in cities.
* Opposing players can interact normally (no PvP except duels).
* Mailboxes from opposing cities are usable.
* Reputation values are **not modified**.
* Alliance and Horde reputations are correctly **visible** on the client when two-side interaction is enabled.

## Requirements

This module depends on **small core patches** to ensure correct reputation visibility and gameobject faction handling.

See `patches/NOTE.md` for integration and validation notes.

Required patches:

* `patches/core-gameobject-faction-hook.patch`
* `patches/core-reputation-crossfaction-visibility.patch`
* `patches/core-reputation-initialize-alliance-horde.patch`

## Installation

1. Copy the module into `modules/mod-crossfactioncities`.

2. From the AzerothCore repo root, validate and apply the core patches:

```
# validate
git apply --check /absolute/path/to/mod-crossfactioncities/patches/core-gameobject-faction-hook.patch
git apply --check /absolute/path/to/mod-crossfactioncities/patches/core-reputation-crossfaction-visibility.patch
git apply --check /absolute/path/to/mod-crossfactioncities/patches/core-reputation-initialize-alliance-horde.patch

# apply (idempotent)
/absolute/path/to/mod-crossfactioncities/patches/apply-core-gameobject-faction-hook.sh
git apply /absolute/path/to/mod-crossfactioncities/patches/core-reputation-crossfaction-visibility.patch
git apply /absolute/path/to/mod-crossfactioncities/patches/core-reputation-initialize-alliance-horde.patch
```

3. Copy the config:

```
cp modules/mod-crossfactioncities/conf/mod_crossfactioncities.conf.dist \
  env/dist/etc/modules/mod_crossfactioncities.conf
```

4. Rebuild the core and restart `worldserver`.

## Config

* CrossFactionCities.Enable
* CrossFactionCities.OnlyInCities
* CrossFactionCities.IncludeGuards
* CrossFactionCities.UseFactionGroup
* CrossFactionCities.ClientFactionTemplateId
* CrossFactionCities.PlayerNeutral
* CrossFactionCities.PlayerNeutralOnlyInCities
* CrossFactionCities.NeutralMailboxes
* CrossFactionCities.MailboxIgnoreCityCheck
* CrossFactionCities.NeutralZoneIds
* CrossFactionCities.NeutralAreaIds
* CrossFactionCities.AllianceCityFactionIds
* CrossFactionCities.HordeCityFactionIds
* CrossFactionCities.GuardFactionIds

## Database

This module includes **optional SQL helpers**.

* `sql/world/base/allow_crossfaction_quests.sql`

  * Run once per server after the module is loaded.
  * Sets `quest_template.AllowableRaces = 0` so opposing-faction players can see all city quest givers.
  * Stores previous values in `quest_template_allowableraces_backup`.

* `sql/world/base/restore_crossfaction_quests.sql`

  * Restores original race masks and removes the backup table.

These SQL files are **not required** for basic cross-faction city interaction.

## Notes

* This module does not alter reputation standings.
* Reputation visibility relies on the core patches listed above.
* Enable `AllowTwoSide.Interaction.*` in `worldserver.conf`.
* Only Alliance and Horde reputations are force-visible. All other factions appear naturally.
* Add towns not flagged as cities to `NeutralZoneIds` or `NeutralAreaIds` if needed.
* Disable legacy cross-faction modules to avoid conflicts.
