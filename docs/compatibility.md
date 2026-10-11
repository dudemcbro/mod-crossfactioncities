# Compatibility notes

## Gameplay

- Player-owned pets, guardians and charmed units follow their owner's neutrality, so
  guards do not aggro when a pet is out.
- NPC neutrality is per faction: every NPC of a listed faction is neutral in a neutral
  area, guards and quest targets included. Keep quest areas hostile with
  `HostileAreaIds`.
- By default reputation and achievements are unchanged.
  `AllowHostileQuestReputation` enables quest reputation with hostile factions, and
  `UnlockFactionAchievements` lets either faction earn every achievement.
- With `UnlockFactionItems`, the other faction's racial mounts still need Exalted
  reputation with that race's faction, as they do for other races of the same faction.
  Combine it with `AllowHostileQuestReputation` so players can earn that reputation.

## Client

- No client patch is needed for any feature.
- Clients cache item data and gossip text. After turning on `UnlockFactionItems` or
  `NpcSpeechUniversal`, players should delete the client's `Cache` folder, or tooltips
  and gossip text they have already seen stay as they were.
- The stock client lists only its own faction's achievements in the achievement window.
  With `UnlockFactionAchievements` the other faction's achievements are still tracked,
  earned and announced, and count for the server, but stay hidden in that window.
- Some clients hide faction-locked taxi nodes entirely (for example the Azuremyst and
  Bloodmyst flight points for Horde). If the flight map is empty there, apply the
  gossip fallback patch.

## worldserver.conf

- The core's `AllowTwoSide.Interaction.*` options cover other cross-faction systems
  (chat, mail, groups, guilds, trade, auction and so on).
  `AllowTwoSide.Interaction.Chat = 1` makes all player chat readable by both factions.
- `AllowTwoSide.Interaction.Group = 1` is needed for cross-faction taxi. It also lets a
  mixed-faction group queue for a battleground together. The core then puts every
  member on the leader's team without changing anyone's faction, so a member from the
  other faction is hostile to their own team. Arenas handle this with their team auras;
  for battlegrounds use a cross-faction battleground module such as
  [mod-cfbg](https://github.com/azerothcore/mod-cfbg), or queue as single-faction
  groups.
- With `InstantFlightPaths` enabled, taxi flights may be instant (core behavior). With
  `InstantFlightPaths = 2`, the flightmaster's "Toggle instant flight" option turns it
  off.

## Other modules

- **mod-playerbots:** list the cities in `AiPlayerbot.PvpProhibitedZoneIds` and
  `AiPlayerbot.PvpProhibitedAreaIds` so bots do not attack in towns.
- **mod-Faction-Free:** do not run both. Its DBC and `creature_template` changes make
  NPCs friendly everywhere for good, which overrides this module's per-area rules. See
  [comparison-faction-free.md](comparison-faction-free.md).
- Modules that also change the `SMSG_MESSAGECHAT` or `SMSG_NPC_TEXT_UPDATE` packets, or
  that rely on NPC speech keeping its race language, should be checked with
  `NpcSpeechUniversal = 0`.
