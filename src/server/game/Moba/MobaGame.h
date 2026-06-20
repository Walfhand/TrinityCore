/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#ifndef GAME_MOBA_GAME_H
#define GAME_MOBA_GAME_H

#include "Define.h"
#include "EventMap.h"
#include "MobaLane.h"
#include "MobaRules.h"
#include "Position.h"

#include <vector>

class Creature;
class Map;
class Player;

namespace Moba
{
// Map-agnostic description of a MOBA arena. A host map only has to provide these
// positions; everything else (nexus spawning, waves, win detection) is generic.
struct ArenaLayout
{
    Position BlueNexus;
    Position RedNexus;
    std::vector<std::vector<Position>> Lanes;   // each lane is a path in Blue->Red order
};

// Drives the MOBA match rules independently of any specific map or Battleground.
// The host forwards Update()/OnUnitKilled() and decides how to actually end the
// match from the returned winner, so this stays decoupled from the BG system.
class MatchController
{
public:
    void Start(Map* map, ArenaLayout const& layout);
    void Update(uint32 diff);
    bool IsStarted() const { return _started; }

    // Returns the winning team id when a nexus is destroyed, otherwise InvalidTeamId.
    uint32 OnUnitKilled(Creature* creature, Player* killer);

private:
    void SpawnNexuses();
    void SpawnWave();

    Map* _map = nullptr;
    ArenaLayout _layout;
    std::vector<LaneConfig> _lanes;
    EventMap _events;
    uint32 _waveNumber = 0;
    uint32 _elapsedMs = 0;
    bool _started = false;
};
}

#endif
