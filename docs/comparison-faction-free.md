# mod-crossfactioncities and mod-Faction-Free

[mod-Faction-Free](https://github.com/gitdalisar/mod-Faction-Free) by Dalisar (listed
in the AzerothCore catalogue) inspired this module's later features. Both let the two
factions share the world, but they are built in opposite ways. This page helps you pick
one. Do not run both on the same server.

## The short version

- **mod-Faction-Free** changes the game data for good. The factions become friendly
  everywhere, all the time. It replaces DBC files on the server, ships a client patch
  every player must install, rewrites tables in the world database and replaces a core
  source file.
- **mod-crossfactioncities** changes nothing on disk. It decides at runtime, per player
  and per area, and changes the server's loaded copy of the data at startup for the
  rest. Every feature is an option, and turning it off (or removing the module) brings
  the original game back.

## Side by side

| | mod-Faction-Free | mod-crossfactioncities |
| --- | --- | --- |
| **How it installs** | Replace `Achievement.dbc`, `Faction.dbc` and `FactionTemplate.dbc`, replace `PlayerUpdates.cpp` in the core, run SQL | A normal module in `modules/`, plus small core patches applied with `git apply` |
| **Client patch** | Yes, `Patch-F.mpq` for every player | No |
| **Other requirements** | mod-eluna, for the teleporter Lua scripts | None |
| **Where the factions get along** | Everywhere, permanently | Cities, towns and the zones you list, or everywhere (`OnlyInCities = 0`) |
| **Enemy NPCs** | Friendly (DBC edits, and some NPCs moved to the Stormwind or Orgrimmar faction in `creature_template`) | Neutral by default, so quest targets stay killable; friendly with `NpcReactionRank = 4` |
| **Quest areas full of the other faction's NPCs** | Handled by editing those NPCs' factions | `HostileZoneIds` and `HostileAreaIds` keep them hostile |
| **Settings** | None; changes mean editing SQL and DBC files | About 25 options in `mod_crossfactioncities.conf` |
| **Players and PvP** | Removes the PvP flag in enemy capitals | Opposing players are friendly, duels allowed; battlegrounds, arenas, Wintergrasp and world PvP zones keep PvP |
| **Reputation** | Reputation with the other faction's factions (DBC changes) | Unchanged by default; `AllowHostileQuestReputation` for quest reputation |
| **Quests** | `AllowableRaces` opened for single-faction quests | `AllowableRaces` opened for all quests, with a backup table and a restore script |
| **Inns** | `areatrigger_tavern` changed so every inn is for both factions | `NeutralInns`, no database change |
| **Languages** | `playercreateinfo_skills` changed so new characters get every language | `LearnLanguages` 0, 1 or 2, for new and existing characters, and removed again when lowered |
| **NPC speech** | `broadcast_text`, `creature_text` and `npc_text` languages set to Universal | `NpcSpeechUniversal`, by editing the packets as they are sent |
| **Faction-locked items** | `item_template` faction flags and race masks cleared | `UnlockFactionItems`, in memory |
| **Achievements** | Achievement.dbc on server and client, so they also show in the achievement window | `UnlockFactionAchievements`, server only, so the other faction's achievements are earned but hidden in the stock client's window |
| **Teleport network** | Teleporter NPCs and objects to capitals and instances | Not included; uses cross-faction flight masters instead |
| **Mailboxes, chests and cages** | Not covered | `NeutralMailboxes`, `NeutralGameObjects` |
| **Flight masters** | Not covered | Cross-faction taxi patches |
| **Core updates** | `PlayerUpdates.cpp` must be refreshed by hand after core changes | Patches touch a few lines; apply scripts check them |
| **Removing it** | Restore your backups by hand; several SQL changes have no undo | Turn the option off or remove the module; only the optional quest SQL has a restore script to run |

## Notes on the differences

- **Languages:** current AzerothCore only gives a new character a starting skill its
  race may have in SkillRaceClassInfo.dbc, and deletes other-race language skills when
  a character logs in. A `playercreateinfo_skills` change alone may therefore not give
  the languages on a current core. This module opens those DBC rows in memory first.
- **Achievements:** showing the other faction's achievements in the client's
  achievement window needs a patched client Achievement.dbc, which is what
  Faction-Free's `Patch-F.mpq` provides. This module avoids client patches, so it can
  only track and award them.
- **Data edits vs. runtime:** Faction-Free's SQL changes stay in your world database.
  Updating the core's world database can bring back original rows, and the changes
  have to be reapplied. This module's changes are made again at every startup.

## Which one to choose

Choose **mod-Faction-Free** if you want a complete "no factions" world with the least
setup thinking, you can give every player a client patch, you want the other faction's
achievements visible in the achievement window, and you want its teleport network.

Choose **mod-crossfactioncities** if you want shared cities with faction identity kept
elsewhere, or want to choose feature by feature; if players use stock clients; if you
update your core often or run mod-playerbots; or if you want to be able to switch
features off again without restoring backups.

The [example setups](configuration.md#example-setups) show a config that comes close to
Faction-Free's behavior.
