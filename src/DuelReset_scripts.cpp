/*
 *  Originally written  for TrinityCore by ShinDarth and GigaDev90 (www.trinitycore.org)
 *  Converted as module for AzerothCore by ShinDarth and Yehonal   (www.azerothcore.org)
 *  Reworked by Gozzim
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Affero General Public License as published
 *  by the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Affero General Public License for more details.
 *
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "ScriptMgr.h"
#include "Player.h"
#include "Pet.h"
#include "SpellInfo.h"
#include "DuelReset.h"

class DuelResetAfterConfigLoad : public WorldScript
{
public:
    DuelResetAfterConfigLoad() : WorldScript("DuelResetAfterConfigLoad", {
        WORLDHOOK_ON_AFTER_CONFIG_LOAD,
        WORLDHOOK_ON_STARTUP
    }) { }

    void OnAfterConfigLoad(bool reload) override
    {
        sDuelReset->LoadConfig(reload);
    }

    void OnStartup() override
    {
        sDuelReset->LoadConfig(false);
    }
};

class DuelResetScript : public PlayerScript {
public:
    DuelResetScript() : PlayerScript("DuelResetScript", {
        PLAYERHOOK_ON_DUEL_START,
        PLAYERHOOK_ON_DUEL_END
    }) {}

    //uint32 PhasedDueling::getNormalPhase(Player* player) const
    // add phase duel? https://github.com/azerothcore/mod-phased-duels/blob/master/src/mod_phased_duels.cpp

    // Called when a duel starts (after 3s countdown)
    void OnPlayerDuelStart(Player *player1, Player *player2) override {
        // Check if Reset is allowed in area or zone
        if (!sDuelReset->IsAllowedInArea(player1)) {
            return;
        }

        // Cooldowns reset
        if (sDuelReset->GetResetCooldownsEnabled())
        {
            sDuelReset->SaveCooldownStateBeforeDuel(player1);
            sDuelReset->SaveCooldownStateBeforeDuel(player2);

            sDuelReset->ResetSpellCooldowns(player1, true);
            sDuelReset->ResetSpellCooldowns(player2, true);
        }

        // Health and mana reset
        if (sDuelReset->GetResetHealthEnabled())
        {
            sDuelReset->SaveHealthBeforeDuel(player1);
            if (player1->getPowerType() == POWER_MANA || player1->getClass() == CLASS_DRUID)
            {
                sDuelReset->SaveManaBeforeDuel(player1);
            }
            player1->ResetAllPowers();

            player1->ClearDiminishings(); // Clear Diminishing Returns
            if (Pet* pet = player1->GetPet())
            {
                if (pet && pet->IsInWorld())
                {
                    if (pet->IsAlive())
                    {
                        pet->ClearDiminishings();
                    }
                }
            }


            sDuelReset->SaveHealthBeforeDuel(player2);
            if (player2->getPowerType() == POWER_MANA || player2->getClass() == CLASS_DRUID)
            {
                sDuelReset->SaveManaBeforeDuel(player2);
            }
            player2->ResetAllPowers();

            player2->ClearDiminishings(); // Clear Diminishing Returns
            if (Pet* pet = player2->GetPet())
            {
                if (pet && pet->IsInWorld())
                {
                    if (pet->IsAlive())
                    {
                        pet->ClearDiminishings();
                    }
                }
            }
        }
    }

    // Called when a duel ends
    void OnPlayerDuelEnd(Player *winner, Player *loser, DuelCompleteType type) override
    {
        // Checking zone here is not necessary and would open options or abuse
        // do not reset anything if DUEL_INTERRUPTED or DUEL_FLED
        if (type == DUEL_WON) {
            // Cooldown restore
            if (sDuelReset->GetResetCooldownsEnabled())
            {
                sDuelReset->RestoreCooldownStateAfterDuel(winner);
                sDuelReset->RestoreCooldownStateAfterDuel(loser);
            }

            // Health and mana restore
            if (sDuelReset->GetResetHealthEnabled())
            {
                // check if player1 class uses mana
                if (winner->getPowerType() == POWER_MANA || winner->getClass() == CLASS_DRUID)
                    sDuelReset->RestoreManaAfterDuel(winner);

                // check if player2 class uses mana
                if (loser->getPowerType() == POWER_MANA || loser->getClass() == CLASS_DRUID)
                    sDuelReset->RestoreManaAfterDuel(loser);

                sDuelReset->RestoreHealthAfterDuel(winner);
                sDuelReset->RestoreHealthAfterDuel(loser);
            }
        }

        // Revive pets
        Pet* pet1 = winner->GetPet();
        Pet* pet2 = loser->GetPet();

        if (!pet1 || !pet2)
            return;

        if (!pet1->IsAlive() || !pet2->IsAlive())
        {
            if (winner->getClass() == CLASS_HUNTER || loser->getClass() == CLASS_HUNTER)
            {
                pet1->SetPower(POWER_HAPPINESS, pet1->GetMaxPower(POWER_HAPPINESS));
                pet2->SetPower(POWER_HAPPINESS, pet2->GetMaxPower(POWER_HAPPINESS));
            }
            pet1->setDeathState(DeathState::Alive);
            pet2->setDeathState(DeathState::Alive);
        }

        pet1->SetHealth(pet1->GetMaxHealth());
        pet2->SetHealth(pet2->GetMaxHealth());
    }
};

void AddSC_DuelReset()
{
    new DuelResetAfterConfigLoad();
    new DuelResetScript();
}
