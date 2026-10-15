#include "doctest/doctest.h"

#include "game/gameplay/GameMechanics.h"
#include "game/gameplay/MeleeContactEffects.h"

#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

using namespace OpenYAMM::Game;

namespace
{
std::array<float, 3> rotate(const std::array<float, 4> &rotation, const std::array<float, 3> &value)
{
    const std::array<float, 3> axis = {rotation[0], rotation[1], rotation[2]};
    const std::array<float, 3> twiceCross = {
        2.0f * (axis[1] * value[2] - axis[2] * value[1]),
        2.0f * (axis[2] * value[0] - axis[0] * value[2]),
        2.0f * (axis[0] * value[1] - axis[1] * value[0]),
    };
    return {
        value[0] + rotation[3] * twiceCross[0] + axis[1] * twiceCross[2] - axis[2] * twiceCross[1],
        value[1] + rotation[3] * twiceCross[1] + axis[2] * twiceCross[0] - axis[0] * twiceCross[2],
        value[2] + rotation[3] * twiceCross[2] + axis[0] * twiceCross[1] - axis[1] * twiceCross[0],
    };
}

MeleeContactEffectInput frontalBlow(size_t memberIndex, size_t memberCount)
{
    // The party looks along +Y: screen right is +X and screen up is +Z.
    MeleeContactEffectInput input;
    input.contact = {0.0f, 500.0f, 100.0f};
    input.viewer = {0.0f, 0.0f, 100.0f};
    input.memberIndex = memberIndex;
    input.memberCount = memberCount;
    return input;
}
}

TEST_CASE("melee contact effects pick the effect by weapon skill and blow strength")
{
    CHECK(meleeWeaponClassForSkill("Sword") == MeleeWeaponClass::Slash);
    CHECK(meleeWeaponClassForSkill("Axe") == MeleeWeaponClass::Slash);
    CHECK(meleeWeaponClassForSkill("Spear") == MeleeWeaponClass::Thrust);
    CHECK(meleeWeaponClassForSkill("Dagger") == MeleeWeaponClass::Thrust);
    CHECK(meleeWeaponClassForSkill("Mace") == MeleeWeaponClass::Crush);
    CHECK(meleeWeaponClassForSkill("Staff") == MeleeWeaponClass::Crush);
    CHECK(meleeWeaponClassForSkill("Unarmed") == MeleeWeaponClass::Crush);

    MeleeContactEffectInput input = frontalBlow(0, 1);
    CHECK(buildMeleeContactEffect(input)->effectId == "openyamm:fx/melee/slash");
    input.weaponClass = MeleeWeaponClass::Thrust;
    input.strength = MeleeHitStrength::Glancing;
    CHECK(buildMeleeContactEffect(input)->effectId == "openyamm:fx/melee/thrust_light");
    input.weaponClass = MeleeWeaponClass::Crush;
    input.strength = MeleeHitStrength::Crushing;
    CHECK(buildMeleeContactEffect(input)->effectId == "openyamm:fx/melee/crush_heavy");
}

TEST_CASE("melee contact effects swing from the attacker's side and face the party")
{
    const std::optional<MeleeContactEffectPlacement> left = buildMeleeContactEffect(frontalBlow(0, 5));
    const std::optional<MeleeContactEffectPlacement> right = buildMeleeContactEffect(frontalBlow(4, 5));
    REQUIRE(left);
    REQUIRE(right);

    // The leftmost member cuts towards the right and down, its arc bulging up; the rightmost mirrors it.
    const std::array<float, 3> leftBlow = rotate(left->rotation, {1.0f, 0.0f, 0.0f});
    const std::array<float, 3> leftOuter = rotate(left->rotation, {0.0f, 1.0f, 0.0f});
    CHECK(leftBlow[0] > 0.8f);
    CHECK(leftBlow[2] < 0.0f);
    CHECK(leftBlow[1] == doctest::Approx(0.0f).epsilon(0.001));
    CHECK(leftOuter[2] > 0.8f);
    const std::array<float, 3> rightBlow = rotate(right->rotation, {1.0f, 0.0f, 0.0f});
    const std::array<float, 3> rightOuter = rotate(right->rotation, {0.0f, 1.0f, 0.0f});
    CHECK(rightBlow[0] < -0.8f);
    CHECK(rightBlow[2] < 0.0f);
    CHECK(rightOuter[2] > 0.8f);

    // Given the camera's axes, the effect uses them: a camera rolled by 90 degrees turns the blow with it.
    MeleeContactEffectInput rolled = frontalBlow(0, 5);
    rolled.screenRight = {0.0f, 0.0f, -1.0f};
    rolled.screenUp = {1.0f, 0.0f, 0.0f};
    const std::array<float, 3> rolledBlow = rotate(buildMeleeContactEffect(rolled)->rotation, {1.0f, 0.0f, 0.0f});
    CHECK(rolledBlow[2] < -0.8f);

    // The middle member cuts nearly straight down.
    const std::optional<MeleeContactEffectPlacement> middle = buildMeleeContactEffect(frontalBlow(2, 5));
    REQUIRE(middle);
    CHECK(rotate(middle->rotation, {1.0f, 0.0f, 0.0f})[2] < -0.9f);

    // The plane faces the party, and the effect moves towards the eye, shrunk to cover the same part of the screen.
    CHECK(std::abs(rotate(left->rotation, {0.0f, 0.0f, 1.0f})[1]) == doctest::Approx(1.0f));
    CHECK(left->position[1] == doctest::Approx(225.0f));
    CHECK(left->position[2] == doctest::Approx(100.0f));
    CHECK(left->scale == doctest::Approx(0.45f));
}

