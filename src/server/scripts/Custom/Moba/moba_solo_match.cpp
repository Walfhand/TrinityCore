/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#include "moba_shared.h"

#include "Battleground.h"
#include "BattlegroundMgr.h"
#include "DBCStores.h"
#include "Log.h"
#include "Player.h"
#include "WorldSession.h"

namespace Moba
{
void QueueSoloNexusTest(Player* player)
{
    if (player->InBattleground())
    {
        player->GetSession()->SendNotification("Tu es deja en match.");
        return;
    }

    Battleground* bgTemplate = sBattlegroundMgr->GetBattlegroundTemplate(BATTLEGROUND_NA);
    if (!bgTemplate)
    {
        TC_LOG_ERROR("scripts", "MOBA solo: Nagrand Arena template introuvable");
        player->GetSession()->SendNotification("Erreur prototype : template arene introuvable.");
        return;
    }

    PvPDifficultyEntry const* bracketEntry = GetBattlegroundBracketByLevel(bgTemplate->GetMapId(), player->GetLevel());
    if (!bracketEntry)
    {
        TC_LOG_ERROR("scripts", "MOBA solo: no PvP bracket for player {} level {} map {}", player->GetName(), player->GetLevel(), bgTemplate->GetMapId());
        player->GetSession()->SendNotification("Erreur prototype : bracket PvP introuvable.");
        return;
    }

    BattlegroundQueueTypeId const oldPrototypeQueueTypeId = BattlegroundMgr::BGQueueTypeId(BATTLEGROUND_NA, bracketEntry->GetBracketId(), ARENA_TYPE_2v2);
    BattlegroundQueueTypeId const bgQueueTypeId = BattlegroundMgr::BGQueueTypeId(BATTLEGROUND_AA, bracketEntry->GetBracketId(), ARENA_TYPE_2v2);

    // Previous prototype versions used BATTLEGROUND_NA directly, while Arena leave cleanup uses BATTLEGROUND_AA.
    player->RemoveBattlegroundQueueId(oldPrototypeQueueTypeId);
    player->RemoveBattlegroundQueueId(bgQueueTypeId);

    if (player->InBattlegroundQueue())
    {
        player->GetSession()->SendNotification("Tu es deja en file.");
        return;
    }

    ResetForMatch(player);

    Battleground* bg = sBattlegroundMgr->CreateNewBattleground(BATTLEGROUND_NA, bracketEntry, ARENA_TYPE_2v2, false);
    if (!bg)
    {
        TC_LOG_ERROR("scripts", "MOBA solo: CreateNewBattleground failed for {}", player->GetName());
        player->GetSession()->SendNotification("Erreur prototype : creation arene refusee.");
        return;
    }

    bg->StartBattleground();

    uint32 const queueSlot = player->AddBattlegroundQueueId(bgQueueTypeId);
    if (queueSlot >= PLAYER_MAX_BATTLEGROUND_QUEUES)
    {
        TC_LOG_ERROR("scripts", "MOBA solo: no free queue slot for {}", player->GetName());
        player->GetSession()->SendNotification("Erreur prototype : aucune file disponible.");
        return;
    }

    player->SetInviteForBattlegroundQueueType(bgQueueTypeId, bg->GetInstanceID());
    bg->IncreaseInvitedCount(ALLIANCE);

    if (!player->InBattleground())
        player->SetBattlegroundEntryPoint();

    player->SetBattlegroundId(bg->GetInstanceID(), BATTLEGROUND_NA);
    // Prototype mapping: ALLIANCE/HORDE are only technical BG team ids.
    // MOBA teams are Blue/Red and must stay independent from character faction.
    player->SetBGTeam(ALLIANCE);

    TC_LOG_INFO("scripts", "MOBA solo: player {} entering Nagrand Arena BG instance {}", player->GetName(), bg->GetInstanceID());
    player->GetSession()->SendNotification("Tag solo accepte. Entree dans l'arene Nexus.");
    sBattlegroundMgr->SendToBattleground(player, bg->GetInstanceID(), BATTLEGROUND_NA);
}

bool CompleteSoloNexusObjective(Player* /*player*/)
{
    return false;
}
}

void AddSC_moba_solo_match()
{
}
