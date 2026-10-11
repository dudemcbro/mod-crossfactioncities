#include "ScriptMgr.h"
#include "AllGameObjectScript.h"
#include "BattlefieldMgr.h"
#include "Config.h"
#include "DBCStores.h"
#include "DataMap.h"
#include "DatabaseEnv.h"
#include "GameObject.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "OutdoorPvPMgr.h"
#include "Player.h"
#include "PlayerScript.h"
#include "ReputationMgr.h"
#include "ServerScript.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "Unit.h"
#include "WorldPacket.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Defined in DBCStores.cpp; the language feature needs every row, not one lookup result.
extern DBCStorage<SkillRaceClassInfoEntry> sSkillRaceClassInfoStore;

namespace
{
    bool sEnabled = true;
    bool sOnlyInCities = true;
    bool sAllAreasNeutral = false;
    bool sNeutralInInstances = false;
    bool sWorldPvPZonesHostile = true;
    ReputationRank sNpcReactionRank = REP_NEUTRAL;
    uint32 sClientFactionTemplateId = 0;
    bool sPlayerNeutral = true;
    bool sPlayerNeutralOnlyInCities = false;
    bool sNeutralMailboxes = true;
    bool sMailboxIgnoreCityCheck = true;
    bool sNeutralGameObjects = false;
    bool sNeutralGameObjectsOnlyInCities = false;
    bool sAllowHostileQuestReputation = false;
    bool sNeutralInns = true;
    uint32 sLearnLanguages = 0;
    bool sNpcSpeechUniversal = true;
    bool sUnlockFactionItems = false;
    bool sUnlockFactionAchievements = false;
    std::unordered_set<uint32> sNeutralZoneIds;
    std::unordered_set<uint32> sNeutralAreaIds;
    std::unordered_set<uint32> sHostileZoneIds;
    std::unordered_set<uint32> sHostileAreaIds;
    std::unordered_set<uint32> sExtraAllianceFactionIds;
    std::unordered_set<uint32> sExtraHordeFactionIds;
    std::unordered_set<uint32> sNeverNeutralFactionIds;

    // Faction.dbc IDs whose NPCs are made neutral to the other side's players in neutral areas.
    std::vector<uint32> sAllianceSideFactionIds;
    std::vector<uint32> sHordeSideFactionIds;
    // Any template of each of those factions, to read back a player's forced reaction.
    std::unordered_map<uint32, FactionTemplateEntry const*> sFactionTemplateByFactionId;

    std::string const PLAYER_STATE_KEY = "mod-crossfactioncities";

    struct ZoneStatus
    {
        bool battleZone = false;     // battleground, arena or battlefield (Wintergrasp)
        bool worldPvPZone = false;   // world PvP objective zone, when WorldPvPZonesHostile is set
        bool instance = false;       // dungeon or raid
        bool hostileArea = false;    // listed in HostileZoneIds or HostileAreaIds
        bool neutralArea = false;    // city, town or listed neutral zone/area
    };

    struct PlayerState : public DataMap::Base
    {
        bool initialized = false;
        ZoneStatus status;
        std::unordered_set<uint32> forcedFactionIds;
        uint32 innCheckTimer = 0;
    };

    // Options that change the server's in-memory game data. They are applied once at startup,
    // before any player logs in, and keep the values they had then until the next restart.
    struct StartupOptions
    {
        bool neutralInns = false;
        uint32 learnLanguages = 0;
        bool unlockFactionItems = false;
        bool unlockFactionAchievements = false;
    };
    StartupOptions sStartupOptions;

    uint32 sPlayableRaceMask = 0;

    // Inn area triggers only the other faction can rest in, indexed by the player's TeamId and map.
    std::unordered_map<uint32, std::vector<AreaTrigger const*>> sForeignInnsByMap[2];
    uint32 const INN_CHECK_INTERVAL = 1000;

    struct PlayerLanguage
    {
        uint32 langId;
        uint32 skillId;
        uint32 spellId;
        uint32 nativeRaceMask;      // races that know the language without the module
        uint32 dbcNativeRaceMask;   // races in its SkillRaceClassInfo rows for native speakers
    };
    // The languages of the playable races (Common, Orcish, Darnassian and so on).
    std::vector<PlayerLanguage> sPlayerLanguages;
    std::unordered_set<uint32> sPlayerLanguageIds;

    void Trim(std::string& token)
    {
        while (!token.empty() && (token.front() == ' ' || token.front() == '\t'))
            token.erase(token.begin());
        while (!token.empty() && (token.back() == ' ' || token.back() == '\t'))
            token.pop_back();
    }

    void LoadIdSetFromConfig(char const* key, std::unordered_set<uint32>& out, std::string const& defaultValue = "")
    {
        out.clear();

        std::string raw = sConfigMgr->GetOption<std::string>(key, defaultValue);
        std::stringstream ss(raw);
        std::string token;

        while (std::getline(ss, token, ','))
        {
            Trim(token);
            if (token.empty())
                continue;

            try
            {
                uint32 id = static_cast<uint32>(std::stoul(token));
                out.insert(id);
            }
            catch (std::exception const&)
            {
                LOG_WARN("module", "mod-crossfactioncities: Invalid id '{}' in {}", token, key);
            }
        }
    }

    std::string FormatIdListForLog(std::vector<uint32> const& ids)
    {
        if (ids.empty())
            return "none";

        std::ostringstream oss;
        for (size_t i = 0; i < ids.size(); ++i)
        {
            if (i)
                oss << ',';
            oss << ids[i];
        }
        return oss.str();
    }

    bool IsCityAreaByFlags(AreaTableEntry const* area)
    {
        if (!area)
            return false;

        uint32 flags = area->flags;
        return (flags & (AREA_FLAG_CAPITAL | AREA_FLAG_CITY | AREA_FLAG_TOWN | AREA_FLAG_SLAVE_CAPITAL | AREA_FLAG_SLAVE_CAPITAL2)) != 0;
    }

