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
Apply the patch before building:

```
modules/mod-crossfactioncities/patches/core-gameobject-faction-hook.patch
```

## Installation
1) Copy the module into `modules/mod-crossfactioncities` (this repo already has it).
2) Apply the core patch:
```
patch -p1 < modules/mod-crossfactioncities/patches/core-gameobject-faction-hook.patch
```
3) Copy the config:
```
cp modules/mod-crossfactioncities/conf/mod_crossfactioncities.conf.dist \
   env/dist/etc/modules/mod_crossfactioncities.conf
```
4) Rebuild the core and restart `worldserver`.

## Config
Main options (all default to enabled):
- `CrossFactionCities.Enable`
- `CrossFactionCities.OnlyInCities`
- `CrossFactionCities.IncludeGuards`
- `CrossFactionCities.UseFactionGroup`
- `CrossFactionCities.ClientFactionTemplateId` (0 = use viewer’s faction for client display)
- `CrossFactionCities.PlayerNeutral`
- `CrossFactionCities.PlayerNeutralOnlyInCities`
- `CrossFactionCities.NeutralMailboxes`
- `CrossFactionCities.MailboxIgnoreCityCheck`
- `CrossFactionCities.NeutralZoneIds` (defaults to starting zones: 1,12,14,85,141,215,3431,3526)
- `CrossFactionCities.NeutralAreaIds`

Faction ID lists (optional, only if you want strict allowlists):
- `CrossFactionCities.AllianceCityFactionIds`
- `CrossFactionCities.HordeCityFactionIds`
- `CrossFactionCities.GuardFactionIds`

## Notes
- This module does not touch reputation, `character_reputation`, or `character_settings`.
- If you want cross-faction mail/trade/chat/etc, enable the relevant
  `AllowTwoSide.Interaction.*` settings in `worldserver.conf`.
- For towns/starting areas not flagged as cities in AreaTable, add their
  IDs to `NeutralZoneIds`/`NeutralAreaIds`.
- Disable any legacy crossfaction modules to avoid conflicting hooks.
