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

#include "DuelReset.h"
#include "GameTime.h"
#include "Pet.h"
#include "SpellMgr.h"

DuelReset* DuelReset::instance()
{
    static DuelReset instance;
    return &instance;
}

void DuelReset::ResetSpellCooldowns(Player* player, bool onStartDuel)
{
    uint32 infTime = GameTime::GetGameTimeMS().count() + infinityCooldownDelayCheck;
    SpellCooldowns::iterator itr, next;

    for (itr = player->GetSpellCooldownMap().begin(); itr != player->GetSpellCooldownMap().end(); itr = next)
    {
        next = itr;
        ++next;
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(itr->first);
        if (!spellInfo)
            continue;

        // Get correct spell cooldown times
        uint32 remainingCooldown = player->GetSpellCooldownDelay(spellInfo->Id);
        int32 totalCooldown = spellInfo->RecoveryTime;
        int32 categoryCooldown = spellInfo->CategoryRecoveryTime;
        player->ApplySpellMod(spellInfo->Id, SPELLMOD_COOLDOWN, totalCooldown, nullptr);
        if (int32 cooldownMod = player->GetTotalAuraModifier(SPELL_AURA_MOD_COOLDOWN))
            totalCooldown += cooldownMod * IN_MILLISECONDS;

        if (!spellInfo->HasAttribute(SPELL_ATTR6_NO_CATEGORY_COOLDOWN_MODS))
            player->ApplySpellMod(spellInfo->Id, SPELLMOD_COOLDOWN, categoryCooldown, nullptr);

        // Clear cooldown if < 10min & (passed time > 30sec or onDuelEnd)
        if (remainingCooldown > 0
            && itr->second.end < infTime
            && totalCooldown < 10 * MINUTE * IN_MILLISECONDS
            && categoryCooldown < 10 * MINUTE * IN_MILLISECONDS
            && remainingCooldown < 10 * MINUTE * IN_MILLISECONDS
            && (onStartDuel ? (totalCooldown - remainingCooldown) > m_cooldownAge * IN_MILLISECONDS : true)
            && (onStartDuel ? (categoryCooldown - remainingCooldown) > m_cooldownAge * IN_MILLISECONDS : true)
            )
            player->RemoveSpellCooldown(itr->first, true);

        // Clear pet CDs
        if (Pet* pet = player->GetPet())
        {
            if (pet && pet->IsInWorld())
            {
                uint32 infTime = GameTime::GetGameTimeMS().count() + infinityCooldownDelayCheck;
                if (!pet->m_CreatureSpellCooldowns.empty())
                {
                    for (auto itr = pet->m_CreatureSpellCooldowns.begin(); itr != pet->m_CreatureSpellCooldowns.end(); ++itr)
                    {
                        if (itr->second.end < infTime)
                            player->SendClearCooldown(itr->first, pet);
                    }
                    pet->m_CreatureSpellCooldowns.clear();
                }
            }
        }

    }

    if (Pet* pet = player->GetPet())
    {
        for (CreatureSpellCooldowns::const_iterator itr2 = pet->m_CreatureSpellCooldowns.begin(); itr2 != pet->m_CreatureSpellCooldowns.end(); ++itr2)
            player->SendClearCooldown(itr2->first, pet);

        // actually clear cooldowns
        pet->m_CreatureSpellCooldowns.clear();
    }
}

void DuelReset::SaveCooldownStateBeforeDuel(Player* player)
{

    if(!player)
        return;

    m_spellCooldownsBeforeDuel[player] = player->GetSpellCooldownMap();
}