    ZoneStatus ComputeZoneStatus(Player const* player, uint32 zoneId, uint32 areaId)
    {
        ZoneStatus status;

        status.battleZone = player->InBattleground() || player->InArena() || sBattlefieldMgr->GetBattlefieldToZoneId(zoneId);
        status.worldPvPZone = sWorldPvPZonesHostile && sOutdoorPvPMgr->GetOutdoorPvPToZoneId(zoneId);

        if (Map const* map = player->FindMap())
            status.instance = map->IsDungeon();

        status.hostileArea = sHostileZoneIds.count(zoneId) || sHostileAreaIds.count(areaId);

        if (!status.hostileArea)
        {
            status.neutralArea = sAllAreasNeutral ||
                                 sNeutralZoneIds.count(zoneId) ||
                                 sNeutralAreaIds.count(areaId) ||
                                 IsCityAreaByFlags(sAreaTableStore.LookupEntry(areaId)) ||
                                 IsCityAreaByFlags(sAreaTableStore.LookupEntry(zoneId));
        }

        return status;
    }

    // Uses the status cached at the last area update, so the server and the client
    // (which only learns about changes at area updates) agree.
    ZoneStatus GetZoneStatus(Player const* player)
    {
        if (PlayerState const* state = player->CustomData.Get<PlayerState>(PLAYER_STATE_KEY))
        {
            if (state->initialized)
            {
                ZoneStatus status = state->status;
                // Joining a battleground or arena can happen before the next area update.
                status.battleZone = status.battleZone || player->InBattleground() || player->InArena();
                return status;
            }
        }

        return ComputeZoneStatus(player, player->GetZoneId(), player->GetAreaId());
    }

    bool IsNpcNeutralHere(ZoneStatus const& status)
    {
        if (!sEnabled || status.battleZone || status.hostileArea)
            return false;

        if (status.instance && !sNeutralInInstances)
            return false;

        return !sOnlyInCities || status.neutralArea;
    }

    void BuildSideFactionLists()
    {
        // Bit 1: some template of the faction is in the Alliance group, bit 2: in the Horde group.
        std::unordered_map<uint32, uint8> sideMasks;
        sFactionTemplateByFactionId.clear();
        for (uint32 i = 0; i < sFactionTemplateStore.GetNumRows(); ++i)
        {
            FactionTemplateEntry const* factionTemplate = sFactionTemplateStore.LookupEntry(i);
            if (!factionTemplate || !factionTemplate->faction)
                continue;

            sFactionTemplateByFactionId.emplace(factionTemplate->faction, factionTemplate);

            uint8& mask = sideMasks[factionTemplate->faction];
            if (factionTemplate->ourMask & FACTION_MASK_ALLIANCE)
                mask |= 1;
            if (factionTemplate->ourMask & FACTION_MASK_HORDE)
                mask |= 2;
        }

        // Player race factions follow the player rules (PlayerNeutral), never the NPC rules.
        std::unordered_set<uint32> playerFactionIds;
        for (uint32 i = 0; i < sChrRacesStore.GetNumRows(); ++i)
            if (ChrRacesEntry const* race = sChrRacesStore.LookupEntry(i))
                if (FactionTemplateEntry const* factionTemplate = sFactionTemplateStore.LookupEntry(race->FactionID))
                    playerFactionIds.insert(factionTemplate->faction);

        sAllianceSideFactionIds.clear();
        sHordeSideFactionIds.clear();

        // A faction without any template is not used by any NPC.
        auto isUsable = [&](uint32 factionId)
        {
            return sFactionTemplateByFactionId.count(factionId) && !playerFactionIds.count(factionId) && !sNeverNeutralFactionIds.count(factionId);
        };

        for (auto const& [factionId, mask] : sideMasks)
        {
            if (!isUsable(factionId))
                continue;

            // Factions with templates in both groups (escort factions and the like) are left alone.
            if (mask == 1)
                sAllianceSideFactionIds.push_back(factionId);
            else if (mask == 2)
                sHordeSideFactionIds.push_back(factionId);
        }

        for (uint32 factionId : sExtraAllianceFactionIds)
            if (isUsable(factionId))
                sAllianceSideFactionIds.push_back(factionId);

        for (uint32 factionId : sExtraHordeFactionIds)
            if (isUsable(factionId))
                sHordeSideFactionIds.push_back(factionId);

        for (std::vector<uint32>* list : { &sAllianceSideFactionIds, &sHordeSideFactionIds })
        {
            std::sort(list->begin(), list->end());
            list->erase(std::unique(list->begin(), list->end()), list->end());
        }

        LOG_INFO("module", "mod-crossfactioncities: Alliance NPC factions ({}): {}", sAllianceSideFactionIds.size(), FormatIdListForLog(sAllianceSideFactionIds));
        LOG_INFO("module", "mod-crossfactioncities: Horde NPC factions ({}): {}", sHordeSideFactionIds.size(), FormatIdListForLog(sHordeSideFactionIds));
    }

    bool HasForceReactionAura(Player const* player, uint32 factionId)
    {
        for (AuraEffect const* effect : player->GetAuraEffectsByType(SPELL_AURA_FORCE_REACTION))
            if (uint32(effect->GetMiscValue()) == factionId)
                return true;

        return false;
    }

