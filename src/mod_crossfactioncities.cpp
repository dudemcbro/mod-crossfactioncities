#include "ScriptMgr.h"
#include "AllGameObjectScript.h"
#include "Config.h"
#include "Creature.h"
#include "DBCStores.h"
#include "GameObject.h"
#include "Log.h"
#include "Player.h"
#include "Unit.h"

#include <exception>
#include <sstream>
#include <string>
#include <unordered_set>

namespace
{
    bool sEnabled = true;
    bool sOnlyInCities = true;
    bool sIncludeGuards = true;
    bool sUseFactionGroup = true;
    uint32 sClientFactionTemplateId = 0;
    bool sPlayerNeutral = true;
    bool sPlayerNeutralOnlyInCities = false;
    bool sNeutralMailboxes = true;
    bool sMailboxIgnoreCityCheck = true;
    std::unordered_set<uint32> sAllianceCityFactionIds;
    std::unordered_set<uint32> sHordeCityFactionIds;
    std::unordered_set<uint32> sGuardFactionIds;
    std::unordered_set<uint32> sNeutralZoneIds;
    std::unordered_set<uint32> sNeutralAreaIds;

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

        if (sNeutralZoneIds.find(player->GetZoneId()) != sNeutralZoneIds.end())
            return true;
        if (sNeutralAreaIds.find(player->GetAreaId()) != sNeutralAreaIds.end())
            return true;

        AreaTableEntry const* area = sAreaTableStore.LookupEntry(player->GetAreaId());
        AreaTableEntry const* zone = sAreaTableStore.LookupEntry(player->GetZoneId());
        return IsCityAreaByFlags(area) || IsCityAreaByFlags(zone);
    }

    bool IsAllianceFaction(FactionTemplateEntry const* factionTemplate)
    {
        return factionTemplate && (factionTemplate->ourMask & FACTION_MASK_ALLIANCE) != 0;
    }

    bool IsHordeFaction(FactionTemplateEntry const* factionTemplate)
    {
        return factionTemplate && (factionTemplate->ourMask & FACTION_MASK_HORDE) != 0;
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
        FactionTemplateEntry const* factionTemplate = creature->GetFactionTemplateEntry();
        bool isAllianceCity = sAllianceCityFactionIds.find(factionId) != sAllianceCityFactionIds.end();
        bool isHordeCity = sHordeCityFactionIds.find(factionId) != sHordeCityFactionIds.end();
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
}

class CrossFactionCities_WorldScript final : public WorldScript
{
public:
    CrossFactionCities_WorldScript() : WorldScript("CrossFactionCities_WorldScript") { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        sEnabled = sConfigMgr->GetOption<bool>("CrossFactionCities.Enable", true);
        sOnlyInCities = sConfigMgr->GetOption<bool>("CrossFactionCities.OnlyInCities", true);
        sIncludeGuards = sConfigMgr->GetOption<bool>("CrossFactionCities.IncludeGuards", true);
        sUseFactionGroup = sConfigMgr->GetOption<bool>("CrossFactionCities.UseFactionGroup", true);
        sClientFactionTemplateId = sConfigMgr->GetOption<uint32>("CrossFactionCities.ClientFactionTemplateId", 0);
        sPlayerNeutral = sConfigMgr->GetOption<bool>("CrossFactionCities.PlayerNeutral", true);
        sPlayerNeutralOnlyInCities = sConfigMgr->GetOption<bool>("CrossFactionCities.PlayerNeutralOnlyInCities", false);
        sNeutralMailboxes = sConfigMgr->GetOption<bool>("CrossFactionCities.NeutralMailboxes", true);
        sMailboxIgnoreCityCheck = sConfigMgr->GetOption<bool>("CrossFactionCities.MailboxIgnoreCityCheck", true);
        LoadIdSetFromConfig("CrossFactionCities.AllianceCityFactionIds", sAllianceCityFactionIds);
        LoadIdSetFromConfig("CrossFactionCities.HordeCityFactionIds", sHordeCityFactionIds);
        LoadIdSetFromConfig("CrossFactionCities.GuardFactionIds", sGuardFactionIds);
        LoadIdSetFromConfig("CrossFactionCities.NeutralZoneIds", sNeutralZoneIds);
        LoadIdSetFromConfig("CrossFactionCities.NeutralAreaIds", sNeutralAreaIds);

        LOG_INFO("module", "mod-crossfactioncities: Enable={}, OnlyInCities={}, IncludeGuards={}, UseFactionGroup={}, ClientFactionTemplateId={}, PlayerNeutral={}, PlayerNeutralOnlyInCities={}, NeutralMailboxes={}, MailboxIgnoreCityCheck={}, AllianceCityFactionIds={}, HordeCityFactionIds={}, GuardFactionIds={}, NeutralZoneIds={}, NeutralAreaIds={}",
            sEnabled ? 1 : 0,
            sOnlyInCities ? 1 : 0,
            sIncludeGuards ? 1 : 0,
            sUseFactionGroup ? 1 : 0,
            sClientFactionTemplateId,
            sPlayerNeutral ? 1 : 0,
            sPlayerNeutralOnlyInCities ? 1 : 0,
            sNeutralMailboxes ? 1 : 0,
            sMailboxIgnoreCityCheck ? 1 : 0,
            sAllianceCityFactionIds.size(),
            sHordeCityFactionIds.size(),
            sGuardFactionIds.size(),
            sNeutralZoneIds.size(),
            sNeutralAreaIds.size());
    }
};

class CrossFactionCities_UnitScript final : public UnitScript
{
public:
    CrossFactionCities_UnitScript() : UnitScript("CrossFactionCities_UnitScript") { }

    bool IfNormalReaction(Unit const* a, Unit const* b, ReputationRank& rank) override
    {
        Player const* player = a ? a->ToPlayer() : nullptr;
        Creature const* creature = b ? b->ToCreature() : nullptr;
        Player const* otherPlayer = b ? b->ToPlayer() : nullptr;

        if (!player || !creature)
        {
            player = b ? b->ToPlayer() : nullptr;
            creature = a ? a->ToCreature() : nullptr;
        }

        if (player && otherPlayer && ShouldForceNeutralPlayers(player, otherPlayer))
        {
            rank = REP_NEUTRAL;
            return false;
        }

        if (!ShouldForceNeutral(player, creature))
            return true;

        rank = REP_NEUTRAL;
        return false;
    }

    void OnPatchValuesUpdate(Unit const* unit, ByteBuffer& valuesUpdateBuf, BuildValuesCachePosPointers& posPointers, Player* target) override
    {
        if (!unit || !target)
            return;

        if (posPointers.UnitFieldFactionTemplatePos < 0)
            return;

        uint32 clientFactionTemplateId = sClientFactionTemplateId ? sClientFactionTemplateId : target->GetFaction();

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

class CrossFactionCities_AllGameObjectScript final : public AllGameObjectScript
{
public:
    CrossFactionCities_AllGameObjectScript() : AllGameObjectScript("CrossFactionCities_AllGameObjectScript") { }

    void OnGameObjectFactionCalc(GameObject const* go, Player const* viewer, uint32& faction) 
    {
        if (!ShouldNeutralizeMailbox(viewer, go))
            return;

        faction = sClientFactionTemplateId ? sClientFactionTemplateId : viewer->GetFaction();
    }
};

void Addmod_crossfactioncitiesScripts()
{
    new CrossFactionCities_WorldScript();
    new CrossFactionCities_UnitScript();
    new CrossFactionCities_AllGameObjectScript();
}

void AddCrossFactionCitiesScripts()
{
    Addmod_crossfactioncitiesScripts();
}