void DuelReset::RestoreCooldownStateAfterDuel(Player* player)
{
    PlayersCooldownMap::iterator savedDuelCooldownsMap = m_spellCooldownsBeforeDuel.find(player);
    if (savedDuelCooldownsMap == m_spellCooldownsBeforeDuel.end())
        return;

    sDuelReset->ResetSpellCooldowns(player, false);

    SpellCooldowns playerSavedDuelCooldowns = savedDuelCooldownsMap->second;
    uint32 curMSTime = GameTime::GetGameTimeMS().count();
    uint32 infTime = curMSTime + infinityCooldownDelayCheck;

    // add all profession CDs created while in duel (if any)
    for (auto itr = player->GetSpellCooldownMap().begin(); itr != player->GetSpellCooldownMap().end(); ++itr)
    {
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(itr->first);

        if (spellInfo && (spellInfo->RecoveryTime > 10 * MINUTE * IN_MILLISECONDS || spellInfo->CategoryRecoveryTime > 10 * MINUTE * IN_MILLISECONDS))
        {
            playerSavedDuelCooldowns[itr->first] = player->GetSpellCooldownMap()[itr->first];
        }
    }

    // check for spell with infinity delay active before and during the duel
    for (auto itr = playerSavedDuelCooldowns.begin(); itr != playerSavedDuelCooldowns.end(); ++itr)
    {
        if (itr->second.end < infTime && player->GetSpellCooldownMap()[itr->first].end < infTime)
            player->AddSpellCooldown(itr->first, itr->second.itemid, (itr->second.end > curMSTime ? itr->second.end - curMSTime : 0), itr->second.needSendToClient, false);
    }

    // update the client: restore old cooldowns
    PacketCooldowns cooldowns;

    for (auto itr = player->GetSpellCooldownMap().begin(); itr != player->GetSpellCooldownMap().end(); ++itr)
    {
        uint32 cooldown = itr->second.end > curMSTime ? itr->second.end - curMSTime : 0;

        // cooldownDuration must be between 0 and 10 minutes in order to avoid any visual bugs
        if (cooldown <= 0 || cooldown > 10 * MINUTE * IN_MILLISECONDS || itr->second.end >= infTime)
            continue;

        cooldowns[itr->first] = cooldown;
    }

    m_spellCooldownsBeforeDuel.erase(player);

    WorldPacket data;
    player->BuildCooldownPacket(data, SPELL_COOLDOWN_FLAG_INCLUDE_EVENT_COOLDOWNS, cooldowns);
    player->SendDirectMessage(&data);
}

void DuelReset::SaveHealthBeforeDuel(Player* player)
{
    if(!player)
        return;

    m_healthBeforeDuel[player] = player->GetHealth();

    // Auras para remover de players após o duel
    std::vector<uint32> auraIds =
    {
        11196, // Recently Bandaged
        66233, // Ardent Defender
        25771, // Forbearance - pala
        61987, // Avenging Wrath Marker (server side forbearance) - pala
        61988, // Server Side Forbearance (Divine Shield Exclude Aura) 
        6788,  // Weakened Soul (Priest)
        41425, // Hipothermia (Mage)
        79500, // Cheated Death (Custom)
        79503, // Reincarnation (Custom)
        79501, // Forbearance Avenging Wrath (Custom)
        79502, // Nature's Guardian (Rshaman - Custom)
        // 57723, // Sated (Bloodlust)
        // 57724, // Exhaustion (heroism)
        // 2825 = Bloodlust, 32182 = Heroism

        // Trinkets:
        71491, 71559, 83098, // Aim of the Iron Dwarves
        71485, 71556, 83096, // Agility of the Vrykul
        71484, 71561, 83095, // Strength of the Taunka
        71492, 71560, 83099, // Speed of the Vrykul
        75456, 75458, 83115, // Sharpened Twilight Scale
        75466, 75473, 83116, // Charred Twilight Scale
        71605, 71636, 83117, // Phylactery
        71601, 71644, 83118, // Dislodged Foreign Object
        71401, 71541, 83114, // Whispering Fanged Skull
        67703, 67708, 67772, 67773, 83112, 83113, // DV/DC

        // Auras Pets:
        47865, 22959, 55360, 47867, // Elements / Imp Scorch / Living Bomb / Curse of Doom
        12579, 42842, 42917, 42931, 33395, 31589, 12494, 55080, // Winter's Chill / Fbolt / Nova / Cone of Cold / PetNova / Arcane Mage Slow / Frostbite / Barrier Nova
        12826, 10326, 14327, 17928, 6215, 10890, // Polymorph / Turn Evil / Scare Beast / Howl / Fear / Psychic Scream
        14309, 60210, 53338, 16857, 770, 53308, 53313 // Trap, Trap2, Hunter's Mark, FFF, FF, Root, natures grasp Root
    };

    // Remova auras de trinkets de players após o duel
    for (uint32 id : auraIds)
        player->RemoveAurasDueToSpell(id);

    // restore pets health + remove debuffs
    if (Pet* pet = player->GetPet())
    {
        if (pet && pet->IsInWorld())
        {
            if (pet->IsAlive())
            {
                pet->SetHealth(pet->GetMaxHealth());

                for (uint32 id : auraIds)
                {
                    // remove pet debuffs
                    pet->RemoveAurasDueToSpell(id);
                }
            }
        }
    }
}