    // NPC neutrality uses forced reactions (SMSG_SET_FORCED_REACTIONS). The core checks them first
    // in every reaction test and the client applies them at once, so the server and the client
    // always agree on which NPCs are hostile.
    void RefreshForcedReactions(Player* player, PlayerState& state)
    {
        std::unordered_set<uint32> wanted;

        if (IsNpcNeutralHere(state.status))
        {
            std::vector<uint32> const& factionIds = player->GetTeamId(true) == TEAM_ALLIANCE ? sHordeSideFactionIds : sAllianceSideFactionIds;
            ReputationMgr const& reputation = player->GetReputationMgr();

            for (uint32 factionId : factionIds)
            {
                // A spell that forces this reaction (disguises and the like) takes precedence.
                if (HasForceReactionAura(player, factionId))
                    continue;

                // Reputation earned with the faction already gives at least the configured reaction.
                FactionEntry const* factionEntry = sFactionStore.LookupEntry(factionId);
                if (factionEntry && factionEntry->CanHaveReputation() && !reputation.IsAtWar(factionEntry) &&
                    reputation.GetRank(factionEntry) >= sNpcReactionRank)
                    continue;

                wanted.insert(factionId);
            }
        }

        ReputationMgr& reputation = player->GetReputationMgr();
        auto currentForcedRank = [&](uint32 factionId) -> ReputationRank const*
        {
            auto itr = sFactionTemplateByFactionId.find(factionId);
            return itr != sFactionTemplateByFactionId.end() ? reputation.GetForcedRankIfAny(itr->second) : nullptr;
        };

        bool changed = false;

        for (uint32 factionId : state.forcedFactionIds)
        {
            if (wanted.count(factionId) || HasForceReactionAura(player, factionId) || !currentForcedRank(factionId))
                continue;

            reputation.ApplyForceReaction(factionId, sNpcReactionRank, false);
            changed = true;
        }

        // Applied every time: a spell aura that ends removes the faction's forced reaction too.
        for (uint32 factionId : wanted)
        {
            ReputationRank const* current = currentForcedRank(factionId);
            if (current && *current == sNpcReactionRank)
                continue;

            reputation.ApplyForceReaction(factionId, sNpcReactionRank, true);
            changed = true;
        }

        state.forcedFactionIds = std::move(wanted);

        if (changed)
            reputation.SendForceReactions();
    }

    bool ShouldForceNeutralPlayers(Player const* viewer, Player const* other)
    {
        if (!sEnabled || !sPlayerNeutral || !viewer || !other || viewer == other)
            return false;

        TeamId viewerTeam = viewer->GetTeamId(true);
        TeamId otherTeam = other->GetTeamId(true);
        if (viewerTeam == TEAM_NEUTRAL || otherTeam == TEAM_NEUTRAL || viewerTeam == otherTeam)
            return false;

        if (viewer->duel && viewer->duel->Opponent == other && viewer->duel->State == DUEL_STATE_IN_PROGRESS)
            return false;

        ZoneStatus viewerStatus = GetZoneStatus(viewer);
        ZoneStatus otherStatus = GetZoneStatus(other);

        // PvP content always wins: battlegrounds, arenas, Wintergrasp and world PvP zones.
        if (viewerStatus.battleZone || otherStatus.battleZone || viewerStatus.worldPvPZone || otherStatus.worldPvPZone)
            return false;

        // In city-only mode a player inside a city is safe from players outside it, and the other way round.
        if (sPlayerNeutralOnlyInCities && !viewerStatus.neutralArea && !otherStatus.neutralArea)
            return false;

        return true;
    }

    // Also resends pets, guardians, totems and charmed units, which follow their player's rules.
    void ForceFactionResend(Player* player)
    {
        player->ForceValuesUpdateAtIndex(UNIT_FIELD_FACTIONTEMPLATE);

        for (Unit* controlled : player->m_Controlled)
            if (controlled)
                controlled->ForceValuesUpdateAtIndex(UNIT_FIELD_FACTIONTEMPLATE);

        if (Unit* charm = player->GetCharm())
            charm->ForceValuesUpdateAtIndex(UNIT_FIELD_FACTIONTEMPLATE);
    }

    // Faction values only reach a client when they change, so after a player's status changes
    // resend the faction of that player to others and of nearby opposing players to that player.
    void RefreshPlayerFactionViews(Player* player)
    {
        if (!player->IsInWorld())
            return;

        Map* map = player->FindMap();
        if (!map)
            return;

        ForceFactionResend(player);

        TeamId team = player->GetTeamId(true);
        for (MapReference const& ref : map->GetPlayers())
        {
            Player* other = ref.GetSource();
            if (!other || other == player || other->GetTeamId(true) == team)
                continue;

            if (player->HaveAtClient(other))
                ForceFactionResend(other);
        }
    }

    void UpdatePlayerState(Player* player, uint32 zoneId, uint32 areaId)
    {
        if (!player)
            return;

        PlayerState* state = player->CustomData.GetDefault<PlayerState>(PLAYER_STATE_KEY);
        ZoneStatus oldStatus = state->status;
        bool wasInitialized = state->initialized;

        state->status = ComputeZoneStatus(player, zoneId, areaId);
        state->initialized = true;

        RefreshForcedReactions(player, *state);

        if (!sEnabled || !sPlayerNeutral)
            return;

        bool playerRulesChanged = !wasInitialized ||
                                  oldStatus.battleZone != state->status.battleZone ||
                                  oldStatus.worldPvPZone != state->status.worldPvPZone ||
                                  (sPlayerNeutralOnlyInCities && oldStatus.neutralArea != state->status.neutralArea);

        if (playerRulesChanged)
            RefreshPlayerFactionViews(player);
    }

    // A spell aura that forced a reaction removes that faction's entry when it ends, so put
    // the module's reactions back right away instead of at the next area change.
    void OnForceReactionAuraRemoved(Player* player)
    {
        if (!player->IsInWorld())
            return;

        if (PlayerState* state = player->CustomData.Get<PlayerState>(PLAYER_STATE_KEY))
            if (state->initialized)
                RefreshForcedReactions(player, *state);
    }

    bool ShouldNeutralizeMailbox(Player const* player, GameObject const* go)
    {
        if (!sEnabled || !sNeutralMailboxes || !player || !go)
            return false;

        if (go->GetGoType() != GAMEOBJECT_TYPE_MAILBOX)
            return false;

        ZoneStatus status = GetZoneStatus(player);
        if (status.battleZone)
            return false;

        if (sOnlyInCities && !status.neutralArea && !sMailboxIgnoreCityCheck)
            return false;

        return true;
    }

