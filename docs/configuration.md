# Configuration

The build installs `mod_crossfactioncities.conf.dist` to your server's `etc/modules`
directory. Copy it to `mod_crossfactioncities.conf` there and edit it. Each option is
also described in the file. An option missing from your config uses the value shipped
in the `.conf.dist`.

## When changes apply

- Most options: after `.reload config`, at each player's next area change or login.
- `NpcSpeechUniversal`: at once after `.reload config`.
- `NeutralInns`, `LearnLanguages`, `UnlockFactionItems` and
  `UnlockFactionAchievements`: only after a worldserver restart, because they change
  the server's loaded game data at startup. `.reload config` logs a warning when they
  differ from what was applied. Turning `Enable` off with `.reload config` stops the
  rest of the module, but these four stay applied until the restart.

## Options

### General

| Option | Shipped | What it does |
| --- | --- | --- |
| `CrossFactionCities.Enable` | `1` | Turns the whole module on or off. |

### NPC neutrality

| Option | Shipped | What it does |
| --- | --- | --- |
| `CrossFactionCities.OnlyInCities` | `1` | NPCs are only neutral in neutral areas: cities, towns and the zones and areas listed below. `0` makes them neutral everywhere (the hostile lists still apply). |
| `CrossFactionCities.AllAreasNeutral` | `0` | Every area counts as a neutral area, also for the player and gameobject options. |
| `CrossFactionCities.NeutralZoneIds` | the playable race starting zones | Zone IDs that count as neutral areas. |
| `CrossFactionCities.NeutralAreaIds` | empty | Area IDs that count as neutral areas. |
| `CrossFactionCities.HostileZoneIds` | empty | Zone IDs where NPCs always keep their normal hostility. |
| `CrossFactionCities.HostileAreaIds` | `372,404,2157` | Area IDs where NPCs always keep their normal hostility (Tiragarde Keep, Bael'dun Digsite, Bael'dun Keep). |
| `CrossFactionCities.NeutralInInstances` | `0` | Also make NPCs neutral in dungeons and raids. Keep it off: encounters such as the Icecrown gunship use the other faction's NPCs as enemies. |
| `CrossFactionCities.NpcReactionRank` | `3` | `3` neutral (they do not attack but can be attacked), `4` friendly (they cannot be attacked). |
| `CrossFactionCities.ExtraAllianceFactionIds` | empty | Faction.dbc IDs to add to the Alliance NPC factions. |
| `CrossFactionCities.ExtraHordeFactionIds` | empty | Faction.dbc IDs to add to the Horde NPC factions. |
| `CrossFactionCities.NeverNeutralFactionIds` | empty | Faction.dbc IDs whose NPCs are never made neutral. |

### Player neutrality

| Option | Shipped | What it does |
| --- | --- | --- |
| `CrossFactionCities.PlayerNeutral` | `1` | Players of the other faction are friendly: no PvP except duels, and they can heal and buff each other. Never in battlegrounds, arenas and Wintergrasp. |
| `CrossFactionCities.PlayerNeutralOnlyInCities` | `0` | Players are only friendly when at least one of them is in a neutral area. |
| `CrossFactionCities.WorldPvPZonesHostile` | `1` | Keep PvP in the world PvP objective zones (Hellfire towers, Halaa, Eastern Plaguelands towers, Silithus, Zangarmarsh, Terokkar, Grizzly Hills). |
| `CrossFactionCities.ClientFactionTemplateId` | `0` | FactionTemplate ID shown to clients for players of the other faction. `0` uses the viewer's own faction. |

### Gameobjects (need `core-gameobject-faction-hook.patch`)

| Option | Shipped | What it does |
| --- | --- | --- |
| `CrossFactionCities.NeutralMailboxes` | `1` | The other faction's mailboxes can be used. |
| `CrossFactionCities.MailboxIgnoreCityCheck` | `1` | Mailboxes work outside neutral areas too. |
| `CrossFactionCities.NeutralGameObjects` | `1` | The other faction's chests, cages and similar objects can be used. |
| `CrossFactionCities.NeutralGameObjectsOnlyInCities` | `0` | Only in neutral areas. |

### Reputation

| Option | Shipped | What it does |
| --- | --- | --- |
| `CrossFactionCities.AllowHostileQuestReputation` | `0` | Quests give reputation with factions that are hostile to the player (the core normally skips it). |

### Inns, languages and NPC speech

| Option | Shipped | What it does |
| --- | --- | --- |
| `CrossFactionCities.NeutralInns` | `1` | Players rest in the other faction's inns and rest areas. Restart to change. |
| `CrossFactionCities.LearnLanguages` | `0` | `0` own race languages only, `1` also the other faction's common language (Orcish or Common), `2` every playable race language. Lowering it removes the extra languages at the next login. Players need the optional [client patch](client-patch.md) to use the extra languages. Restart to change. |
| `CrossFactionCities.NpcSpeechUniversal` | `1` | NPC speech and gossip text in a playable race language is sent as Universal, so everyone can read it. |

### Items and achievements

| Option | Shipped | What it does |
| --- | --- | --- |
| `CrossFactionCities.UnlockFactionItems` | `0` | Removes the faction flags and race limits from items: faction mounts, PvP gear and faction quest items can be equipped, used, looted and bought by everyone. Reputation requirements stay. Restart to change. |
| `CrossFactionCities.UnlockFactionAchievements` | `0` | Either faction can earn the other faction's achievements. The stock client lists them in its achievement window only with the optional [client patch](client-patch.md). Restart to change. |

## Example setups

**Shared cities (the shipped config).** Cities and towns are neutral ground, players of
both factions are friendly, inns work for everyone and NPC speech is readable. Gear,
achievements and languages stay per faction.

**One world, no factions.** Closest to mod-Faction-Free:

```ini
CrossFactionCities.OnlyInCities = 0
CrossFactionCities.AllowHostileQuestReputation = 1
CrossFactionCities.NeutralInns = 1
CrossFactionCities.LearnLanguages = 2
CrossFactionCities.NpcSpeechUniversal = 1
CrossFactionCities.UnlockFactionItems = 1
CrossFactionCities.UnlockFactionAchievements = 1
```

Also run `allow_crossfaction_quests.sql` and set `AllowTwoSide.Interaction.*` in
`worldserver.conf` (chat, group, guild, trade, auction and so on). With
`OnlyInCities = 0` the other faction's NPCs are neutral everywhere outside the hostile
lists, instances and PvP content.

## Options removed in the forced reaction rework

NPC neutrality works per faction instead of per NPC role, so these options no longer
exist and can be deleted from your `mod_crossfactioncities.conf`: `IncludeGuards`,
`UseFactionGroup`, `NeutralizeNonServiceNpcs`, `KeepHostileToBothFactions`,
`FactionRoleAuditLog`, `AllianceCityFactionIds`, `HordeCityFactionIds`,
`GuardFactionIds`, `NeverNeutralFactionTemplateIds` and
`NeverNeutralCreatureEntryIds`. Use `ExtraAllianceFactionIds`, `ExtraHordeFactionIds`
and `NeverNeutralFactionIds` (Faction.dbc IDs) for faction lists, and `HostileAreaIds`
to keep enemy NPCs hostile in a quest area.