void DuelReset::RestoreHealthAfterDuel(Player* player)
{
    PlayersHealthMap::iterator savedPlayerHealth = m_healthBeforeDuel.find(player);
    if (savedPlayerHealth == m_healthBeforeDuel.end())
        return;

    player->SetHealth(savedPlayerHealth->second);
    m_healthBeforeDuel.erase(player);

    if (!player)
        return;

    switch (player->getPowerType())
    {
        case POWER_MANA:
            player->SetPower(POWER_MANA, player->GetMaxPower(POWER_MANA));
            if (player->getClass() == CLASS_DRUID)
                player->SetPower(POWER_MANA, player->GetMaxPower(POWER_MANA));
            break;
        case POWER_ENERGY:
            player->SetPower(POWER_ENERGY, player->GetMaxPower(POWER_ENERGY));
            if (player->getClass() == CLASS_DRUID)
                player->SetPower(POWER_MANA, player->GetMaxPower(POWER_MANA));
            break;
        case POWER_RUNIC_POWER:
            player->SetPower(POWER_RUNIC_POWER, 0);
            break;
        //case POWER_RAGE:
        //    player->SetPower(POWER_RAGE, 0);
        //    break;
        default:
            break;
    }
}

void DuelReset::SaveManaBeforeDuel(Player* player)
{
    if(!player)
        return;

    m_manaBeforeDuel[player] = player->GetPower(POWER_MANA);
}

void DuelReset::RestoreManaAfterDuel(Player* player)
{
    PlayersManaMap::iterator savedPlayerMana = m_manaBeforeDuel.find(player);
    if (savedPlayerMana == m_manaBeforeDuel.end())
        return;

    player->SetPower(POWER_MANA, savedPlayerMana->second);
    m_manaBeforeDuel.erase(player);
}

void DuelReset::LoadConfig(bool /*reload*/)
{
    m_enableCooldowns = sConfigMgr->GetOption<bool>("DuelReset.Cooldowns", true);
    m_enableHealth = sConfigMgr->GetOption<bool>("DuelReset.HealthMana", true);
    m_cooldownAge = sConfigMgr->GetOption<uint32>("DuelReset.CooldownAge", 30);

    FillWhitelist(sConfigMgr->GetOption<std::string>("DuelReset.Zones", "0"), m_zoneWhitelist);
    FillWhitelist(sConfigMgr->GetOption<std::string>("DuelReset.Areas", "12;14;809"), m_areaWhitelist);
}

void DuelReset::FillWhitelist(std::string zonesAreas, std::vector<uint32> &whitelist)
{
    whitelist.clear();

    if (zonesAreas.empty())
        return;

    std::string zone;
    std::istringstream zoneStream(zonesAreas);
    while (std::getline(zoneStream, zone, ';'))
    {
        whitelist.push_back(stoi(zone));
    }
}

bool DuelReset::IsAllowedInArea(Player* player) const
{
    return (std::find(m_zoneWhitelist.begin(), m_zoneWhitelist.end(), player->GetZoneId()) != m_zoneWhitelist.end())
        || (std::find(m_areaWhitelist.begin(), m_areaWhitelist.end(), player->GetAreaId()) != m_areaWhitelist.end())
        || m_zoneWhitelist.empty();
}

bool DuelReset::GetResetCooldownsEnabled() const
{
    return m_enableCooldowns;
}

bool DuelReset::GetResetHealthEnabled() const
{
    return m_enableHealth;
}

uint32 DuelReset::GetCooldownAge() const
{
    return m_cooldownAge;
}

std::vector<uint32> DuelReset::GetZoneWhitelist() const
{
    return m_zoneWhitelist;
}

std::vector<uint32> DuelReset::GetAreaWhitelist() const
{
    return m_areaWhitelist;
}