    bool IsAllianceFaction(FactionTemplateEntry const* factionTemplate)
    {
        return (factionTemplate->ourMask & FACTION_MASK_ALLIANCE) && !(factionTemplate->ourMask & FACTION_MASK_HORDE);
    }

    bool IsHordeFaction(FactionTemplateEntry const* factionTemplate)
    {
        return (factionTemplate->ourMask & FACTION_MASK_HORDE) && !(factionTemplate->ourMask & FACTION_MASK_ALLIANCE);
    }

    bool ShouldNeutralizeGameObject(Player const* player, GameObject const* go, uint32 faction)
    {
        if (!sEnabled || !sNeutralGameObjects || !player || !go)
            return false;

        // Skip objects that are already neutral / no faction template.
        if (!faction)
            return false;

        ZoneStatus status = GetZoneStatus(player);
        if (status.battleZone)
            return false;

        if (sNeutralGameObjectsOnlyInCities && !status.neutralArea)
            return false;

        FactionTemplateEntry const* factionTemplate = sFactionTemplateStore.LookupEntry(faction);
        if (!factionTemplate)
            return false;

        TeamId playerTeam = player->GetTeamId(true);
        if (playerTeam == TEAM_ALLIANCE)
            return IsHordeFaction(factionTemplate);
        if (playerTeam == TEAM_HORDE)
            return IsAllianceFaction(factionTemplate);

        return false;
    }

    FactionTemplateEntry const* GetAnyFactionTemplateForFaction(uint32 factionId)
    {
        for (uint32 i = 0; i < sFactionTemplateStore.GetNumRows(); ++i)
        {
            if (FactionTemplateEntry const* factionTemplate = sFactionTemplateStore.LookupEntry(i))
            {
                if (factionTemplate->faction == factionId)
                    return factionTemplate;
            }
        }
        return nullptr;
    }

    bool IsFactionHostileToPlayer(Player const* player, uint32 factionId)
    {
        if (!player)
            return false;

        FactionTemplateEntry const* templateEntry = GetAnyFactionTemplateForFaction(factionId);
        if (!templateEntry)
            return false;

        TeamId teamId = player->GetTeamId(true);
        return (teamId == TEAM_ALLIANCE) ? templateEntry->IsHostileToAlliancePlayers()
                                         : templateEntry->IsHostileToHordePlayers();
    }

    uint32 RaceMask(uint32 race)
    {
        return 1u << (race - 1);
    }

    bool IsPlayableRace(uint32 race)
    {
        for (uint32 playerClass = 1; playerClass < MAX_CLASSES; ++playerClass)
            if (sObjectMgr->GetPlayerInfo(race, playerClass))
                return true;

        return false;
    }

    void BuildPlayableRaceMask()
    {
        sPlayableRaceMask = 0;
        for (uint32 race = 1; race < sChrRacesStore.GetNumRows() && race <= 32; ++race)
            if (IsPlayableRace(race))
                sPlayableRaceMask |= RaceMask(race);
    }

    // A language is a player language when some race learns it at character creation
    // (playercreateinfo_skills). Demonic, Draconic, Titan and the like are not.
    uint32 GetCreationRaceMask(uint32 skillId)
    {
        uint32 raceMask = 0;
        for (uint32 race = 1; race < sChrRacesStore.GetNumRows() && race <= 32; ++race)
        {
            for (uint32 playerClass = 1; playerClass < MAX_CLASSES; ++playerClass)
            {
                PlayerInfo const* info = sObjectMgr->GetPlayerInfo(race, playerClass);
                if (!info)
                    continue;

                for (PlayerCreateInfoSkill const& skill : info->skills)
                    if (skill.SkillId == skillId)
                        raceMask |= RaceMask(race);
            }
        }

        return raceMask;
    }

    // Each language has two kinds of SkillRaceClassInfo rows: one for its native speakers
    // (no skill tier, the language range) and one that lets the other races hold it too
    // (with a skill tier, used for languages learned later).
    void BuildPlayerLanguages()
    {
        sPlayerLanguages.clear();
        sPlayerLanguageIds.clear();

        for (LanguageDesc const& language : lang_description)
        {
            if (!language.skill_id || !language.spell_id)
                continue;

            uint32 creationRaceMask = GetCreationRaceMask(language.skill_id);
            if (!creationRaceMask)
                continue;

            uint32 dbcNativeRaceMask = 0;
            for (SkillRaceClassInfoEntry const* entry : sSkillRaceClassInfoStore)
                if (entry->SkillID == language.skill_id && !entry->SkillTierID)
                    dbcNativeRaceMask |= entry->RaceMask ? entry->RaceMask : sPlayableRaceMask;

            dbcNativeRaceMask &= sPlayableRaceMask;
            sPlayerLanguages.push_back({ uint32(language.lang_id), language.skill_id, language.spell_id,
                                         creationRaceMask | dbcNativeRaceMask, dbcNativeRaceMask });
            sPlayerLanguageIds.insert(uint32(language.lang_id));
        }
    }

    // The other faction's common language: Orcish for the Alliance, Common for the Horde.
    bool IsLanguageGranted(PlayerLanguage const& language, TeamId team)
    {
        switch (sStartupOptions.learnLanguages)
        {
            case 1:
                return language.langId == uint32(team == TEAM_ALLIANCE ? LANG_ORCISH : LANG_COMMON);
            case 2:
                return true;
            default:
                return false;
        }
    }

