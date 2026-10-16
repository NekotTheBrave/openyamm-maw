#pragma once

#include "game/events/EventRuntime.h"
#include "game/gameplay/GameMechanics.h"
#include "game/gameplay/GameplayWorldInteraction.h"
#include "game/party/Party.h"
#include "game/party/SkillData.h"
#include "game/tables/MonsterTable.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace OpenYAMM::Game
{
class ActorBehaviorRuntime;
class GameplayActorHitFeedback;
class MergedBolsterMonsterTable;
struct MapArenaDefinition;
struct GameplayMinimapState;
struct GameplayMinimapLineState;
struct GameplayMinimapMarkerState;
struct GameplayInputFrame;
struct PartySpellCastResult;

struct GameplayWorldUiRenderState
{
    bool canRenderHudOverlays = false;
    bool renderGameplayHud = true;
    bool renderActorInspectOverlay = true;
    bool renderDebugFallbacks = false;
};

struct GameplayWorldMovementFrameDiagnostics
{
    static constexpr size_t MaxTimerEventIds = 8;

    bool collected = false;
    uint64_t totalNanoseconds = 0;
    uint64_t controllerNanoseconds = 0;
    uint64_t sceneAdvanceNanoseconds = 0;
    uint64_t actorColliderBuildNanoseconds = 0;
    uint64_t partyUpdateNanoseconds = 0;
    uint64_t movementDriverNanoseconds = 0;
    uint64_t movementInputNanoseconds = 0;
    uint64_t movementCollisionNanoseconds = 0;
    uint64_t movementTraceNanoseconds = 0;
    uint64_t movementContactsNanoseconds = 0;
    uint64_t movementConsequencesNanoseconds = 0;
    uint64_t partyRecoveryNanoseconds = 0;
    uint64_t partyTimedStateNanoseconds = 0;
    uint64_t partySpellStateNanoseconds = 0;
    uint64_t partyEffectsNanoseconds = 0;
    uint64_t timerNanoseconds = 0;
    uint64_t timerEventExecutionNanoseconds = 0;
    uint64_t timerEventApplyNanoseconds = 0;
    uint64_t partyEventApplyNanoseconds = 0;
    uint64_t pressurePlateNanoseconds = 0;
    uint64_t boundaryTransitionNanoseconds = 0;
    uint64_t actorQueueAndContactsNanoseconds = 0;
    uint32_t movementStepCount = 0;
    uint32_t timerEventsFired = 0;
    size_t storedTimerEventIdCount = 0;
    std::array<uint16_t, MaxTimerEventIds> timerEventIds = {};
};

struct GameplayChestItemState
{
    InventoryItem item = {};
    uint32_t itemId = 0;
    uint32_t quantity = 0;
    uint32_t goldAmount = 0;
    uint32_t goldRollCount = 0;
    bool isGold = false;
    uint8_t width = 1;
    uint8_t height = 1;
    uint8_t gridX = 0;
    uint8_t gridY = 0;
};

struct GameplayChestViewState
{
    uint32_t chestId = 0;
    uint16_t chestTypeId = 0;
    uint16_t flags = 0;
    uint8_t gridWidth = 0;
    uint8_t gridHeight = 0;
    std::vector<GameplayChestItemState> items;
    std::vector<GameplayChestItemState> hiddenItems;
};

struct GameplayCorpseViewState
{
    bool fromSummonedMonster = false;
    uint32_t sourceIndex = 0;
    std::string title;
    std::vector<GameplayChestItemState> items;
};

struct GameplayWorldItemInspectState
{
    InventoryItem item = {};
    uint32_t goldAmount = 0;
    bool isGold = false;
};

enum class ActorAiAnimationState : uint8_t
{
    Standing = 0,
    Walking = 1,
    AttackMelee = 2,
    AttackRanged = 3,
    GotHit = 4,
    Dying = 5,
    Dead = 6,
    Bored = 7,
};

// Actor fields the mod runtime can read and write through IGameplayWorldRuntime.
enum class GameplayModActorField : uint8_t
{
    MonsterId,
    CurrentHp,
    MaxHp,
    X,
    Y,
    Z,
    Group,
    // Which placed-monster name the actor goes by; 0 is its kind's own name.
    UniqueNameId,
    Ally,
    // What the actor is doing, in the original engine's numbers (const.AIState; see mm8AiState). A write of
    // Removed, Invisible or Dead puts the actor in that state, any other brings a dead or hidden actor back.
    AiState,
    // The actor's own speed and AI type (GameplayActorAiType's numbers, which are the original's). Writing a speed
    // of 0 gives the actor its kind's speed back.
    MoveSpeed,
    AiType,
    // The item the actor carries and leaves on its body, 0 for none. Read only.
    CarriedItem,
    // The actor's own level and resistances (Resistance0 + n: to const.Damage n, 0 to 10), its kind's until a mod
    // sets them; see ModActorStatOverrides.
    Level,
    Resistance0,
    Resistance10 = Resistance0 + 10,
    // The actor's own special attack (the original's Bonus, by its numbers: 1 curse ... 23 fear, 0 none) and its
    // strength (BonusMul), its kind's until a mod sets them (MAW's Doom gives monsters one on arriving at a map).
    SpecialAttack,
    SpecialAttackLevel,
    // The actor's own skill with its two spells, level and mastery (1 normal ... 4 grandmaster), its kind's until a
    // mod sets them (the original's SpellSkill and Spell2Skill; MAW's bosses cast stronger).
    SpellSkillLevel,
    SpellSkillMastery,
    Spell2SkillLevel,
    Spell2SkillMastery,
};

// The original's number of a monster special attack (Bonus), 0 for none.
inline int mm8MonsterBonusNumber(MonsterSpecialAttackKind kind)
{
    switch (kind)
    {
        case MonsterSpecialAttackKind::Curse: return 1;
        case MonsterSpecialAttackKind::Weak: return 2;
        case MonsterSpecialAttackKind::Sleep: return 3;
        case MonsterSpecialAttackKind::Drunk: return 4;
        case MonsterSpecialAttackKind::Insane: return 5;
        case MonsterSpecialAttackKind::PoisonWeak: return 6;
        case MonsterSpecialAttackKind::PoisonMedium: return 7;
        case MonsterSpecialAttackKind::PoisonSevere: return 8;
        case MonsterSpecialAttackKind::DiseaseWeak: return 9;
        case MonsterSpecialAttackKind::DiseaseMedium: return 10;
        case MonsterSpecialAttackKind::DiseaseSevere: return 11;
        case MonsterSpecialAttackKind::Paralyze: return 12;
        case MonsterSpecialAttackKind::Unconscious: return 13;
        case MonsterSpecialAttackKind::Dead: return 14;
        case MonsterSpecialAttackKind::Petrify: return 15;
        case MonsterSpecialAttackKind::Eradicate: return 16;
        case MonsterSpecialAttackKind::BreakAny: return 17;
        case MonsterSpecialAttackKind::BreakArmor: return 18;
        case MonsterSpecialAttackKind::BreakWeapon: return 19;
        case MonsterSpecialAttackKind::Aging: return 21;
        case MonsterSpecialAttackKind::ManaDrain: return 22;
        case MonsterSpecialAttackKind::Fear: return 23;
        default: return 0;
    }
}

// The special attack of the original's number `bonus`; none for 0 and numbers it does not have.
inline MonsterSpecialAttackKind monsterSpecialAttackFromMm8Bonus(int bonus)
{
    constexpr MonsterSpecialAttackKind kinds[] = {
        MonsterSpecialAttackKind::Curse, MonsterSpecialAttackKind::Weak, MonsterSpecialAttackKind::Sleep,
        MonsterSpecialAttackKind::Drunk, MonsterSpecialAttackKind::Insane, MonsterSpecialAttackKind::PoisonWeak,
        MonsterSpecialAttackKind::PoisonMedium, MonsterSpecialAttackKind::PoisonSevere,
        MonsterSpecialAttackKind::DiseaseWeak, MonsterSpecialAttackKind::DiseaseMedium,
        MonsterSpecialAttackKind::DiseaseSevere, MonsterSpecialAttackKind::Paralyze,
        MonsterSpecialAttackKind::Unconscious, MonsterSpecialAttackKind::Dead, MonsterSpecialAttackKind::Petrify,
        MonsterSpecialAttackKind::Eradicate, MonsterSpecialAttackKind::BreakAny, MonsterSpecialAttackKind::BreakArmor,
        MonsterSpecialAttackKind::BreakWeapon, MonsterSpecialAttackKind::Aging, MonsterSpecialAttackKind::ManaDrain,
        MonsterSpecialAttackKind::Fear};

    for (MonsterSpecialAttackKind kind : kinds)
    {
        if (mm8MonsterBonusNumber(kind) == bonus)
        {
            return kind;
        }
    }

    return MonsterSpecialAttackKind::None;
}

// The const.Damage number of a damage type (0 fire, 1 air, 2 water, 3 earth, 4 physical, 6 spirit, 7 mind, 8 body,
// 9 light, 10 dark); none for those the original's monsters have no resistance to.
inline std::optional<size_t> mm8MonsterResistanceIndex(CombatDamageType damageType)
{
    switch (damageType)
    {
        case CombatDamageType::Fire: return 0;
        case CombatDamageType::Air: return 1;
        case CombatDamageType::Water: return 2;
        case CombatDamageType::Earth: return 3;
        case CombatDamageType::Physical: return 4;
        case CombatDamageType::Spirit: return 6;
        case CombatDamageType::Mind: return 7;
        case CombatDamageType::Body: return 8;
        case CombatDamageType::Light: return 9;
        case CombatDamageType::Dark: return 10;
        default: return std::nullopt;
    }
}

// The damage type of const.Damage number `index`; none for 5 (magic, which the original's monsters do not resist).
inline std::optional<CombatDamageType> mm8MonsterResistanceType(size_t index)
{
    constexpr std::array<std::optional<CombatDamageType>, 11> types = {
        CombatDamageType::Fire, CombatDamageType::Air, CombatDamageType::Water, CombatDamageType::Earth,
        CombatDamageType::Physical, std::nullopt, CombatDamageType::Spirit, CombatDamageType::Mind,
        CombatDamageType::Body, CombatDamageType::Light, CombatDamageType::Dark};
    return index < types.size() ? types[index] : std::nullopt;
}

// One monster's own level and resistances, as a mod's script set them on it in place of its kind's (MAW's bosses,
// named monsters and arena waves; its crowd control, which sets a resistance for a moment to decide whether a spell
// takes hold). The engine's rolls against the monster use them: whether a spell's effect takes hold, and its chance
// to hit.
struct ModActorStatOverrides
{
    std::optional<int> level;
    std::array<std::optional<int>, 11> resistances = {};
    // The special attack by the original's number (Bonus) and its strength (BonusMul).
    std::optional<int> specialAttack;
    std::optional<int> specialAttackLevel;
    // The two spells' skill: level and mastery of the first, then of the second (GameplayModActorField order).
    std::array<std::optional<int>, 4> spellSkills = {};

    int levelOr(int kindLevel) const
    {
        return level.value_or(kindLevel);
    }

    MonsterSpecialAttackKind specialAttackOr(MonsterSpecialAttackKind kindSpecialAttack) const
    {
        return specialAttack ? monsterSpecialAttackFromMm8Bonus(*specialAttack) : kindSpecialAttack;
    }

    int specialAttackLevelOr(int kindSpecialAttackLevel) const
    {
        return specialAttackLevel.value_or(kindSpecialAttackLevel);
    }

    // A spell's skill level or mastery (`field` SpellSkillLevel ... Spell2SkillMastery), the kind's when not set.
    int spellSkillOr(GameplayModActorField field, int kindValue) const
    {
        const size_t index = static_cast<size_t>(field) - static_cast<size_t>(GameplayModActorField::SpellSkillLevel);
        return index < spellSkills.size() && spellSkills[index] ? *spellSkills[index] : kindValue;
    }

    // Reads (`set` false) or sets a spell's skill level or mastery; `kindValue` the kind's. False for another field.
    bool accessSpellSkill(GameplayModActorField field, int64_t &value, bool set, int kindValue)
    {
        if (field < GameplayModActorField::SpellSkillLevel || field > GameplayModActorField::Spell2SkillMastery)
        {
            return false;
        }

        const size_t index = static_cast<size_t>(field) - static_cast<size_t>(GameplayModActorField::SpellSkillLevel);
        const bool mastery = field == GameplayModActorField::SpellSkillMastery
            || field == GameplayModActorField::Spell2SkillMastery;

        if (set)
        {
            spellSkills[index] = static_cast<int>(std::clamp<int64_t>(value, 0, mastery ? 4 : 1000));
        }
        else
        {
            value = spellSkills[index].value_or(kindValue);
        }

        return true;
    }

    // Reads (`set` false) or sets the special attack or its strength; false for another field.
    bool accessSpecialAttack(
        GameplayModActorField field,
        int64_t &value,
        bool set,
        MonsterSpecialAttackKind kindSpecialAttack,
        int kindSpecialAttackLevel)
    {
        if (field != GameplayModActorField::SpecialAttack && field != GameplayModActorField::SpecialAttackLevel)
        {
            return false;
        }

        std::optional<int> &stored = field == GameplayModActorField::SpecialAttack ? specialAttack : specialAttackLevel;

        if (set)
        {
            stored = static_cast<int>(std::clamp<int64_t>(value, 0, INT32_MAX));
        }
        else
        {
            value = field == GameplayModActorField::SpecialAttack
                ? (stored ? *stored : mm8MonsterBonusNumber(kindSpecialAttack))
                : stored.value_or(kindSpecialAttackLevel);
        }

        return true;
    }

    int resistanceOr(CombatDamageType damageType, int kindResistance) const
    {
        const std::optional<size_t> index = mm8MonsterResistanceIndex(damageType);
        return index && resistances[*index] ? *resistances[*index] : kindResistance;
    }

    // Reads (`set` false) or sets the field, a Level or Resistance one; `kindValue` gives the kind's value of a
    // resistance by its damage type. False for another field.
    template <typename KindValue>
    bool access(GameplayModActorField field, int64_t &value, bool set, int kindLevel, KindValue kindValue)
    {
        if (field == GameplayModActorField::Level)
        {
            if (set)
            {
                level = static_cast<int>(std::clamp<int64_t>(value, INT32_MIN, INT32_MAX));
            }
            else
            {
                value = levelOr(kindLevel);
            }

            return true;
        }

        if (field < GameplayModActorField::Resistance0 || field > GameplayModActorField::Resistance10)
        {
            return false;
        }

        const size_t index =
            static_cast<size_t>(field) - static_cast<size_t>(GameplayModActorField::Resistance0);
        const std::optional<CombatDamageType> type = mm8MonsterResistanceType(index);

        if (set)
        {
            resistances[index] = static_cast<int>(std::clamp<int64_t>(value, INT32_MIN, INT32_MAX));
        }
        else
        {
            value = resistances[index] ? *resistances[index] : type ? kindValue(*type) : 0;
        }

        return true;
    }
};

enum class ActorTurnBasedMode : uint8_t
{
    // Real-time play: actors attack whenever their recovery cooldown allows.
    RealTime,
    // Turn-based wait stage: actors move but never attack.
    MoveOnly,
    // Turn-based attack and movement stages: actors hold position and only granted actors take their action.
    Hold,
};

struct GameplayRuntimeActorState
{
    uint32_t actorId = 0;
    float yawRadians = 0.0f;
    ActorAiAnimationState animationState = ActorAiAnimationState::Standing;
    float animationTimeTicks = 0.0f;
    // Presentation observes the same action clock and movement as combat; it never drives either.
    float actionSeconds = 0.0f;
    float velocityX = 0.0f;
    float velocityY = 0.0f;
    bool castingSpell = false;
    bool secondaryAttack = false;
    uint32_t castingSpellId = 0;
    bool attackImpactTriggered = false;
    // Action seconds still to run once the attack applies at its sprite's contact frame; 0 when it applies at the end.
    float attackImpactLeadSeconds = 0.0f;
    float visualScale = 1.0f;
    int16_t monsterId = 0;
    int32_t mm9RudeId = 0;
    uint32_t mm9SourceObjectIndex = 0;
    float preciseX = 0.0f;
    float preciseY = 0.0f;
    float preciseZ = 0.0f;
    uint16_t radius = 0;
    // Collision height: indoors at least 2 x radius + 2, so wide monsters fit the floor/ceiling sweep.
    uint16_t height = 0;
    // The actor's own height (map placement or descriptor), unpadded; presentation (3D model scale) uses this.
    uint16_t bodyHeight = 0;
    bool isDead = false;
    bool isInvisible = false;
    bool hostileToParty = false;
    bool hasDetectedParty = false;
    bool combatTargetingParty = false;
    int currentHp = 0;
    int maxHp = 0;
    // A behaviour script's clip role (actors.yml `script_clips`) and its playback time; empty when none plays.
    std::string scriptedClip;
    float scriptedClipSeconds = 0.0f;
    // Hit flash mixed over the sprite or model (GameplayActorHitFeedback), 0 when none.
    float hitFlash = 0.0f;
};

struct GameplayCombatFeedbackEvent
{
    size_t actorIndex = 0;
    int damage = 0;
    int experience = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float height = 0.0f;
    bool killed = false;
};

// One of a map actor's spell effects as a mod sees it: the seconds it has left and its power.
struct GameplayModActorBuff
{
    float remainingSeconds = 0.0f;
    int power = 0;
};

enum class GameplayActorControlMode : uint8_t
{
    None = 0,
    Charm = 1,
    Berserk = 2,
    Enslaved = 3,
    ControlUndead = 4,
    Reanimated = 5,
};

enum class GameplayActorAiType : uint8_t
{
    Suicide = 0,
    Wimp = 1,
    Normal = 2,
    Aggressive = 3,
};

enum class GameplayActorAttackAbility : uint8_t
{
    Attack1 = 0,
    Attack2 = 1,
    Spell1 = 2,
    Spell2 = 3,
};

struct GameplayActorInspectState
{
    std::string displayName;
    std::string previewTextureName;
    size_t uniqueActorIndex = static_cast<size_t>(-1);
    int16_t monsterId = 0;
    int16_t previewPaletteId = 0;
    int previewYOffset = -40;
    int currentHp = 0;
    int maxHp = 0;
    int armorClass = 0;
    MonsterTable::MonsterStatsEntry::DamageProfile attack1Damage = {};
    MonsterTable::MonsterStatsEntry::DamageProfile attack2Damage = {};
    int attack2Chance = 0;
    bool hasSpell1 = false;
    bool hasSpell2 = false;
    std::string spell1Name;
    std::string spell2Name;
    uint32_t spell1SkillLevel = 0;
    SkillMastery spell1SkillMastery = SkillMastery::None;
    uint32_t spell2SkillLevel = 0;
    SkillMastery spell2SkillMastery = SkillMastery::None;
    bool isDead = false;
    float slowRemainingSeconds = 0.0f;
    float stunRemainingSeconds = 0.0f;
    float paralyzeRemainingSeconds = 0.0f;
    float fearRemainingSeconds = 0.0f;
    float shrinkRemainingSeconds = 0.0f;
    float darkGraspRemainingSeconds = 0.0f;
    float meleeOnlyRemainingSeconds = 0.0f;
    float damageHalvedRemainingSeconds = 0.0f;
    float dayOfProtectionRemainingSeconds = 0.0f;
    float hourOfPowerRemainingSeconds = 0.0f;
    float painReflectionRemainingSeconds = 0.0f;
    float hammerhandsRemainingSeconds = 0.0f;
    float hasteRemainingSeconds = 0.0f;
    float shieldRemainingSeconds = 0.0f;
    float stoneskinRemainingSeconds = 0.0f;
    float blessRemainingSeconds = 0.0f;
    float fateRemainingSeconds = 0.0f;
    float heroismRemainingSeconds = 0.0f;
    std::string pendingSelfBuffName;
    GameplayActorControlMode controlMode = GameplayActorControlMode::None;
};

struct GameplayActorSpellEffectState
{
    float slowRemainingSeconds = 0.0f;
    float slowMoveMultiplier = 1.0f;
    float slowRecoveryMultiplier = 1.0f;
    float stunRemainingSeconds = 0.0f;
    float paralyzeRemainingSeconds = 0.0f;
    float fearRemainingSeconds = 0.0f;
    float blindRemainingSeconds = 0.0f;
    float controlRemainingSeconds = 0.0f;
    GameplayActorControlMode controlMode = GameplayActorControlMode::None;
    float shrinkRemainingSeconds = 0.0f;
    float shrinkDamageMultiplier = 1.0f;
    float shrinkArmorClassMultiplier = 1.0f;
    float armorClassHalvedRemainingSeconds = 0.0f;
    float darkGraspRemainingSeconds = 0.0f;
    // The original's MeleeOnly and DamageHalved monster effects, which only mods set (MAW's Death Knight's grasp:
    // half damage, and at Grandmaster no ranged attacks). Dark Grasp, the engine's spell, does both and more.
    float meleeOnlyRemainingSeconds = 0.0f;
    float damageHalvedRemainingSeconds = 0.0f;
    float dayOfProtectionRemainingSeconds = 0.0f;
    int dayOfProtectionPower = 0;
    float hourOfPowerRemainingSeconds = 0.0f;
    int hourOfPowerPower = 0;
    float painReflectionRemainingSeconds = 0.0f;
    float hammerhandsRemainingSeconds = 0.0f;
    int hammerhandsPower = 0;
    float hasteRemainingSeconds = 0.0f;
    float shieldRemainingSeconds = 0.0f;
    float stoneskinRemainingSeconds = 0.0f;
    int stoneskinPower = 0;
    float blessRemainingSeconds = 0.0f;
    int blessPower = 0;
    float fateRemainingSeconds = 0.0f;
    int fatePower = 0;
    float heroismRemainingSeconds = 0.0f;
    int heroismPower = 0;
    bool hostileToParty = false;
    bool hasDetectedParty = false;
};

struct GameplayActorTargetPolicyState
{
    int16_t monsterId = 0;
    int16_t relationMonsterId = 0;
    uint32_t group = 0;
    float preciseZ = 0.0f;
    uint16_t height = 0;
    bool hostileToParty = false;
    GameplayActorControlMode controlMode = GameplayActorControlMode::None;
};

struct GameplayActorTargetPolicyResult
{
    bool canTarget = false;
    int relationToTarget = 0;
    float engagementRange = 0.0f;
};

struct GameplayActorAttackChoiceResult
{
    GameplayActorAttackAbility ability = GameplayActorAttackAbility::Attack1;
    uint32_t nextDecisionCount = 0;
};

struct GameplayActorAttackConstraintState
{
    bool attack1IsRanged = false;
    bool attack2IsRanged = false;
    bool blindActive = false;
    bool darkGraspActive = false;
    bool rangedCommitAllowed = true;
};

struct GameplayPartySpellProjectileRequest
{
    uint32_t casterMemberIndex = 0;
    uint32_t spellId = 0;
    uint32_t skillLevel = 0;
    SkillMastery skillMastery = SkillMastery::None;
    int damage = 0;
    CombatDamageType damageType = CombatDamageType::Physical;
    float sourceX = 0.0f;
    float sourceY = 0.0f;
    float sourceZ = 0.0f;
    float targetX = 0.0f;
    float targetY = 0.0f;
    float targetZ = 0.0f;
    uint32_t effectSoundIdOverride = 0;
    uint32_t impactSoundIdOverride = 0;
    bool turnBasedPendingAction = false;
    // The monster the projectile is aimed at, which a homing projectile follows.
    std::optional<size_t> targetActorIndex;
};

enum class GameplayProjectileVisualMode
{
    SpriteBillboard,
    FxOnly,
};

struct GameplayProjectilePresentationState
{
    uint32_t projectileId = 0;
    uint16_t objectDescriptionId = 0;
    uint16_t objectSpriteId = 0;
    uint16_t objectSpriteFrameIndex = 0;
    uint16_t objectFlags = 0;
    GameplayProjectileVisualMode visualMode = GameplayProjectileVisualMode::SpriteBillboard;
    uint16_t radius = 0;
    uint16_t height = 0;
    int spellId = 0;
    // The monster that threw it (0: party, event or unknown), for per-monster enhanced visuals.
    int16_t sourceMonsterId = 0;
    std::string objectName;
    std::string objectSpriteName;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float velocityX = 0.0f;
    float velocityY = 0.0f;
    float velocityZ = 0.0f;
    uint32_t timeSinceCreatedTicks = 0;
    int16_t sectorId = -1;
    bool isSettled = false;
};

// A projectile that ended (hit something or ran out of lifetime) and where it stopped, so its flight effect can
// reach that point before it fades.
struct GameplayProjectileEndPresentationState
{
    uint32_t projectileId = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct GameplayProjectileImpactPresentationState
{
    uint32_t effectId = 0;
    uint16_t objectDescriptionId = 0;
    uint16_t objectSpriteId = 0;
    uint16_t objectSpriteFrameIndex = 0;
    uint16_t sourceObjectFlags = 0;
    int sourceSpellId = 0;
    int16_t sourceMonsterId = 0;
    std::string objectName;
    std::string objectSpriteName;
    std::string sourceObjectName;
    std::string sourceObjectSpriteName;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    uint32_t timeSinceCreatedTicks = 0;
    uint32_t lifetimeTicks = 0;
    int16_t sectorId = -1;
    size_t targetActorIndex = static_cast<size_t>(-1);
    bool freezeAnimation = false;
};

struct GameplayHeldItemDropRequest
{
    InventoryItem item = {};
    float sourceX = 0.0f;
    float sourceY = 0.0f;
    float sourceZ = 0.0f;
    float yawRadians = 0.0f;
};

struct GameplayWorldPoint
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct GameplayPartyAttackActorFacts
{
    size_t actorIndex = 0;
    int16_t monsterId = 0;
    std::string displayName;
    GameplayWorldPoint position = {};
    uint16_t radius = 0;
    uint16_t height = 0;
    int currentHp = 0;
    int maxHp = 0;
    int effectiveArmorClass = 0;
    int hourOfPowerPower = 0;
    bool isDead = false;
    bool isInvisible = false;
    bool hostileToParty = false;
    bool visibleForFallback = false;
    bool lineOfSightToParty = true;
};

struct GameplayCombatActorInfo
{
    uint32_t actorId = 0;
    // Position among the map's actors, which is how mods address them.
    size_t actorIndex = 0;
    int16_t monsterId = 0;
    int monsterLevel = 0;
    int maxHp = 0;
    uint32_t attackPreferences = 0;
    int attackBonus = 0;
    bool bolsterAffectsPlayerArmorClass = false;
    MonsterSpecialAttackKind specialAttackKind = MonsterSpecialAttackKind::None;
    int specialAttackLevel = 0;
    std::string displayName;
};

struct GameplayPartyAttackSpellRequest
{
    size_t sourcePartyMemberIndex = 0;
    uint32_t spellId = 0;
    uint32_t skillLevel = 0;
    uint32_t skillMastery = 0;
    int damage = 0;
    int attackBonus = 0;
    bool useActorHitChance = false;
    GameplayWorldPoint source = {};
    GameplayWorldPoint target = {};
};

struct GameplayPartyAttackProjectileRequest
{
    size_t sourcePartyMemberIndex = 0;
    uint32_t objectId = 0;
    uint32_t impactObjectId = 0;
    int damage = 0;
    int attackBonus = 0;
    bool useActorHitChance = false;
    CombatDamageType damageType = CombatDamageType::Physical;
    GameplayWorldPoint source = {};
    GameplayWorldPoint target = {};
    bool turnBasedPendingAction = false;
    // The monster the projectile is aimed at, which a homing projectile follows.
    std::optional<size_t> targetActorIndex;
};

struct GameplayPartyAttackFallbackQuery
{
    float screenX = 0.0f;
    float screenY = 0.0f;
    int viewWidth = 0;
    int viewHeight = 0;
    std::array<float, 16> viewMatrix = {};
    std::array<float, 16> projectionMatrix = {};
};

struct GameplayPartyAttackFrameInput
{
    bool enabled = false;
    GameplayWorldPoint partyPosition = {};
    GameplayWorldPoint rangedSource = {};
    GameplayWorldPoint rangedRight = {};
    GameplayWorldPoint defaultRangedTarget = {};
    GameplayWorldPoint rayRangedTarget = {};
    bool hasRayRangedTarget = false;
    GameplayPartyAttackFallbackQuery fallbackQuery = {};
};

enum class GameplayInteractionMethod
{
    Keyboard,
    Mouse,
};

class IGameplayWorldRuntime
{
public:
    virtual ~IGameplayWorldRuntime() = default;

    virtual const std::string &mapName() const = 0;
    virtual const MonsterTable *monsterTable() const
    {
        return nullptr;
    }
    virtual const MergedBolsterMonsterTable *mergedBolsterMonsterTable() const
    {
        return nullptr;
    }
    virtual GameplayWorldPoint chooseBountyHuntSpawnPoint(uint32_t seed) const
    {
        (void)seed;
        return GameplayWorldPoint{partyX() + 1024.0f, partyY(), partyFootZ()};
    }
    virtual bool isIndoorMap() const = 0;
    virtual bool isUnderwaterMap() const
    {
        return false;
    }
    virtual bool allowsLloydsBeacon() const
    {
        return true;
    }
    virtual bool allowsRest() const
    {
        return true;
    }
    virtual float gameMinutes() const = 0;
    virtual int currentHour() const = 0;
    virtual const std::vector<uint8_t> *journalMapFullyRevealedCells() const = 0;
    virtual const std::vector<uint8_t> *journalMapPartiallyRevealedCells() const = 0;
    virtual int restFoodRequired() const = 0;
    virtual void advanceGameMinutes(float minutes) = 0;
    virtual void applyMapReentryReset()
    {
    }
    virtual int currentLocationReputation() const = 0;
    virtual void setCurrentLocationReputation(int reputation) = 0;
    virtual void applyEventRuntimeState(bool syncPersistentHostilityMasks = false)
    {
        static_cast<void>(syncPersistentHostilityMasks);
    }
    virtual Party *party() = 0;
    virtual const Party *party() const = 0;
    virtual float partyX() const = 0;
    virtual float partyY() const = 0;
    virtual float partyFootZ() const = 0;
    // Puts the party at a point of the map (its feet at footZ), as a script moving it does (MAW's Knight charge, its
    // unstuck key). False when the world cannot.
    virtual bool setPartyPosition(float x, float y, float footZ)
    {
        (void)x;
        (void)y;
        (void)footZ;
        return false;
    }
    // The party's eyes: the camera height over the party's feet in this world.
    virtual float partyEyeZ() const = 0;
    virtual float partyEngagementRange() const
    {
        return 10240.0f;
    }
    virtual float gameplayCameraYawRadians() const
    {
        return 0.0f;
    }
    virtual float gameplayCameraPitchRadians() const
    {
        return 0.0f;
    }
    virtual bool partyIsAirborneForRest() const
    {
        return false;
    }
    virtual bool partyIsFlyingForEventChecks() const
    {
        return false;
    }
    virtual bool partyIsActivelyFlyingForHud() const
    {
        return false;
    }
    virtual bool partyNeedsTurnBasedPhysicsUpdate() const
    {
        return false;
    }
    virtual void syncSpellMovementStatesFromPartyBuffs() = 0;
    virtual void requestPartyJump(float verticalVelocity = 0.0f, float lift = 1.0f) = 0;
    virtual void setAlwaysRunEnabled(bool enabled) = 0;
    virtual void updateWorldMovement(
        const GameplayInputFrame &input,
        float deltaSeconds,
        bool allowWorldInput) = 0;
    virtual const GameplayWorldMovementFrameDiagnostics *lastWorldMovementFrameDiagnostics() const
    {
        return nullptr;
    }
    virtual void setScenarioPartyActorCollisionEnabled(bool enabled)
    {
        (void)enabled;
    }
    virtual void updateActorAi(float deltaSeconds) = 0;
    virtual void updateTurnBasedPausedActorAnimations(float deltaSeconds)
    {
        (void)deltaSeconds;
    }
    virtual size_t turnBasedPendingWorldActionCount() const
    {
        return 0;
    }
    virtual bool turnBasedActorActionInProgress() const
    {
        return false;
    }
    virtual void stopTurnBasedActorMovement()
    {
    }
    // The turn engine schedules turn-based actor actions; the world applies them to actor AI.
    virtual void setTurnBasedActorMode(ActorTurnBasedMode mode)
    {
        (void)mode;
    }
    // Lets the actor take its queued action (attack when it can) in the next actor AI step.
    virtual void grantTurnBasedActorAction(size_t actorIndex)
    {
        (void)actorIndex;
    }
    virtual bool turnBasedActorActionGrantPending() const
    {
        return false;
    }
    // Monster recovery in turn-based initiative ticks, doubled while the actor is slowed.
    virtual int turnBasedActorRecoveryTicks(size_t actorIndex) const
    {
        (void)actorIndex;
        return 100;
    }
    virtual void setActorAttackCooldownSeconds(size_t actorIndex, float seconds)
    {
        (void)actorIndex;
        (void)seconds;
    }
    virtual void updateWorld(float deltaSeconds) = 0;
    virtual void renderWorld(
        int width,
        int height,
        const GameplayInputFrame &input,
        float deltaSeconds) = 0;
    virtual GameplayWorldUiRenderState gameplayUiRenderState(int width, int height) const = 0;
    virtual bool requestTravelAutosave() = 0;
    virtual void cancelPendingMapTransition() = 0;
    virtual bool executeNpcTopicEvent(
        uint16_t eventId,
        size_t &previousMessageCount,
        std::optional<uint8_t> continueStep = std::nullopt) = 0;
    virtual bool executeMapEvent(
        uint16_t eventId,
        size_t &previousMessageCount,
        std::optional<uint8_t> continueStep = std::nullopt) = 0;
    virtual bool executeEventHooks(EventRuntimeHookKind kind)
    {
        (void)kind;
        return false;
    }
    virtual const std::optional<ScriptedEventProgram> *globalEventProgram() const = 0;
    virtual const MapDeltaData *mapDeltaData() const
    {
        return nullptr;
    }
    virtual MapDeltaData *mapDeltaData()
    {
        return nullptr;
    }
    virtual EventRuntimeState *eventRuntimeState() = 0;
    virtual const EventRuntimeState *eventRuntimeState() const = 0;
    virtual bool castEventSpell(
        uint32_t spellId,
        uint32_t skillLevel,
        uint32_t skillMastery,
        int32_t fromX,
        int32_t fromY,
        int32_t fromZ,
        int32_t toX,
        int32_t toY,
        int32_t toZ) = 0;
    virtual const MapArenaDefinition *arenaDefinition() const
    {
        return nullptr;
    }
    virtual size_t mapActorCount() const = 0;
    // Mod runtime access to one field of a map actor. False when the actor or the field is not available.
    virtual bool modActorField(size_t actorIndex, GameplayModActorField field, int64_t &value) const
    {
        static_cast<void>(actorIndex);
        static_cast<void>(field);
        static_cast<void>(value);
        return false;
    }
    virtual bool setModActorField(size_t actorIndex, GameplayModActorField field, int64_t value)
    {
        static_cast<void>(actorIndex);
        static_cast<void>(field);
        static_cast<void>(value);
        return false;
    }
    // Mod runtime access to one of a map actor's spell effects, in the original's numbering (const.MonsterBuff; see
    // ModActorBuffs.h). False when the actor or the effect is not available.
    virtual bool modActorBuff(size_t actorIndex, int buffId, GameplayModActorBuff &buff) const
    {
        static_cast<void>(actorIndex);
        static_cast<void>(buffId);
        static_cast<void>(buff);
        return false;
    }
    virtual bool setModActorBuff(
        size_t actorIndex, int buffId, const GameplayModActorBuff &buff, bool setSeconds, bool setPower)
    {
        static_cast<void>(actorIndex);
        static_cast<void>(buffId);
        static_cast<void>(buff);
        static_cast<void>(setSeconds);
        static_cast<void>(setPower);
        return false;
    }
    // Where a point stands, for mods that place monsters. Outdoors `room` is 0 and the floor is the ground under
    // the point. Indoors `room` is the sector the point is in, 0 when it is in none (inside a wall, outside the
    // map), and the floor is the one under the point, if there is one within reach.
    struct ModPointInfo
    {
        int room = 0;
        std::optional<float> floorZ;
    };
    virtual ModPointInfo modPointInfo(float x, float y, float z) const
    {
        static_cast<void>(x);
        static_cast<void>(y);
        static_cast<void>(z);
        return {};
    }
    virtual bool actorRuntimeState(size_t actorIndex, GameplayRuntimeActorState &state) const = 0;
    virtual GameplayActorHitFeedback &actorHitFeedback() = 0;
    virtual const GameplayActorHitFeedback &actorHitFeedback() const = 0;
    virtual ActorBehaviorRuntime *actorBehaviorRuntime()
    {
        return nullptr;
    }
    virtual const ActorBehaviorRuntime *actorBehaviorRuntime() const
    {
        return nullptr;
    }
    // Gold plus item value left in a dead actor's corpse (its loot is rolled at death); nullopt without corpse loot.
    virtual std::optional<uint32_t> corpseLootValue(size_t actorIndex) const
    {
        (void)actorIndex;
        return std::nullopt;
    }
    virtual bool tryStealFromActor(size_t actorIndex, uint32_t successRoll, uint32_t caughtRoll)
    {
        (void)actorIndex;
        (void)successRoll;
        (void)caughtRoll;
        return false;
    }
    virtual bool actorInspectState(
        size_t actorIndex,
        uint32_t animationTicks,
        GameplayActorInspectState &state) const = 0;
    virtual std::optional<GameplayCombatActorInfo> combatActorInfoById(uint32_t actorId) const = 0;
    virtual bool applyReflectedDamageToActor(
        uint32_t actorId,
        int damage,
        CombatDamageType damageType,
        uint32_t sourcePartyMemberIndex)
    {
        (void)actorId;
        (void)damage;
        (void)damageType;
        (void)sourcePartyMemberIndex;
        return false;
    }
    virtual bool castPartySpellProjectile(const GameplayPartySpellProjectileRequest &request) = 0;
    virtual bool applyPartySpellToActor(
        size_t actorIndex,
        uint32_t spellId,
        uint32_t skillLevel,
        SkillMastery skillMastery,
        int damage,
        float partyX,
        float partyY,
        float partyZ,
        uint32_t sourcePartyMemberIndex) = 0;
    virtual std::vector<size_t> collectMapActorIndicesWithinRadius(
        float centerX,
        float centerY,
        float centerZ,
        float radius,
        bool requireLineOfSight,
        float sourceX,
        float sourceY,
        float sourceZ) const = 0;
    virtual std::vector<size_t> collectVisibleMapActorIndicesWithinRadius(
        float centerX,
        float centerY,
        float centerZ,
        float radius,
        float sourceX,
        float sourceY,
        float sourceZ,
        float viewX,
        float viewY,
        float viewZ,
        float viewYawRadians,
        float viewPitchRadians,
        float viewAspectRatio) const
    {
        (void)viewX;
        (void)viewY;
        (void)viewZ;
        (void)viewYawRadians;
        (void)viewPitchRadians;
        (void)viewAspectRatio;
        return collectMapActorIndicesWithinRadius(centerX, centerY, centerZ, radius, true, sourceX, sourceY, sourceZ);
    }
    virtual bool spawnPartyFireSpikeTrap(
        uint32_t casterMemberIndex,
        uint32_t spellId,
        uint32_t skillLevel,
        uint32_t skillMastery,
        float x,
        float y,
        float z) = 0;
    virtual bool summonFriendlyMonsterById(
        int16_t monsterId,
        uint32_t count,
        float durationSeconds,
        float x,
        float y,
        float z) = 0;
    // Debug: the actor launches its attack or spell projectile at a point now, regardless of range, recovery and
    // targeting, for reviewing projectile visuals. Worlds without support return false.
    virtual bool debugFireActorProjectile(
        size_t actorIndex,
        GameplayActorAttackAbility ability,
        const GameplayWorldPoint &target)
    {
        (void)actorIndex;
        (void)ability;
        (void)target;
        return false;
    }
    virtual bool summonHostileMonsterById(
        int16_t monsterId,
        uint32_t count,
        float x,
        float y,
        float z,
        uint32_t group)
    {
        (void)monsterId;
        (void)count;
        (void)x;
        (void)y;
        (void)z;
        (void)group;
        return false;
    }
    virtual bool tryStartArmageddon(
        size_t casterMemberIndex,
        uint32_t skillLevel,
        SkillMastery skillMastery,
        std::string &failureText) = 0;
    virtual bool canActivateWorldHit(
        const GameplayWorldHit &hit,
        GameplayInteractionMethod interactionMethod) const = 0;
    virtual bool activateWorldHit(const GameplayWorldHit &hit) = 0;
    virtual bool activateWorldHitFromSpell(const GameplayWorldHit &hit, uint32_t spellId)
    {
        (void)spellId;
        return activateWorldHit(hit);
    }
    virtual bool canActivateTelekinesisTarget(const GameplayWorldHit &hit) const = 0;
    virtual bool activateTelekinesisTarget(const GameplayWorldHit &hit) = 0;
    virtual std::optional<GameplayPartyAttackActorFacts> partyAttackActorFacts(
        size_t actorIndex,
        bool visibleForFallback) const = 0;
    virtual std::vector<GameplayPartyAttackActorFacts> collectPartyAttackFallbackActors(
        const GameplayPartyAttackFallbackQuery &query) const = 0;
    virtual std::optional<GameplayWorldPoint> partyAttackActorContactPoint(
        size_t actorIndex,
        const GameplayPartyAttackFallbackQuery &query) const
    {
        (void)actorIndex;
        (void)query;
        return std::nullopt;
    }
    virtual bool applyPartyAttackMeleeDamage(
        size_t actorIndex,
        int damage,
        const GameplayWorldPoint &source) = 0;
    virtual bool applyPartyAttackMeleeBModelDamage(size_t bModelIndex, int damage)
    {
        (void)bModelIndex;
        (void)damage;
        return false;
    }
    virtual bool isPartyAttackMeleeBModelTarget(size_t bModelIndex) const
    {
        (void)bModelIndex;
        return false;
    }
    virtual void applyPartyAttackMeleeEffects(
        size_t actorIndex,
        const CharacterAttackResult &attack,
        const GameplayWorldPoint &source)
    {
        (void)actorIndex;
        (void)attack;
        (void)source;
    }
    virtual bool spawnPartyAttackProjectile(const GameplayPartyAttackProjectileRequest &request) = 0;
    virtual bool castPartyAttackSpell(const GameplayPartyAttackSpellRequest &request) = 0;
    virtual std::vector<GameplayCombatFeedbackEvent> drainCombatFeedbackEvents()
    {
        return {};
    }
    virtual void recordPartyAttackWorldResult(
        std::optional<size_t> actorIndex,
        bool attacked,
        bool actionPerformed) = 0;
    virtual bool worldInteractionReady() const = 0;
    virtual bool worldInspectModeActive() const = 0;
    virtual GameplayWorldPickRequest buildWorldPickRequest(const GameplayWorldPickRequestInput &input) const = 0;
    virtual std::optional<GameplayHeldItemDropRequest> buildHeldItemDropRequest() const = 0;
    virtual GameplayPartyAttackFrameInput buildPartyAttackFrameInput(
        const GameplayWorldPickRequest &pickRequest) const = 0;
    virtual std::optional<size_t> spellActionHoveredActorIndex() const = 0;
    virtual std::optional<size_t> spellActionClosestVisibleHostileActorIndex() const = 0;
    virtual std::optional<bx::Vec3> spellActionActorTargetPoint(size_t actorIndex) const = 0;
    virtual std::optional<bx::Vec3> spellActionGroundTargetPoint(float screenX, float screenY) const = 0;
    virtual GameplayPendingSpellWorldTargetFacts pickPendingSpellWorldTarget(
        const GameplayWorldPickRequest &request) = 0;
    virtual GameplayWorldHit pickKeyboardInteractionTarget(const GameplayWorldPickRequest &request) = 0;
    virtual GameplayWorldHit pickHeldItemWorldTarget(const GameplayWorldPickRequest &request) = 0;
    virtual GameplayWorldHit pickMouseInteractionTarget(const GameplayWorldPickRequest &request) = 0;
    virtual GameplayWorldHit pickPartyAttackTarget(const GameplayWorldPickRequest &request)
    {
        return pickMouseInteractionTarget(request);
    }
    virtual bool hasCustomWorldItemActivation(size_t worldItemIndex) const
    {
        (void)worldItemIndex;
        return false;
    }
    virtual bool activateCustomWorldItem(size_t worldItemIndex)
    {
        (void)worldItemIndex;
        return false;
    }
    virtual bool worldItemInspectState(size_t worldItemIndex, GameplayWorldItemInspectState &state) const
    {
        (void)worldItemIndex;
        (void)state;
        return false;
    }
    virtual bool updateWorldItemInspectState(size_t worldItemIndex, const InventoryItem &item)
    {
        (void)worldItemIndex;
        (void)item;
        return false;
    }
    virtual bool takeWorldItemInspectState(size_t worldItemIndex, GameplayWorldItemInspectState &state)
    {
        (void)worldItemIndex;
        (void)state;
        return false;
    }
    // The items lying on the map, by index (some indexes may hold no item): how many places, where one lies, and
    // moving one (mods: MAW gathers crafting materials to the party).
    virtual size_t worldItemCount() const
    {
        return 0;
    }
    virtual bool worldItemPosition(size_t worldItemIndex, float &x, float &y, float &z) const
    {
        (void)worldItemIndex;
        (void)x;
        (void)y;
        (void)z;
        return false;
    }
    virtual bool moveWorldItem(size_t worldItemIndex, float x, float y, float z)
    {
        (void)worldItemIndex;
        (void)x;
        (void)y;
        (void)z;
        return false;
    }
    // Makes the item lying on the map one of another kind, as it looks too (MAW's randomizer). False for gold or no
    // item there.
    virtual bool setWorldItemKind(size_t worldItemIndex, uint32_t itemId)
    {
        (void)worldItemIndex;
        (void)itemId;
        return false;
    }
    // The map's chests as the map data keeps them (their item records), by chest number; nullptr past the last.
    // A chest's records become its contents the first time it is opened.
    virtual size_t mapChestCount() const
    {
        return 0;
    }
    virtual std::vector<uint8_t> *mapChestItemRecords(size_t chestId)
    {
        (void)chestId;
        return nullptr;
    }
    virtual GameplayWorldHoverCacheState worldHoverCacheState() const = 0;
    virtual GameplayHoverStatusPayload refreshWorldHover(const GameplayWorldHoverRequest &request) = 0;
    virtual GameplayHoverStatusPayload readCachedWorldHover() = 0;
    virtual void clearWorldHover() = 0;
    virtual bool canUseHeldItemOnWorld(const GameplayWorldHit &hit) const = 0;
    virtual bool useHeldItemOnWorld(const GameplayWorldHit &hit) = 0;
    virtual void applyPendingSpellCastWorldEffects(const PartySpellCastResult &castResult) = 0;
    virtual bool dropHeldItemToWorld(const GameplayHeldItemDropRequest &request) = 0;
    virtual bool tryGetGameplayMinimapState(GameplayMinimapState &state) const = 0;
    virtual void collectGameplayMinimapLines(std::vector<GameplayMinimapLineState> &lines) = 0;
    virtual void collectGameplayMinimapMarkers(std::vector<GameplayMinimapMarkerState> &markers) const = 0;
    virtual GameplayChestViewState *activeChestView() = 0;
    virtual const GameplayChestViewState *activeChestView() const = 0;
    virtual void commitActiveChestView() = 0;
    virtual bool takeActiveChestItem(size_t itemIndex, GameplayChestItemState &item) = 0;
    virtual bool takeActiveChestItemAt(uint8_t gridX, uint8_t gridY, GameplayChestItemState &item) = 0;
    virtual bool tryPlaceActiveChestItemAt(const GameplayChestItemState &item, uint8_t gridX, uint8_t gridY) = 0;
    virtual void closeActiveChestView() = 0;
    virtual GameplayCorpseViewState *activeCorpseView() = 0;
    virtual const GameplayCorpseViewState *activeCorpseView() const = 0;
    virtual void commitActiveCorpseView() = 0;
    virtual bool takeActiveCorpseItem(size_t itemIndex, GameplayChestItemState &item) = 0;
    virtual void closeActiveCorpseView() = 0;
};
}
