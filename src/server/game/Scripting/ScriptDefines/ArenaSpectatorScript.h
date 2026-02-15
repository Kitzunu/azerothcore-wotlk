/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SCRIPT_OBJECT_ARENA_SPECTATOR_SCRIPT_H_
#define SCRIPT_OBJECT_ARENA_SPECTATOR_SCRIPT_H_

#include "ScriptObject.h"
#include <vector>

class Aura;
class AuraApplication;
class Battleground;
class Map;
class Player;
class Spell;
class Unit;
enum AuraRemoveMode : uint8;
enum Powers : int8;

enum ArenaSpectatorHook
{
    ARENASPECTATORHOOK_ON_UPDATE_STATS,
    ARENASPECTATORHOOK_END
};

class ArenaSpectatorScript : public ScriptObject
{
protected:
    ArenaSpectatorScript(const char* name, std::vector<uint16> enabledHooks = std::vector<uint16>());

public:
    [[nodiscard]] bool IsDatabaseBound() const override { return false; }

    // Called when a player's unit values that spectators care about change
    virtual void OnUpdateStats(Unit* unit, uint8 updateType) { }
};

#endif