    // The core drops a skill at login when no SkillRaceClassInfo row allows it for the
    // character's race and class. The rows for other races already exist, but leave out
    // Death Knights, and Thalassian's also lists Blood Elves, which makes the core's lookup
    // for them ambiguous. So the other-race rows of the granted languages are opened to
    // every class and limited to the races that are not native speakers.
    void ApplyLanguageSkillRows()
    {
        uint32 changed = 0;
        for (PlayerLanguage const& language : sPlayerLanguages)
        {
            if (!IsLanguageGranted(language, TEAM_ALLIANCE) && !IsLanguageGranted(language, TEAM_HORDE))
                continue;

            bool hasOtherRaceRow = false;
            for (SkillRaceClassInfoEntry const* entry : sSkillRaceClassInfoStore)
            {
                if (entry->SkillID != language.skillId || !entry->SkillTierID)
                    continue;

                uint32 races = (entry->RaceMask ? entry->RaceMask : sPlayableRaceMask) & ~language.dbcNativeRaceMask;
                if (!races)
                    continue;

                SkillRaceClassInfoEntry* row = const_cast<SkillRaceClassInfoEntry*>(entry);
                row->RaceMask = races;
                row->ClassMask = 0;
                hasOtherRaceRow = true;
                ++changed;
            }

            if (!hasOtherRaceRow)
                LOG_WARN("module", "mod-crossfactioncities: language skill {} has no SkillRaceClassInfo row for other races; the core will remove it at login", language.skillId);
        }

        LOG_INFO("module", "mod-crossfactioncities: LearnLanguages={}: {} language skill rows opened to every class", sStartupOptions.learnLanguages, changed);
    }

    // Grants the languages the options allow and removes other languages the player's race
    // does not speak, so lowering LearnLanguages takes them away again.
    void UpdateLanguages(Player* player)
    {
        uint32 raceMask = player->getRaceMask();
        TeamId team = player->GetTeamId(true);

        for (PlayerLanguage const& language : sPlayerLanguages)
        {
            if (language.nativeRaceMask & raceMask)
                continue;

            if (IsLanguageGranted(language, team))
            {
                if (!player->HasSkill(language.skillId))
                    player->SetSkill(language.skillId, 0, 300, 300);
                if (!player->HasSpell(language.spellId))
                    player->learnSpell(language.spellId);
            }
            else
            {
                if (player->HasSkill(language.skillId))
                    player->SetSkill(language.skillId, 0, 0, 0);
                if (player->HasSpell(language.spellId))
                    player->removeSpell(language.spellId, SPEC_MASK_ALL, false);
            }
        }
    }

    // Inns: the other faction's inn area triggers are found here, and a timer in the player
    // update checks whether the player stands in one (the core ignores them for that player).
    // Rest areas without a trigger (Northrend inns, Dalaran pavilions) carry an AreaTable flag
    // per faction, so both flags are set.
    void ApplyNeutralInns()
    {
        for (auto& innsByMap : sForeignInnsByMap)
            innsByMap.clear();

        uint32 innCount = 0;
        if (QueryResult result = WorldDatabase.Query("SELECT id FROM areatrigger_tavern"))
        {
            do
            {
                uint32 triggerId = result->Fetch()[0].Get<uint32>();
                AreaTrigger const* trigger = sObjectMgr->GetAreaTrigger(triggerId);
                if (!trigger)
                    continue;

                bool alliance = sObjectMgr->IsTavernAreaTrigger(triggerId, FACTION_MASK_ALLIANCE);
                bool horde = sObjectMgr->IsTavernAreaTrigger(triggerId, FACTION_MASK_HORDE);
                if (alliance == horde)
                    continue;

                // An Alliance inn is added for Horde players, and the other way round.
                sForeignInnsByMap[alliance ? TEAM_HORDE : TEAM_ALLIANCE][trigger->map].push_back(trigger);
                ++innCount;
            } while (result->NextRow());
        }

        uint32 const bothRestFlags = AREA_FLAG_REST_ZONE_ALLIANCE | AREA_FLAG_REST_ZONE_HORDE;
        uint32 areaCount = 0;
        for (uint32 i = 0; i < sAreaTableStore.GetNumRows(); ++i)
        {
            AreaTableEntry const* area = sAreaTableStore.LookupEntry(i);
            if (!area)
                continue;

            uint32 restFlags = area->flags & bothRestFlags;
            if (!restFlags || restFlags == bothRestFlags)
                continue;

            const_cast<AreaTableEntry*>(area)->flags |= bothRestFlags;
            ++areaCount;
        }

        LOG_INFO("module", "mod-crossfactioncities: NeutralInns: {} single-faction inns and {} single-faction rest areas opened to both factions", innCount, areaCount);
    }

    void UpdateForeignInnRest(Player* player, uint32 diff)
    {
        PlayerState* state = player->CustomData.Get<PlayerState>(PLAYER_STATE_KEY);
        if (!state)
            return;

        if (state->innCheckTimer > diff)
        {
            state->innCheckTimer -= diff;
            return;
        }
        state->innCheckTimer = INN_CHECK_INTERVAL;

        if (player->HasRestFlag(REST_FLAG_IN_TAVERN) || player->IsInFlight())
            return;

        TeamId team = player->GetTeamId(true);
        if (team != TEAM_ALLIANCE && team != TEAM_HORDE)
            return;

        auto itr = sForeignInnsByMap[team].find(player->GetMapId());
        if (itr == sForeignInnsByMap[team].end())
            return;

        // The core's own zone update removes the flag again when the player leaves the inn.
        for (AreaTrigger const* trigger : itr->second)
        {
            if (player->IsInAreaTriggerRadius(trigger))
            {
                player->SetRestFlag(REST_FLAG_IN_TAVERN, trigger->entry);
                return;
            }
        }
    }

    // Clears the Horde-only and Alliance-only item flags and widens race limits to every
    // playable race: equipping, using, looting, buying and vendor lists all read these.
    void UnlockFactionItems()
    {
        ItemFlags2 const factionFlags = ItemFlags2(ITEM_FLAG2_FACTION_HORDE | ITEM_FLAG2_FACTION_ALLIANCE);
        uint32 count = 0;

        for (ItemTemplate* proto : *sObjectMgr->GetItemTemplateStoreFast())
        {
            if (!proto)
                continue;

            bool factionLocked = (proto->Flags2 & factionFlags) != 0;
            uint32 allowedRaces = proto->AllowableRace & sPlayableRaceMask;
            // Items no playable race can use are broken data; leave them as they are.
            bool raceLocked = allowedRaces && allowedRaces != sPlayableRaceMask;
            if (!factionLocked && !raceLocked)
                continue;

            proto->Flags2 = ItemFlags2(proto->Flags2 & ~factionFlags);
            if (raceLocked)
                proto->AllowableRace |= sPlayableRaceMask;
            ++count;
        }

        LOG_INFO("module", "mod-crossfactioncities: UnlockFactionItems: {} faction or race locked items opened to every race", count);
    }

