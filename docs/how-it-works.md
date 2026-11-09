# How it works

This page explains what the module changes inside the server for each feature. Every
feature works on the server side only: no DBC files and no database tables are changed
(apart from the optional quest SQL in
[installation.md](installation.md#database-setup)). The one client patch is optional; it
lets the client list the other faction's achievements and use the extra languages
([client-patch.md](client-patch.md)).

The module changes two kinds of things:

- **Per player, at runtime:** forced reactions, faction values sent to clients, rest
  state, languages, and the language field of NPC chat packets.
- **In memory, once at startup:** the server's loaded copy of some DBC and world data
  (inn rest areas, language race masks, item flags, achievement factions). Restarting
  the worldserver without the option restores the original data.

## NPC neutrality

The module makes the other faction's NPCs neutral with forced reactions, the mechanism
disguise spells use. The core checks forced reactions before anything else when it
decides whether two units are hostile, and the server sends the same list to the
client, so what a player sees always matches what the NPCs do.

- The NPC factions are found at startup from the DBC data: every Faction.dbc faction
  whose faction templates are all in the Alliance group, or all in the Horde group.
  The worldserver log lists them (`Alliance NPC factions`, `Horde NPC factions`).
  Player race factions and factions with templates in both groups are left alone.
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
- A spell that forces a reaction (a disguise) takes precedence. When such an aura ends
  the module puts its own reactions back at once.

## Player neutrality

Players of the other faction are made friendly by two hooks:

- `IfNormalReaction` answers "friendly" for any two players (and their pets, guardians
  and charmed units) of opposite factions, unless they are dueling or either is in a
  battleground, arena, Wintergrasp or (with `WorldPvPZonesHostile`) a world PvP zone.
  Friendly rather than neutral, because the core lets anyone attack a PvP flagged
  neutral player.
- `OnPatchValuesUpdate` rewrites the faction template field in the update packets each
  client receives, so the client shows the other player as friendly too
  (`ClientFactionTemplateId`, default: the viewer's own faction).

Faction values only reach a client when they change, so when a player's status changes
the module resends the faction field of that player and of nearby opposing players.

## Mailboxes and gameobjects

`core-gameobject-faction-hook.patch` adds a hook that runs while a gameobject's faction
is written into an update packet for one viewer. The module answers with the viewer's
own faction for opposing-faction mailboxes (`NeutralMailboxes`) and for other
faction-owned objects such as chests and cages (`NeutralGameObjects`). The object
itself keeps its faction on the server.

## Taxi

`core-taxi-crossfaction-hook.patch` lets the nearest taxi node lookup and the mount
selection use the other faction's nodes and mounts when
`AllowTwoSide.Interaction.Group = 1`. Some clients hide faction-locked nodes (a
`MountCreatureID` of 0 for one faction) and show an empty flight map there.

## Quests and reputation

- `sql/world/base/allow_crossfaction_quests.sql` sets `quest_template.AllowableRaces`
  to 0 (every race) after saving the original values in a backup table.
  `restore_crossfaction_quests.sql` puts them back.
- The core skips quest reputation for factions that are hostile to the player. With
  `AllowHostileQuestReputation`, the module applies quest reputation for those factions
  itself (no spillover). Battlegrounds and arenas are excluded.

## Inns and rest areas (`NeutralInns`)

The core gives rested XP in two ways, and both are limited to one faction:

- **Inn area triggers** (`areatrigger_tavern`, with a faction mask per trigger). The
  core only reacts to a trigger the player's faction may use. At startup the module
  lists the triggers that only one faction may use, by map. Once a second, while the
  player is not already resting in an inn, it checks whether the player stands inside
  one of the other faction's triggers and sets the inn rest flag. The core's own zone
  update removes the flag when the player walks out.
- **Rest areas** (`AREA_FLAG_REST_ZONE_ALLIANCE` and `AREA_FLAG_REST_ZONE_HORDE` in
  AreaTable.dbc, used by Northrend inns and the Dalaran pavilions). At startup the
  module gives every area with one of the two flags both flags, in the server's
  loaded copy of AreaTable.dbc. The core does the same itself for Kharanos.

The worldserver log shows how many inns and rest areas were opened.

## Languages (`LearnLanguages`)

Languages are skills. The core keeps a table (SkillRaceClassInfo.dbc) of which races and
classes may have each skill, and it deletes a skill from a character at login when no row
allows it. Each language has two kinds of rows: one for its native speakers, and one that
lets the other races hold it as a learned skill. The stock rows for other races leave out
Death Knights, Common's also leaves out Undead, and Thalassian's also lists Blood Elves,
so the core's lookup for a Blood Elf can land on either row.

- At startup the module finds the playable race languages: the language skills some race
  gets at character creation (`playercreateinfo_skills`). They are Common, Orcish,
  Darnassian, Dwarvish, Gnomish, Draenei, Taurahe, Gutterspeak, Troll and Thalassian.
- For the languages the option grants, it opens their "other races" rows to every class
  and to every playable race that is not a native speaker, in the server's loaded copy
  of the DBC. Native speakers keep using their own row.
- At every login it gives the player each granted language (skill and spell) and removes
  the languages their race does not speak natively and the option does not grant.
  Lowering `LearnLanguages` and restarting therefore takes the languages away again at
  the players' next login.
- A native speaker here is a race in the language's native DBC row. Some servers change
  `playercreateinfo_skills` so every race gets every language at creation; characters
  created before that change still learn the granted languages at login, and the module
  never removes a language the character's race gets at creation.

| `LearnLanguages` | Alliance players also learn | Horde players also learn |
| --- | --- | --- |
| `0` | nothing | nothing |
| `1` | Orcish | Common |
| `2` | every playable race language | every playable race language |

The client makes the same check from its own copies of `SkillLineAbility.dbc` and
`SkillRaceClassInfo.dbc`: it only offers a language in the chat menu, lets the player
speak it and understands it when the language spell's row matches the player's race and
a skill row matches the race and class. The stock files tie each language spell to its
native races, so players need the optional client patch ([client-patch.md](client-patch.md))
to use the languages the server gives them. Without it the languages show in the skill
window, and nothing else changes.

Player chat between factions is a separate core option:
`AllowTwoSide.Interaction.Chat = 1` in `worldserver.conf` sends all player chat as
Universal.

## NPC speech (`NpcSpeechUniversal`)

The client garbles text in a language the player does not know. NPC speech comes from
`creature_text`, `broadcast_text` and scripts, and gossip text from `npc_text`, and each
carries a language. Instead of changing those tables, the module edits two packets as
they are sent:

- `SMSG_MESSAGECHAT` with an NPC chat type (say, yell, whisper, emote, party, boss
  emote and boss whisper): the language field is set to Universal.
- `SMSG_NPC_TEXT_UPDATE` (gossip text): the language of each of the eight text options
  is set to Universal.

Only playable race languages are changed. Demonic, Draconic, Titan and the like stay as
they are. The stored texts are not changed, so `.reload` commands keep working and the
option can be switched with `.reload config`.

The client caches gossip text, so text a player has already seen stays garbled until
the client's `Cache` folder is cleared.

## Faction-locked items (`UnlockFactionItems`)

Items can be limited to one faction (the `ITEM_FLAG2_FACTION_HORDE` and
`ITEM_FLAG2_FACTION_ALLIANCE` flags in `item_template.FlagsExtra`) and to some races
(`item_template.AllowableRace`). The core reads both when a player equips, uses, loots
or buys an item and when it builds a vendor list.

At startup the module clears the two faction flags and widens the race mask to every
playable race for each item that has them, in the server's loaded item data. Items no
playable race can use are left alone. Class limits and reputation requirements stay,
so the other faction's racial mounts still need the usual reputation.

The server sends item data to clients from that loaded copy, but clients cache it, so
players should clear the client's `Cache` folder to see the new tooltips.

## Achievements (`UnlockFactionAchievements`)

Achievement.dbc gives each achievement a faction (Alliance, Horde or both). The core only
updates an achievement's criteria for players of that faction. At startup the module
sets every achievement to "both factions" in the server's loaded copy of the DBC, so
the server tracks and awards them for everyone. Reward titles still follow the player's
own faction.

The stock client reads its own Achievement.dbc to build the achievement window, and it
lists only the player's own faction's achievements there. Achievements of the other
faction are still earned and saved, and the server sends the usual "achievement earned"
message, but they do not show in that window unless the player installs the optional
patch from `tools/client_patch.py` ([client-patch.md](client-patch.md)), which marks
every achievement as both factions in the client's copy.

## Startup-only options

`NeutralInns`, `LearnLanguages`, `UnlockFactionItems` and `UnlockFactionAchievements`
change the server's loaded game data. They are applied once in `OnStartup`, before any
player can log in, and keep those values until the next restart. `.reload config` logs a
warning when one of them (or `Enable`) no longer matches what was applied. The other
options take effect at each player's next area change or login after `.reload config`.
