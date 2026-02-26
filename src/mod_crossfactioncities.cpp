#include "ScriptMgr.h"
#include "AllGameObjectScript.h"
#include "Config.h"
#include "Creature.h"
#include "DBCStores.h"
#include "GameObject.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ReputationMgr.h"
#include "PlayerScript.h"
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
    bool sIncludeGuards = true;
    bool sUseFactionGroup = true;
    bool sNeutralizeNonServiceNpcs = false;
    bool sKeepHostileToBothFactions = true;
    bool sFactionRoleAuditLog = true;
    uint32 sClientFactionTemplateId = 0;
    bool sPlayerNeutral = true;
    bool sPlayerNeutralOnlyInCities = false;
    bool sNeutralMailboxes = true;
    bool sMailboxIgnoreCityCheck = true;
    bool sNeutralGameObjects = false;
    bool sNeutralGameObjectsOnlyInCities = false;
    bool sAllowHostileQuestReputation = false;
    std::unordered_set<uint32> sAllianceCityFactionIds;
    std::unordered_set<uint32> sHordeCityFactionIds;
    std::unordered_set<uint32> sGuardFactionIds;
    std::unordered_set<uint32> sNeutralZoneIds;
    std::unordered_set<uint32> sNeutralAreaIds;
    std::unordered_set<uint32> sNeverNeutralFactionTemplateIds;
    std::unordered_set<uint32> sNeverNeutralCreatureEntryIds;
    std::unordered_set<uint32> sMixedUseFactionTemplateIds;
    std::unordered_set<uint32> sCombatOnlyFactionTemplateIds;

    constexpr uint32 NEUTRAL_INTERACTION_NPC_FLAGS =
        UNIT_NPC_FLAG_GOSSIP |
        UNIT_NPC_FLAG_QUESTGIVER |
        UNIT_NPC_FLAG_TRAINER |
        UNIT_NPC_FLAG_VENDOR_MASK |
        UNIT_NPC_FLAG_FLIGHTMASTER |
        UNIT_NPC_FLAG_PETITIONER |
        UNIT_NPC_FLAG_BATTLEMASTER |
        UNIT_NPC_FLAG_BANKER |
        UNIT_NPC_FLAG_INNKEEPER |
        UNIT_NPC_FLAG_SPIRITHEALER |
        UNIT_NPC_FLAG_SPIRITGUIDE |
        UNIT_NPC_FLAG_TABARDDESIGNER |
        UNIT_NPC_FLAG_AUCTIONEER |
        UNIT_NPC_FLAG_STABLEMASTER |
        UNIT_NPC_FLAG_GUILD_BANKER |
        UNIT_NPC_FLAG_MAILBOX;

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
                LOG_WARN("module", "mod-crossfactioncities: Invalid faction id '{}' in {}", token, key);
            }
        }
    }

    bool IsCityAreaByFlags(AreaTableEntry const* area)
    {
        if (!area)
            return false;

        uint32 flags = area->flags;
        return (flags & (AREA_FLAG_CAPITAL | AREA_FLAG_CITY | AREA_FLAG_TOWN | AREA_FLAG_SLAVE_CAPITAL | AREA_FLAG_SLAVE_CAPITAL2)) != 0;
    }

    bool IsCityAreaForPlayer(Player const* player)
    {
        if (!player)
            return false;

        if (sAllAreasNeutral)
            return true;

        if (sNeutralZoneIds.find(player->GetZoneId()) != sNeutralZoneIds.end())
            return true;
        if (sNeutralAreaIds.find(player->GetAreaId()) != sNeutralAreaIds.end())
            return true;

        AreaTableEntry const* area = sAreaTableStore.LookupEntry(player->GetAreaId());
        AreaTableEntry const* zone = sAreaTableStore.LookupEntry(player->GetZoneId());
        return IsCityAreaByFlags(area) || IsCityAreaByFlags(zone);
    }

    enum class FactionSide : uint8
    {
        Unknown,
        Alliance,
        Horde
    };

    FactionSide GetFactionSide(FactionTemplateEntry const* factionTemplate)
    {
        if (!factionTemplate)
            return FactionSide::Unknown;

        bool ourAlliance = (factionTemplate->ourMask & FACTION_MASK_ALLIANCE) != 0;
        bool ourHorde = (factionTemplate->ourMask & FACTION_MASK_HORDE) != 0;
        // Use only the faction-group bitmask to classify city-side alignment.
        // Inferring side from friendly/hostile masks is too broad and catches many combat factions.
        if (ourAlliance == ourHorde)
            return FactionSide::Unknown;

        return ourAlliance ? FactionSide::Alliance : FactionSide::Horde;
    }

    bool IsAllianceFaction(FactionTemplateEntry const* factionTemplate)
    {
        return GetFactionSide(factionTemplate) == FactionSide::Alliance;
    }

    bool IsHordeFaction(FactionTemplateEntry const* factionTemplate)
    {
        return GetFactionSide(factionTemplate) == FactionSide::Horde;
    }

    bool IsAssistPlayersFaction(FactionTemplateEntry const* factionTemplate)
    {
        return factionTemplate && (factionTemplate->factionFlags & FACTION_TEMPLATE_FLAG_ASSIST_PLAYERS);
    }

    bool HasNeutralInteractionNpcFlags(uint32 npcFlags)
    {
        return (npcFlags & NEUTRAL_INTERACTION_NPC_FLAGS) != 0;
    }

    bool IsHostileToBothPlayerFactions(FactionTemplateEntry const* factionTemplate)
    {
        if (!factionTemplate)
            return false;

        return factionTemplate->IsHostileToAlliancePlayers() && factionTemplate->IsHostileToHordePlayers();
    }

    bool IsNeutralizableRole(Creature const* creature, FactionTemplateEntry const* factionTemplate, bool isGuard, bool isMailboxNpc)
    {
        if (!creature)
            return false;

        if (isMailboxNpc && sNeutralMailboxes)
            return true;

        if (isGuard)
            return true;

        if (creature->IsServiceProvider() || creature->IsQuestGiver() || creature->IsGossip() || creature->HasNpcFlag(UNIT_NPC_FLAG_STABLEMASTER))
            return true;

        return IsAssistPlayersFaction(factionTemplate);
    }

    std::string FormatFactionIdListForLog(std::unordered_set<uint32> const& ids, size_t maxIds = 40)
    {
        if (ids.empty())
            return "none";

        std::vector<uint32> sorted(ids.begin(), ids.end());
        std::sort(sorted.begin(), sorted.end());

        std::ostringstream oss;
        size_t count = std::min(maxIds, sorted.size());
        for (size_t i = 0; i < count; ++i)
        {
            if (i)
                oss << ',';
            oss << sorted[i];
        }

        if (sorted.size() > count)
            oss << ",...+" << (sorted.size() - count);

        return oss.str();
    }

    void RebuildFactionRoleAudit()
    {
        sMixedUseFactionTemplateIds.clear();
        sCombatOnlyFactionTemplateIds.clear();

        CreatureTemplateContainer const* creatureTemplates = sObjectMgr->GetCreatureTemplates();
        if (!creatureTemplates)
            return;

        struct RoleUsage
        {
            uint32 total = 0;
            uint32 neutralizable = 0;
            uint32 combat = 0;
        };

        std::unordered_map<uint32, RoleUsage> usageByFaction;

        for (auto const& pair : *creatureTemplates)
        {
            CreatureTemplate const& creatureTemplate = pair.second;
            if (!creatureTemplate.faction)
                continue;

            FactionTemplateEntry const* factionTemplate = sFactionTemplateStore.LookupEntry(creatureTemplate.faction);
            bool hasNeutralRole = HasNeutralInteractionNpcFlags(creatureTemplate.npcflag) ||
                                  creatureTemplate.HasFlagsExtra(CREATURE_FLAG_EXTRA_GUARD) ||
                                  IsAssistPlayersFaction(factionTemplate);

            RoleUsage& usage = usageByFaction[creatureTemplate.faction];
            ++usage.total;
            if (hasNeutralRole)
                ++usage.neutralizable;
            else
                ++usage.combat;
        }

        for (auto const& pair : usageByFaction)
        {
            uint32 factionTemplateId = pair.first;
            RoleUsage const& usage = pair.second;

            if (usage.neutralizable > 0 && usage.combat > 0)
                sMixedUseFactionTemplateIds.insert(factionTemplateId);
            else if (usage.neutralizable == 0 && usage.combat > 0)
                sCombatOnlyFactionTemplateIds.insert(factionTemplateId);
        }

        if (!sFactionRoleAuditLog)
            return;

        LOG_INFO("module", "mod-crossfactioncities: faction role audit complete: factions={}, mixedUse={}, combatOnly={}",
            usageByFaction.size(), sMixedUseFactionTemplateIds.size(), sCombatOnlyFactionTemplateIds.size());

        if (!sMixedUseFactionTemplateIds.empty())
            LOG_WARN("module", "mod-crossfactioncities: mixed-use faction template IDs (service+combat): {}",
                FormatFactionIdListForLog(sMixedUseFactionTemplateIds));

        if (!sCombatOnlyFactionTemplateIds.empty())
            LOG_INFO("module", "mod-crossfactioncities: combat-only faction template IDs: {}",
                FormatFactionIdListForLog(sCombatOnlyFactionTemplateIds));
    }

    bool ShouldForceNeutral(Player const* player, Creature const* creature)
    {
        if (!sEnabled || !player || !creature)
            return false;

        if (player->InBattleground() || player->InArena())
            return false;

        bool isMailboxNpc = creature->HasNpcFlag(UNIT_NPC_FLAG_MAILBOX);

        if (sOnlyInCities && !IsCityAreaForPlayer(player) && !(sNeutralMailboxes && sMailboxIgnoreCityCheck && isMailboxNpc))
            return false;

        uint32 factionId = creature->GetFaction();
        uint32 creatureEntry = creature->GetEntry();
        FactionTemplateEntry const* factionTemplate = creature->GetFactionTemplateEntry();

        if (sNeverNeutralFactionTemplateIds.find(factionId) != sNeverNeutralFactionTemplateIds.end())
            return false;

        if (sNeverNeutralCreatureEntryIds.find(creatureEntry) != sNeverNeutralCreatureEntryIds.end())
            return false;

        bool isAllianceCityExplicit = sAllianceCityFactionIds.find(factionId) != sAllianceCityFactionIds.end();
        bool isHordeCityExplicit = sHordeCityFactionIds.find(factionId) != sHordeCityFactionIds.end();
        bool isAllianceCity = isAllianceCityExplicit;
        bool isHordeCity = isHordeCityExplicit;
        bool isGuard = sGuardFactionIds.find(factionId) != sGuardFactionIds.end();

        if (sUseFactionGroup && factionTemplate)
        {
            if (IsAllianceFaction(factionTemplate))
                isAllianceCity = true;
            if (IsHordeFaction(factionTemplate))
                isHordeCity = true;
        }

        if (!isGuard && sIncludeGuards && creature->IsGuard())
            isGuard = true;

        bool isNeutralizableRole = IsNeutralizableRole(creature, factionTemplate, isGuard, isMailboxNpc);
        bool isDerivedFactionGroupOnly = (isAllianceCity || isHordeCity) && !isAllianceCityExplicit && !isHordeCityExplicit;
        if (isDerivedFactionGroupOnly)
        {
            if (!sNeutralizeNonServiceNpcs && sCombatOnlyFactionTemplateIds.find(factionId) != sCombatOnlyFactionTemplateIds.end())
                return false;

            if (sKeepHostileToBothFactions && IsHostileToBothPlayerFactions(factionTemplate))
                return false;

            if (!sNeutralizeNonServiceNpcs && !isNeutralizableRole)
                return false;
        }

        if (!isAllianceCity && !isHordeCity && !isGuard)
        {
            if (sNeutralMailboxes && isMailboxNpc)
                return true;
            return false;
        }

        TeamId playerTeam = player->GetTeamId(true);
        if (playerTeam == TEAM_ALLIANCE)
        {
            if (isHordeCity)
                return true;
            if (isGuard && IsHordeFaction(factionTemplate))
                return true;
        }
        else if (playerTeam == TEAM_HORDE)
        {
            if (isAllianceCity)
                return true;
            if (isGuard && IsAllianceFaction(factionTemplate))
                return true;
        }

        return false;
    }

    bool ShouldForceNeutralPlayers(Player const* viewer, Player const* other)
    {
        if (!sEnabled || !sPlayerNeutral || !viewer || !other)
            return false;

        if (viewer->InBattleground() || viewer->InArena())
            return false;

        if (other->InBattleground() || other->InArena())
            return false;

        if (viewer->duel && viewer->duel->Opponent == other && viewer->duel->State == DUEL_STATE_IN_PROGRESS)
            return false;

        if (sPlayerNeutralOnlyInCities && !IsCityAreaForPlayer(viewer))
            return false;

        TeamId viewerTeam = viewer->GetTeamId(true);
        TeamId otherTeam = other->GetTeamId(true);
        if (viewerTeam == TEAM_NEUTRAL || otherTeam == TEAM_NEUTRAL)
            return false;

        return viewerTeam != otherTeam;
    }

    bool ShouldNeutralizeMailbox(Player const* player, GameObject const* go)
    {
        if (!sEnabled || !sNeutralMailboxes || !player || !go)
            return false;

        if (player->InBattleground() || player->InArena())
            return false;

        if (go->GetGoType() != GAMEOBJECT_TYPE_MAILBOX)
            return false;

        if (sOnlyInCities && !IsCityAreaForPlayer(player) && !sMailboxIgnoreCityCheck)
            return false;

        return true;
    }

    bool ShouldNeutralizeGameObject(Player const* player, GameObject const* go, uint32 faction)
    {
        if (!sEnabled || !sNeutralGameObjects || !player || !go)
            return false;

        if (player->InBattleground() || player->InArena())
            return false;

        if (sNeutralGameObjectsOnlyInCities && !IsCityAreaForPlayer(player))
            return false;

        // Skip objects that are already neutral / no faction template.
        if (!faction)
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
        sIncludeGuards = sConfigMgr->GetOption<bool>("CrossFactionCities.IncludeGuards", true);
        sUseFactionGroup = sConfigMgr->GetOption<bool>("CrossFactionCities.UseFactionGroup", true);
        sNeutralizeNonServiceNpcs = sConfigMgr->GetOption<bool>("CrossFactionCities.NeutralizeNonServiceNpcs", false);
        sKeepHostileToBothFactions = sConfigMgr->GetOption<bool>("CrossFactionCities.KeepHostileToBothFactions", true);
        sFactionRoleAuditLog = sConfigMgr->GetOption<bool>("CrossFactionCities.FactionRoleAuditLog", true);
        sClientFactionTemplateId = sConfigMgr->GetOption<uint32>("CrossFactionCities.ClientFactionTemplateId", 0);
        sPlayerNeutral = sConfigMgr->GetOption<bool>("CrossFactionCities.PlayerNeutral", true);
        sPlayerNeutralOnlyInCities = sConfigMgr->GetOption<bool>("CrossFactionCities.PlayerNeutralOnlyInCities", false);
        sNeutralMailboxes = sConfigMgr->GetOption<bool>("CrossFactionCities.NeutralMailboxes", true);
        sMailboxIgnoreCityCheck = sConfigMgr->GetOption<bool>("CrossFactionCities.MailboxIgnoreCityCheck", true);
        sNeutralGameObjects = sConfigMgr->GetOption<bool>("CrossFactionCities.NeutralGameObjects", false);
        sNeutralGameObjectsOnlyInCities = sConfigMgr->GetOption<bool>("CrossFactionCities.NeutralGameObjectsOnlyInCities", false);
        sAllowHostileQuestReputation = sConfigMgr->GetOption<bool>("CrossFactionCities.AllowHostileQuestReputation", false);
        LoadIdSetFromConfig("CrossFactionCities.AllianceCityFactionIds", sAllianceCityFactionIds);
        LoadIdSetFromConfig("CrossFactionCities.HordeCityFactionIds", sHordeCityFactionIds);
        LoadIdSetFromConfig("CrossFactionCities.GuardFactionIds", sGuardFactionIds);
        LoadIdSetFromConfig("CrossFactionCities.NeutralZoneIds", sNeutralZoneIds);
        LoadIdSetFromConfig("CrossFactionCities.NeutralAreaIds", sNeutralAreaIds);
        LoadIdSetFromConfig("CrossFactionCities.NeverNeutralFactionTemplateIds", sNeverNeutralFactionTemplateIds);
        LoadIdSetFromConfig("CrossFactionCities.NeverNeutralCreatureEntryIds", sNeverNeutralCreatureEntryIds);

        LOG_INFO("module", "mod-crossfactioncities: Enable={}, OnlyInCities={}, AllAreasNeutral={}, IncludeGuards={}, UseFactionGroup={}, NeutralizeNonServiceNpcs={}, KeepHostileToBothFactions={}, FactionRoleAuditLog={}, ClientFactionTemplateId={}, PlayerNeutral={}, PlayerNeutralOnlyInCities={}, NeutralMailboxes={}, MailboxIgnoreCityCheck={}, NeutralGameObjects={}, NeutralGameObjectsOnlyInCities={}, AllowHostileQuestReputation={}, AllianceCityFactionIds={}, HordeCityFactionIds={}, GuardFactionIds={}, NeutralZoneIds={}, NeutralAreaIds={}, NeverNeutralFactionTemplateIds={}, NeverNeutralCreatureEntryIds={}",
            sEnabled ? 1 : 0,
            sOnlyInCities ? 1 : 0,
            sAllAreasNeutral ? 1 : 0,
            sIncludeGuards ? 1 : 0,
            sUseFactionGroup ? 1 : 0,
            sNeutralizeNonServiceNpcs ? 1 : 0,
            sKeepHostileToBothFactions ? 1 : 0,
            sFactionRoleAuditLog ? 1 : 0,
            sClientFactionTemplateId,
            sPlayerNeutral ? 1 : 0,
            sPlayerNeutralOnlyInCities ? 1 : 0,
            sNeutralMailboxes ? 1 : 0,
            sMailboxIgnoreCityCheck ? 1 : 0,
            sNeutralGameObjects ? 1 : 0,
            sNeutralGameObjectsOnlyInCities ? 1 : 0,
            sAllowHostileQuestReputation ? 1 : 0,
            sAllianceCityFactionIds.size(),
            sHordeCityFactionIds.size(),
            sGuardFactionIds.size(),
            sNeutralZoneIds.size(),
            sNeutralAreaIds.size(),
            sNeverNeutralFactionTemplateIds.size(),
            sNeverNeutralCreatureEntryIds.size());

        if (reload)
            RebuildFactionRoleAudit();
    }

    void OnStartup() override
    {
        RebuildFactionRoleAudit();
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
        Creature const* creatureA = a ? a->ToCreature() : nullptr;
        Creature const* creatureB = b ? b->ToCreature() : nullptr;

        if (playerA && playerB && ShouldForceNeutralPlayers(playerA, playerB))
        {
            rank = REP_NEUTRAL;
            return false;
        }

        if (playerA && creatureB && ShouldForceNeutral(playerA, creatureB))
        {
            rank = REP_NEUTRAL;
            return false;
        }

        if (playerB && creatureA && ShouldForceNeutral(playerB, creatureA))
        {
            rank = REP_NEUTRAL;
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

        uint32 clientFactionTemplateId = sClientFactionTemplateId ? sClientFactionTemplateId : target->GetFaction();

        if (!clientFactionTemplateId)
            return;

        if (Creature const* creature = unit->ToCreature())
        {
            if (!ShouldForceNeutral(target, creature))
                return;

            valuesUpdateBuf.put(posPointers.UnitFieldFactionTemplatePos, uint32(clientFactionTemplateId));
            return;
        }

        if (Player const* otherPlayer = unit->ToPlayer())
        {
            if (!ShouldForceNeutralPlayers(target, otherPlayer))
                return;

            valuesUpdateBuf.put(posPointers.UnitFieldFactionTemplatePos, uint32(clientFactionTemplateId));
        }
    }
};

class CrossFactionCities_PlayerScript final : public PlayerScript
{
public:
    CrossFactionCities_PlayerScript() : PlayerScript("CrossFactionCities_PlayerScript") { }

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