    void UnlockFactionAchievements()
    {
        uint32 count = 0;
        for (uint32 i = 0; i < sAchievementStore.GetNumRows(); ++i)
        {
            AchievementEntry const* achievement = sAchievementStore.LookupEntry(i);
            if (!achievement || achievement->requiredFaction == ACHIEVEMENT_FACTION_ANY)
                continue;

            const_cast<AchievementEntry*>(achievement)->requiredFaction = ACHIEVEMENT_FACTION_ANY;
            ++count;
        }

        LOG_INFO("module", "mod-crossfactioncities: UnlockFactionAchievements: {} faction achievements opened to both factions", count);
    }

    // The startup options as the config sets them now; all off while the module is disabled.
    StartupOptions ConfiguredStartupOptions()
    {
        StartupOptions options;
        if (!sEnabled)
            return options;

        options.neutralInns = sNeutralInns;
        options.learnLanguages = sLearnLanguages;
        options.unlockFactionItems = sUnlockFactionItems;
        options.unlockFactionAchievements = sUnlockFactionAchievements;
        return options;
    }

    void ApplyStartupOptions()
    {
        BuildPlayableRaceMask();
        BuildPlayerLanguages();

        sStartupOptions = ConfiguredStartupOptions();

        if (sStartupOptions.neutralInns)
            ApplyNeutralInns();
        if (sStartupOptions.learnLanguages)
            ApplyLanguageSkillRows();
        if (sStartupOptions.unlockFactionItems)
            UnlockFactionItems();
        if (sStartupOptions.unlockFactionAchievements)
            UnlockFactionAchievements();
    }

    void WarnIfStartupOptionsChanged()
    {
        StartupOptions configured = ConfiguredStartupOptions();
        if (configured.neutralInns != sStartupOptions.neutralInns ||
            configured.learnLanguages != sStartupOptions.learnLanguages ||
            configured.unlockFactionItems != sStartupOptions.unlockFactionItems ||
            configured.unlockFactionAchievements != sStartupOptions.unlockFactionAchievements)
            LOG_WARN("module", "mod-crossfactioncities: changes to NeutralInns, LearnLanguages, UnlockFactionItems and UnlockFactionAchievements (and to Enable, for them) take effect after a worldserver restart");
    }

    bool IsMonsterChat(uint8 chatType)
    {
        switch (chatType)
        {
            case CHAT_MSG_MONSTER_SAY:
            case CHAT_MSG_MONSTER_PARTY:
            case CHAT_MSG_MONSTER_YELL:
            case CHAT_MSG_MONSTER_WHISPER:
            case CHAT_MSG_MONSTER_EMOTE:
            case CHAT_MSG_RAID_BOSS_EMOTE:
            case CHAT_MSG_RAID_BOSS_WHISPER:
                return true;
            default:
                return false;
        }
    }

    void MakeLanguageUniversal(WorldPacket& packet, std::size_t pos)
    {
        if (pos + sizeof(uint32) <= packet.size() && sPlayerLanguageIds.count(packet.read<uint32>(pos)))
            packet.put<uint32>(pos, uint32(LANG_UNIVERSAL));
    }

    // Skips a null-terminated string; returns false if the packet ends first.
    bool SkipCString(WorldPacket const& packet, std::size_t& pos)
    {
        uint8 const* data = packet.contents();
        while (pos < packet.size())
            if (data[pos++] == 0)
                return true;

        return false;
    }

    // NPC speech and gossip text in a player race language reach the client as Universal, so
    // every player can read them. This edits the packet as it is sent: the stored texts are not
    // changed, and the same text is sent the same way to every player.
    void MakeNpcSpeechUniversal(WorldPacket& packet)
    {
        switch (packet.GetOpcode())
        {
            case SMSG_MESSAGECHAT:
                // uint8 chat type, uint32 language, ...
                if (packet.size() >= 5 && IsMonsterChat(packet.contents()[0]))
                    MakeLanguageUniversal(packet, 1);
                break;
            case SMSG_NPC_TEXT_UPDATE:
            {
                // uint32 text id, then per option: float probability, two strings, uint32 language,
                // three emote pairs.
                std::size_t pos = sizeof(uint32);
                for (uint8 i = 0; i < MAX_GOSSIP_TEXT_OPTIONS; ++i)
                {
                    pos += sizeof(float);
                    if (!SkipCString(packet, pos) || !SkipCString(packet, pos))
                        return;

                    MakeLanguageUniversal(packet, pos);
                    pos += sizeof(uint32) + MAX_GOSSIP_TEXT_EMOTES * 2 * sizeof(uint32);
                }
                break;
            }
            default:
                break;
        }
    }
}

class CrossFactionCities_WorldScript final : public WorldScript
{
public:
    CrossFactionCities_WorldScript() : WorldScript("CrossFactionCities_WorldScript") { }

