# mod-crossfactioncities

Cross-faction city neutrality and interaction for AzerothCore WotLK.

## Features
- Opposing-faction city NPCs are neutral and fully interactable.
- Guards do not aggro opposing players in cities.
- Opposing players can interact normally (no PvP except duels).
- Mailboxes from opposing cities are usable.
- No reputation is forced or modified; rep gains remain natural.

## Requirements
This module depends on a small core hook for GameObject faction updates.

See `patches/NOTE.md` for integration and validation notes.

## Installation
1) Copy the module into `modules/mod-crossfactioncities`.
2) From the AzerothCore repo root, validate and apply the core patch:

```
# validate
git apply --check /absolute/path/to/mod-crossfactioncities/patches/core-gameobject-faction-hook.patch

# apply (idempotent)
/absolute/path/to/mod-crossfactioncities/patches/apply-core-gameobject-faction-hook.sh
```

3) Copy the config:
```
cp modules/mod-crossfactioncities/conf/mod_crossfactioncities.conf.dist \
  env/dist/etc/modules/mod_crossfactioncities.conf
```
4) Rebuild the core and restart `worldserver`.

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
- CrossFactionCities.NeutralZoneIds
- CrossFactionCities.NeutralAreaIds
- CrossFactionCities.AllianceCityFactionIds
- CrossFactionCities.HordeCityFactionIds
- CrossFactionCities.GuardFactionIds

## Notes
- This module does not touch reputation tables.
- Enable `AllowTwoSide.Interaction.*` in `worldserver.conf` for cross-faction systems.
- Add towns not flagged as cities to `NeutralZoneIds` or `NeutralAreaIds`.
- Guard neutrality infers Alliance/Horde from faction template masks (our/friendly/hostile) to cover guards without `ourMask`.
- If you run mod-playerbots, set no-PvP zones/areas so bots do not attack in towns/cities. Use:
  - `AiPlayerbot.PvpProhibitedZoneIds` and `AiPlayerbot.PvpProhibitedAreaIds` in `playerbots.conf`.
- Disable legacy crossfaction modules to avoid conflicts.
