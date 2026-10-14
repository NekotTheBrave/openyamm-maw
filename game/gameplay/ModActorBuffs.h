#pragma once

#include "game/gameplay/GameplayRuntimeInterfaces.h"

#include <algorithm>
#include <cmath>

namespace OpenYAMM::Game
{
// A monster's spell effects in the original's numbering (const.MonsterBuff), over the engine's effect fields, which
// both world runtimes keep under the same names (outdoors on the actor, indoors in its spell effect state). Effects
// the engine has no field for (Summoned, Stoned, Mass Distortion, Mistform) are not there. The original's
// MeleeOnly and DamageHalved are both parts of Dark Grasp here.
template <typename Effects>
bool readModActorBuff(const Effects &effects, int buffId, GameplayModActorBuff &buff)
{
    using Mode = decltype(effects.controlMode);
    const auto control = [&effects, &buff](int mode)
    {
        buff.remainingSeconds =
            effects.controlMode == static_cast<Mode>(mode) ? effects.controlRemainingSeconds : 0.0f;
        return true;
    };
    const auto timed = [&buff](float seconds, int power)
    {
        buff.remainingSeconds = seconds;
        buff.power = power;
        return true;
    };

    switch (buffId)
    {
        case 1: return control(1);
        case 3:
            return timed(
                effects.shrinkRemainingSeconds,
                effects.shrinkDamageMultiplier > 0.0f
                    ? static_cast<int>(std::lround(1.0f / effects.shrinkDamageMultiplier)) : 0);
        case 4: return timed(effects.fearRemainingSeconds, 0);
        case 6: return timed(effects.paralyzeRemainingSeconds, 0);
        case 7:
            return timed(effects.slowRemainingSeconds, static_cast<int>(std::lround(effects.slowRecoveryMultiplier)));
        case 8: return control(2);
        case 10: return timed(effects.fateRemainingSeconds, effects.fatePower);
        case 11: return control(3);
        case 12: return timed(effects.dayOfProtectionRemainingSeconds, effects.dayOfProtectionPower);
        case 13: return timed(effects.hourOfPowerRemainingSeconds, effects.hourOfPowerPower);
        case 14: return timed(effects.shieldRemainingSeconds, 0);
        case 15: return timed(effects.stoneskinRemainingSeconds, effects.stoneskinPower);
        case 16: return timed(effects.blessRemainingSeconds, effects.blessPower);
        case 17: return timed(effects.heroismRemainingSeconds, effects.heroismPower);
        case 18: return timed(effects.hasteRemainingSeconds, 0);
        case 19: return timed(effects.painReflectionRemainingSeconds, 0);
        case 20: return timed(effects.hammerhandsRemainingSeconds, effects.hammerhandsPower);
        case 21: return timed(effects.armorClassHalvedRemainingSeconds, 0);
        case 22:
        case 23: return timed(effects.darkGraspRemainingSeconds, 0);
        case 24: return timed(effects.blindRemainingSeconds, 0);
        default: return false;
    }
}

// Sets what `buff` has: the seconds left when `setSeconds`, the power when `setPower`.
template <typename Effects>
bool writeModActorBuff(Effects &effects, int buffId, const GameplayModActorBuff &buff, bool setSeconds, bool setPower)
{
    using Mode = decltype(effects.controlMode);
    const float seconds = std::max(0.0f, buff.remainingSeconds);
    const auto control = [&](int mode)
    {
        if (!setSeconds)
        {
            return true;
        }

        if (seconds > 0.0f)
        {
            effects.controlMode = static_cast<Mode>(mode);
            effects.controlRemainingSeconds = seconds;
        }
        else if (effects.controlMode == static_cast<Mode>(mode))
        {
            effects.controlMode = static_cast<Mode>(0);
            effects.controlRemainingSeconds = 0.0f;
        }

        return true;
    };
    const auto timed = [&](float &remaining, int *pPower)
    {
        if (setSeconds)
        {
            remaining = seconds;
        }

        if (setPower && pPower != nullptr)
        {
            *pPower = buff.power;
        }

        return true;
    };

    switch (buffId)
    {
        case 1: return control(1);
        case 3:
            if (setPower && buff.power > 0)
            {
                effects.shrinkDamageMultiplier = 1.0f / static_cast<float>(buff.power);
                effects.shrinkArmorClassMultiplier = 1.0f / static_cast<float>(buff.power);
            }

            return timed(effects.shrinkRemainingSeconds, nullptr);
        case 4: return timed(effects.fearRemainingSeconds, nullptr);
        case 6: return timed(effects.paralyzeRemainingSeconds, nullptr);
        case 7:
            if (setPower && buff.power > 0)
            {
                effects.slowMoveMultiplier = 1.0f / static_cast<float>(buff.power);
                effects.slowRecoveryMultiplier = static_cast<float>(buff.power);
            }

            return timed(effects.slowRemainingSeconds, nullptr);
        case 8: return control(2);
        case 10: return timed(effects.fateRemainingSeconds, &effects.fatePower);
        case 11: return control(3);
        case 12: return timed(effects.dayOfProtectionRemainingSeconds, &effects.dayOfProtectionPower);
        case 13: return timed(effects.hourOfPowerRemainingSeconds, &effects.hourOfPowerPower);
        case 14: return timed(effects.shieldRemainingSeconds, nullptr);
        case 15: return timed(effects.stoneskinRemainingSeconds, &effects.stoneskinPower);
        case 16: return timed(effects.blessRemainingSeconds, &effects.blessPower);
        case 17: return timed(effects.heroismRemainingSeconds, &effects.heroismPower);
        case 18: return timed(effects.hasteRemainingSeconds, nullptr);
        case 19: return timed(effects.painReflectionRemainingSeconds, nullptr);
        case 20: return timed(effects.hammerhandsRemainingSeconds, &effects.hammerhandsPower);
        case 21: return timed(effects.armorClassHalvedRemainingSeconds, nullptr);
        case 22:
        case 23: return timed(effects.darkGraspRemainingSeconds, nullptr);
        case 24: return timed(effects.blindRemainingSeconds, nullptr);
        default: return false;
    }
}
}