    void OnAfterConfigLoad(bool reload) override
    {
        sEnabled = sConfigMgr->GetOption<bool>("CrossFactionCities.Enable", true);
        sOnlyInCities = sConfigMgr->GetOption<bool>("CrossFactionCities.OnlyInCities", true);
        sAllAreasNeutral = sConfigMgr->GetOption<bool>("CrossFactionCities.AllAreasNeutral", false);
        sNeutralInInstances = sConfigMgr->GetOption<bool>("CrossFactionCities.NeutralInInstances", false);
        sWorldPvPZonesHostile = sConfigMgr->GetOption<bool>("CrossFactionCities.WorldPvPZonesHostile", true);
        sClientFactionTemplateId = sConfigMgr->GetOption<uint32>("CrossFactionCities.ClientFactionTemplateId", 0);
        sPlayerNeutral = sConfigMgr->GetOption<bool>("CrossFactionCities.PlayerNeutral", true);
        sPlayerNeutralOnlyInCities = sConfigMgr->GetOption<bool>("CrossFactionCities.PlayerNeutralOnlyInCities", false);
        sNeutralMailboxes = sConfigMgr->GetOption<bool>("CrossFactionCities.NeutralMailboxes", true);
        sMailboxIgnoreCityCheck = sConfigMgr->GetOption<bool>("CrossFactionCities.MailboxIgnoreCityCheck", true);
        sNeutralGameObjects = sConfigMgr->GetOption<bool>("CrossFactionCities.NeutralGameObjects", false);
        sNeutralGameObjectsOnlyInCities = sConfigMgr->GetOption<bool>("CrossFactionCities.NeutralGameObjectsOnlyInCities", false);
        sAllowHostileQuestReputation = sConfigMgr->GetOption<bool>("CrossFactionCities.AllowHostileQuestReputation", false);
        sNeutralInns = sConfigMgr->GetOption<bool>("CrossFactionCities.NeutralInns", true);
        sLearnLanguages = sConfigMgr->GetOption<uint32>("CrossFactionCities.LearnLanguages", 0);
        sNpcSpeechUniversal = sConfigMgr->GetOption<bool>("CrossFactionCities.NpcSpeechUniversal", true);
        sUnlockFactionItems = sConfigMgr->GetOption<bool>("CrossFactionCities.UnlockFactionItems", false);
        sUnlockFactionAchievements = sConfigMgr->GetOption<bool>("CrossFactionCities.UnlockFactionAchievements", false);
        // Defaults match the shipped .conf.dist, for configs that predate an option.
        LoadIdSetFromConfig("CrossFactionCities.NeutralZoneIds", sNeutralZoneIds, "1,12,14,85,141,215,3430,3431,3433,3524,3525,3526");
        LoadIdSetFromConfig("CrossFactionCities.NeutralAreaIds", sNeutralAreaIds);
        LoadIdSetFromConfig("CrossFactionCities.HostileZoneIds", sHostileZoneIds);
        LoadIdSetFromConfig("CrossFactionCities.HostileAreaIds", sHostileAreaIds, "372,404,2157");
        LoadIdSetFromConfig("CrossFactionCities.ExtraAllianceFactionIds", sExtraAllianceFactionIds);
        LoadIdSetFromConfig("CrossFactionCities.ExtraHordeFactionIds", sExtraHordeFactionIds);
        LoadIdSetFromConfig("CrossFactionCities.NeverNeutralFactionIds", sNeverNeutralFactionIds);

        uint32 npcReactionRank = sConfigMgr->GetOption<uint32>("CrossFactionCities.NpcReactionRank", REP_NEUTRAL);
        if (npcReactionRank != REP_NEUTRAL && npcReactionRank != REP_FRIENDLY)
        {
            LOG_WARN("module", "mod-crossfactioncities: CrossFactionCities.NpcReactionRank must be 3 (neutral) or 4 (friendly), using 3");
            npcReactionRank = REP_NEUTRAL;
        }
        sNpcReactionRank = ReputationRank(npcReactionRank);

        if (sLearnLanguages > 2)
        {
            LOG_WARN("module", "mod-crossfactioncities: CrossFactionCities.LearnLanguages must be 0, 1 or 2, using 0");
            sLearnLanguages = 0;
        }

        LOG_INFO("module", "mod-crossfactioncities: Enable={}, OnlyInCities={}, AllAreasNeutral={}, NeutralInInstances={}, WorldPvPZonesHostile={}, NpcReactionRank={}, ClientFactionTemplateId={}, PlayerNeutral={}, PlayerNeutralOnlyInCities={}, NeutralMailboxes={}, MailboxIgnoreCityCheck={}, NeutralGameObjects={}, NeutralGameObjectsOnlyInCities={}, AllowHostileQuestReputation={}, NeutralInns={}, LearnLanguages={}, NpcSpeechUniversal={}, UnlockFactionItems={}, UnlockFactionAchievements={}, NeutralZoneIds={}, NeutralAreaIds={}, HostileZoneIds={}, HostileAreaIds={}, ExtraAllianceFactionIds={}, ExtraHordeFactionIds={}, NeverNeutralFactionIds={}",
            sEnabled ? 1 : 0,
            sOnlyInCities ? 1 : 0,
            sAllAreasNeutral ? 1 : 0,
            sNeutralInInstances ? 1 : 0,
            sWorldPvPZonesHostile ? 1 : 0,
            uint32(sNpcReactionRank),
            sClientFactionTemplateId,
            sPlayerNeutral ? 1 : 0,
            sPlayerNeutralOnlyInCities ? 1 : 0,
            sNeutralMailboxes ? 1 : 0,
            sMailboxIgnoreCityCheck ? 1 : 0,
            sNeutralGameObjects ? 1 : 0,
            sNeutralGameObjectsOnlyInCities ? 1 : 0,
            sAllowHostileQuestReputation ? 1 : 0,
            sNeutralInns ? 1 : 0,
            sLearnLanguages,
            sNpcSpeechUniversal ? 1 : 0,
            sUnlockFactionItems ? 1 : 0,
            sUnlockFactionAchievements ? 1 : 0,
            sNeutralZoneIds.size(),
            sNeutralAreaIds.size(),
            sHostileZoneIds.size(),
            sHostileAreaIds.size(),
            sExtraAllianceFactionIds.size(),
            sExtraHordeFactionIds.size(),
            sNeverNeutralFactionIds.size());

        // DBC stores are not loaded yet at the first config load; OnStartup builds the lists then.
        if (reload)
        {
            BuildSideFactionLists();
            WarnIfStartupOptionsChanged();
        }
    }