TEST_CASE("melee contact effects scale with the target and the weapon")
{
    // At 500 units the effect is drawn at 45% of the distance, so its scale is 45% of the target-sized one.
    MeleeContactEffectInput input = frontalBlow(0, 1);
    input.targetHeight = 320.0f;
    CHECK(buildMeleeContactEffect(input)->scale == doctest::Approx(2.0f * 0.45f));
    input.targetHeight = 20.0f;
    CHECK(buildMeleeContactEffect(input)->scale == doctest::Approx(0.6f * 0.45f));
    input.targetHeight = 160.0f;
    input.twoHanded = true;
    CHECK(buildMeleeContactEffect(input)->scale == doctest::Approx(1.15f * 0.45f));

    // Close up it keeps a minimum distance from the eye.
    input.contact = {0.0f, 100.0f, 100.0f};
    CHECK(buildMeleeContactEffect(input)->position[1] == doctest::Approx(60.0f));

    input.viewer = input.contact;
    CHECK_FALSE(buildMeleeContactEffect(input));
}

TEST_CASE("melee contact effects add the weapon's strongest element and draw misses faintly")
{
    using Elements = std::vector<MeleeWeaponElement>;
    CHECK(meleeWeaponElementsFor(ElementalDamageBonuses{}).empty());
    CHECK(meleeWeaponElementsFor(ElementalDamageBonuses{.body = 5}) == Elements{MeleeWeaponElement::Body});
    // A dual wielder with a fire sword and a poison sword shows both, the stronger first; at most three.
    CHECK(meleeWeaponElementsFor(ElementalDamageBonuses{.fire = 6, .body = 8})
        == Elements{MeleeWeaponElement::Body, MeleeWeaponElement::Fire});
    CHECK(meleeWeaponElementsFor(ElementalDamageBonuses{.fire = 1, .air = 2, .water = 3, .dark = 4}).size() == 3);

    MeleeContactEffectInput input = frontalBlow(0, 5);
    CHECK(buildMeleeContactEffect(input)->accentEffectIds.empty());
    input.elements = {MeleeWeaponElement::Fire, MeleeWeaponElement::Body};
    CHECK(buildMeleeContactEffect(input)->accentEffectIds
        == std::vector<std::string>{"openyamm:fx/melee/slash_element_fire", "openyamm:fx/melee/slash_element_body"});
    input.weaponClass = MeleeWeaponClass::Crush;
    input.elements = {MeleeWeaponElement::Water};
    CHECK(buildMeleeContactEffect(input)->accentEffectIds
        == std::vector<std::string>{"openyamm:fx/melee/crush_element_water"});

    // A miss has no strength variant and no accent.
    input.missed = true;
    input.strength = MeleeHitStrength::Crushing;
    const std::optional<MeleeContactEffectPlacement> miss = buildMeleeContactEffect(input);
    REQUIRE(miss);
    CHECK(miss->effectId == "openyamm:fx/melee/crush_miss");
    CHECK(miss->accentEffectIds.empty());

    // The blow's screen direction: the leftmost member cuts to the right and down.
    CHECK(miss->blowScreenDirection[0] > 0.8f);
    CHECK(miss->blowScreenDirection[1] < 0.0f);
    CHECK(meleeContactParticleScale(320.0f) == doctest::Approx(1.8f));
    CHECK(meleeContactEffectVariant("openyamm:fx/melee/contact/blood", MeleeHitStrength::Glancing)
        == "openyamm:fx/melee/contact/blood_light");
    CHECK(meleeContactEffectVariant("openyamm:fx/melee/contact/blood", MeleeHitStrength::Normal)
        == "openyamm:fx/melee/contact/blood");
    CHECK(meleeContactEffectVariant("openyamm:fx/melee/contact/blood", MeleeHitStrength::Crushing)
        == "openyamm:fx/melee/contact/blood_heavy");
    CHECK(meleeContactParticleScale(160.0f) == doctest::Approx(1.0f));
}
