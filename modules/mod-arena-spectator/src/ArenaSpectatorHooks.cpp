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

#include "ArenaSpectator.h"
#include "Pet.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "Unit.h"
#include "UnitScript.h"

class ArenaSpectatorUnitScript : public UnitScript
{
public:
    ArenaSpectatorUnitScript() : UnitScript("ArenaSpectatorUnitScript", true, {
        UNITHOOK_ON_HEALTH_CHANGE,
        UNITHOOK_ON_MAX_HEALTH_CHANGE,
        UNITHOOK_ON_POWER_CHANGE,
        UNITHOOK_ON_MAX_POWER_CHANGE,
        UNITHOOK_ON_POWER_TYPE_CHANGE
    }) { }

    void OnHealthChange(Unit* unit, uint32 /*oldHealth*/,
        uint32 newHealth) override
    {
        // Handle player health updates
        if (Player* player = unit->ToPlayer())
        {
            if (player->NeedSendSpectatorData())
                ArenaSpectator::SendCommand_UInt32Value(
                    player->FindMap(), player->GetGUID(), "CHP",
                    newHealth);
        }
        // Handle pet health updates
        else if (Creature* creature = unit->ToCreature())
        {
            if (Pet* pet = creature->ToPet())
            {
                if (pet->isControlled() &&
                    pet->GetCreatureTemplate()->family)
                {
                    if (Unit* owner = pet->GetOwner())
                    {
                        if (Player* ownerPlayer = owner->ToPlayer())
                        {
                            if (ownerPlayer->NeedSendSpectatorData())
                                ArenaSpectator::SendCommand_UInt32Value(
                                    ownerPlayer->FindMap(),
                                    ownerPlayer->GetGUID(), "PHP",
                                    (uint32)pet->GetHealthPct());
                        }
                    }
                }
            }
        }
    }

    void OnMaxHealthChange(Unit* unit, uint32 /*oldMaxHealth*/,
        uint32 newMaxHealth) override
    {
        // Handle player max health updates
        if (Player* player = unit->ToPlayer())
        {
            if (player->NeedSendSpectatorData())
                ArenaSpectator::SendCommand_UInt32Value(
                    player->FindMap(), player->GetGUID(), "MHP",
                    newMaxHealth);
        }
        // Handle pet max health updates (affects health percentage)
        else if (Creature* creature = unit->ToCreature())
        {
            if (Pet* pet = creature->ToPet())
            {
                if (pet->isControlled() &&
                    pet->GetCreatureTemplate()->family)
                {
                    if (Unit* owner = pet->GetOwner())
                    {
                        if (Player* ownerPlayer = owner->ToPlayer())
                        {
                            if (ownerPlayer->NeedSendSpectatorData())
                                ArenaSpectator::SendCommand_UInt32Value(
                                    ownerPlayer->FindMap(),
                                    ownerPlayer->GetGUID(), "PHP",
                                    (uint32)pet->GetHealthPct());
                        }
                    }
                }
            }
        }
    }

    void OnPowerChange(Unit* unit, Powers power, uint32 /*oldPower*/,
        uint32 newPower) override
    {
        if (Player* player = unit->ToPlayer())
        {
            if (player->getPowerType() == power &&
                player->NeedSendSpectatorData())
            {
                uint32 powerValue = (power == POWER_RAGE ||
                    power == POWER_RUNIC_POWER) ? newPower / 10 : newPower;
                ArenaSpectator::SendCommand_UInt32Value(
                    player->FindMap(), player->GetGUID(), "CPW",
                    powerValue);
            }
        }
    }

    void OnMaxPowerChange(Unit* unit, Powers power,
        uint32 /*oldMaxPower*/, uint32 newMaxPower) override
    {
        if (Player* player = unit->ToPlayer())
        {
            if (player->getPowerType() == power &&
                player->NeedSendSpectatorData())
            {
                uint32 powerValue = (power == POWER_RAGE ||
                    power == POWER_RUNIC_POWER) ?
                    newMaxPower / 10 : newMaxPower;
                ArenaSpectator::SendCommand_UInt32Value(
                    player->FindMap(), player->GetGUID(), "MPW",
                    powerValue);
            }
        }
    }

    void OnPowerTypeChange(Unit* unit, Powers /*oldPowerType*/,
        Powers newPowerType) override
    {
        if (Player* player = unit->ToPlayer())
        {
            if (player->NeedSendSpectatorData())
            {
                ArenaSpectator::SendCommand_UInt32Value(
                    player->FindMap(), player->GetGUID(), "PWT",
                    newPowerType);

                uint32 maxPowerValue = (newPowerType == POWER_RAGE ||
                    newPowerType == POWER_RUNIC_POWER) ?
                    player->GetMaxPower(newPowerType) / 10 :
                    player->GetMaxPower(newPowerType);
                ArenaSpectator::SendCommand_UInt32Value(
                    player->FindMap(), player->GetGUID(), "MPW",
                    maxPowerValue);

                uint32 powerValue = (newPowerType == POWER_RAGE ||
                    newPowerType == POWER_RUNIC_POWER) ?
                    player->GetPower(newPowerType) / 10 :
                    player->GetPower(newPowerType);
                ArenaSpectator::SendCommand_UInt32Value(
                    player->FindMap(), player->GetGUID(), "CPW",
                    powerValue);
            }
        }
    }
};

void AddSC_ArenaSpectatorHooks()
{
    new ArenaSpectatorUnitScript();
}