    void OnStartup() override
    {
        BuildSideFactionLists();
        ApplyStartupOptions();
    }
};

class CrossFactionCities_UnitScript final : public UnitScript
{
public:
    CrossFactionCities_UnitScript() : UnitScript("CrossFactionCities_UnitScript") { }

    bool IfNormalReaction(Unit const* a, Unit const* b, ReputationRank& rank) override
    {
        Player const* playerA = a ? a->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr;
        Player const* playerB = b ? b->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr;

        // Friendly rather than neutral: the core lets anyone attack a neutral player who is PvP
        // flagged, which would allow PvP the client does not show.
        if (playerA && playerB && ShouldForceNeutralPlayers(playerA, playerB))
        {
            rank = REP_FRIENDLY;
            return false;
        }

        return true;
    }

    void OnPatchValuesUpdate(Unit const* unit, ByteBuffer& valuesUpdateBuf, BuildValuesCachePosPointers& posPointers, Player* target) override
    {
        if (!unit || !target)
            return;

        if (posPointers.UnitFieldFactionTemplatePos < 0)
            return;

        // Only player-controlled units: NPCs are handled with forced reactions.
        Player const* owner = unit->GetCharmerOrOwnerPlayerOrPlayerItself();
        if (!owner || owner == target || !ShouldForceNeutralPlayers(target, owner))
            return;

        uint32 clientFactionTemplateId = sClientFactionTemplateId ? sClientFactionTemplateId : target->GetFaction();
        if (!clientFactionTemplateId)
            return;

        valuesUpdateBuf.put(posPointers.UnitFieldFactionTemplatePos, uint32(clientFactionTemplateId));
    }

    void OnAuraRemove(Unit* unit, AuraApplication* aurApp, AuraRemoveMode /*mode*/) override
    {
        Player* player = unit ? unit->ToPlayer() : nullptr;
        if (!player || !aurApp)
            return;

        if (aurApp->GetBase()->GetSpellInfo()->HasAura(SPELL_AURA_FORCE_REACTION))
            OnForceReactionAuraRemoved(player);
    }
};

class CrossFactionCities_PlayerScript final : public PlayerScript
{
public:
    CrossFactionCities_PlayerScript() : PlayerScript("CrossFactionCities_PlayerScript") { }

    void OnPlayerLogin(Player* player) override
    {
        UpdatePlayerState(player, player->GetZoneId(), player->GetAreaId());

        if (sEnabled)
            UpdateLanguages(player);
    }

    void OnPlayerUpdate(Player* player, uint32 diff) override
    {
        if (sEnabled && sStartupOptions.neutralInns)
            UpdateForeignInnRest(player, diff);
    }

    void OnPlayerUpdateArea(Player* player, uint32 /*oldArea*/, uint32 newArea) override
    {
        // Zone changes also run the area update, so this covers both.
        UpdatePlayerState(player, player->GetZoneId(), newArea);
    }

    void OnPlayerGiveReputation(Player* player, int32 factionID, float& amount, ReputationSource repSource) override
    {
        if (!sEnabled || !sAllowHostileQuestReputation || !player)
            return;

        if (amount == 0.0f)
            return;

        if (player->InBattleground() || player->InArena())
            return;

        switch (repSource)
        {
            case REPUTATION_SOURCE_QUEST:
            case REPUTATION_SOURCE_DAILY_QUEST:
            case REPUTATION_SOURCE_WEEKLY_QUEST:
            case REPUTATION_SOURCE_MONTHLY_QUEST:
            case REPUTATION_SOURCE_REPEATABLE_QUEST:
                break;
            default:
                return;
        }

        FactionEntry const* factionEntry = sFactionStore.LookupEntry(uint32(factionID));
        if (!factionEntry)
            return;

        if (!IsFactionHostileToPlayer(player, factionEntry->ID))
            return;

        // Core skips quest reputation for hostile factions; apply it here so cross-faction questing can progress reps.
        // Disable spillover to avoid unintended reputation changes when forcing hostile-faction quest rep.
        player->GetReputationMgr().ModifyReputation(factionEntry, amount, true);
    }
};

class CrossFactionCities_AllGameObjectScript final : public AllGameObjectScript
{
public:
    CrossFactionCities_AllGameObjectScript() : AllGameObjectScript("CrossFactionCities_AllGameObjectScript") { }

    void OnGameObjectFactionCalc(GameObject const* go, Player const* viewer, uint32& faction)
    {
        if (!go || !viewer)
            return;

        // Mailboxes: keep existing behavior and configurability.
        if (ShouldNeutralizeMailbox(viewer, go))
        {
            faction = sClientFactionTemplateId ? sClientFactionTemplateId : viewer->GetFaction();
            return;
        }

        // Other faction-owned interactables (chests, cages, etc.) can be neutralized globally.
        if (ShouldNeutralizeGameObject(viewer, go, faction))
            faction = sClientFactionTemplateId ? sClientFactionTemplateId : viewer->GetFaction();
    }
};

class CrossFactionCities_ServerScript final : public ServerScript
{
public:
    CrossFactionCities_ServerScript() : ServerScript("CrossFactionCities_ServerScript", { SERVERHOOK_CAN_PACKET_SEND }) { }

    bool CanPacketSend(WorldSession* /*session*/, WorldPacket const& packet) override
    {
        // The packets edited here are built for sending and not read again by the server.
        if (sEnabled && sNpcSpeechUniversal)
            MakeNpcSpeechUniversal(const_cast<WorldPacket&>(packet));

        return true;
    }
};

void Addmod_crossfactioncitiesScripts()
{
    new CrossFactionCities_WorldScript();
    new CrossFactionCities_ServerScript();
    new CrossFactionCities_UnitScript();
    new CrossFactionCities_PlayerScript();
    new CrossFactionCities_AllGameObjectScript();
}

void AddCrossFactionCitiesScripts()
{
    Addmod_crossfactioncitiesScripts();
}
