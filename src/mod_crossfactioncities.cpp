#include "ScriptMgr.h"
#include "AllGameObjectScript.h"
#include "BattlefieldMgr.h"
#include "Config.h"
#include "DBCStores.h"
#include "DataMap.h"
#include "GameObject.h"
#include "Log.h"
#include "Map.h"
#include "OutdoorPvPMgr.h"
#include "Pet.h"
#include "Player.h"
#include "PlayerScript.h"
#include "ReputationMgr.h"
#include "SpellAuraEffects.h"
#include "Unit.h"

#include <algorithm>
#include <exception>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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
    };

    void Trim(std::string& token)
    {
        while (!token.empty() && (token.front() == ' ' || token.front() == '\t'))
            token.erase(token.begin());
        while (!token.empty() && (token.back() == ' ' || token.back() == '\t'))
            token.pop_back();
    }

    void LoadIdSetFromConfig(char const* key, std::unordered_set<uint32>& out)
    {
        out.clear();

        std::string raw = sConfigMgr->GetOption<std::string>(key, "");
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

    void ForceFactionResend(Unit* unit)
    {
        unit->ForceValuesUpdateAtIndex(UNIT_FIELD_FACTIONTEMPLATE);
        if (Player* player = unit->ToPlayer())
            if (Pet* pet = player->GetPet())
                pet->ForceValuesUpdateAtIndex(UNIT_FIELD_FACTIONTEMPLATE);
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
        LoadIdSetFromConfig("CrossFactionCities.NeutralZoneIds", sNeutralZoneIds);
        LoadIdSetFromConfig("CrossFactionCities.NeutralAreaIds", sNeutralAreaIds);
        LoadIdSetFromConfig("CrossFactionCities.HostileZoneIds", sHostileZoneIds);
        LoadIdSetFromConfig("CrossFactionCities.HostileAreaIds", sHostileAreaIds);
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

        LOG_INFO("module", "mod-crossfactioncities: Enable={}, OnlyInCities={}, AllAreasNeutral={}, NeutralInInstances={}, WorldPvPZonesHostile={}, NpcReactionRank={}, ClientFactionTemplateId={}, PlayerNeutral={}, PlayerNeutralOnlyInCities={}, NeutralMailboxes={}, MailboxIgnoreCityCheck={}, NeutralGameObjects={}, NeutralGameObjectsOnlyInCities={}, AllowHostileQuestReputation={}, NeutralZoneIds={}, NeutralAreaIds={}, HostileZoneIds={}, HostileAreaIds={}, ExtraAllianceFactionIds={}, ExtraHordeFactionIds={}, NeverNeutralFactionIds={}",
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
            sNeutralZoneIds.size(),
            sNeutralAreaIds.size(),
            sHostileZoneIds.size(),
            sHostileAreaIds.size(),
            sExtraAllianceFactionIds.size(),
            sExtraHordeFactionIds.size(),
            sNeverNeutralFactionIds.size());

        // DBC stores are not loaded yet at the first config load; OnStartup builds the lists then.
        if (reload)
            BuildSideFactionLists();
    }

    void OnStartup() override
    {
        BuildSideFactionLists();
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
};

class CrossFactionCities_PlayerScript final : public PlayerScript
{
public:
    CrossFactionCities_PlayerScript() : PlayerScript("CrossFactionCities_PlayerScript") { }

    void OnPlayerLogin(Player* player) override
    {
        UpdatePlayerState(player, player->GetZoneId(), player->GetAreaId());
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

void Addmod_crossfactioncitiesScripts()
{
    new CrossFactionCities_WorldScript();
    new CrossFactionCities_UnitScript();
    new CrossFactionCities_PlayerScript();
    new CrossFactionCities_AllGameObjectScript();
}

void AddCrossFactionCitiesScripts()
{
    Addmod_crossfactioncitiesScripts();
}
