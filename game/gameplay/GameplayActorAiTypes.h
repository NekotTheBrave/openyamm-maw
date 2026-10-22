#pragma once

#include "game/gameplay/GameplayRuntimeInterfaces.h"
#include "game/party/SkillData.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace OpenYAMM::Game
{
enum class ActorAiTargetKind : uint8_t
{
    None = 0,
    Party = 1,
    Actor = 2,
};

enum class ActorAiMotionState : uint8_t
{
    Standing = 0,
    Wandering = 1,
    Pursuing = 2,
    Fleeing = 3,
    Stunned = 4,
    Attacking = 5,
    Dying = 6,
    Dead = 7,
};

enum class ActorAiMovementAction : uint8_t
{
    None = 0,
    Stand = 1,
    Move = 2,
    Pursue = 3,
    Flee = 4,
    Wander = 5,
};

enum class ActorAiAttackRequestKind : uint8_t
{
    None = 0,
    PartyMelee = 1,
    ActorMelee = 2,
    RangedProjectile = 3,
    SpellProjectile = 4,
};

enum class ActorAiAudioRequestKind : uint8_t
{
    None = 0,
    Bored = 1,
    Attack = 2,
    Hit = 3,
    Death = 4,
};

enum class ActorAiFxRequestKind : uint8_t
{
    None = 0,
    Hit = 1,
    Death = 2,
    Spell = 3,
    Buff = 4,
};

struct ActorPartyFacts
{
    GameplayWorldPoint position = {};
    float collisionRadius = 0.0f;
    float collisionHeight = 0.0f;
    bool invisible = false;
    bool hasDispellableBuffs = false;
};

struct ActorIdentityFacts
{
    uint32_t actorId = 0;
    int16_t monsterId = 0;
    std::string displayName;
    uint32_t group = 0;
    uint32_t ally = 0;
    uint8_t hostilityType = 0;
    GameplayActorTargetPolicyState targetPolicy = {};
};

struct ActorStatsFacts
{
    struct AttackDamageFacts
    {
        int diceRolls = 0;
        int diceSides = 0;
        int bonus = 0;
    };

    GameplayActorAiType aiType = GameplayActorAiType::Normal;
    int monsterLevel = 0;
    int currentHp = 0;
    int maxHp = 0;
    int armorClass = 0;
    uint16_t radius = 0;
    uint16_t height = 0;
    uint16_t moveSpeed = 0;
    bool canFly = false;
    bool hasSpell1 = false;
    uint32_t spell1Id = 0;
    std::string spell1Name;
    uint32_t spell1SkillLevel = 0;
    SkillMastery spell1SkillMastery = SkillMastery::None;
    CombatDamageType spell1DamageType = CombatDamageType::Physical;
    bool spell1CastSupported = true;
    bool hasSpell2 = false;
    uint32_t spell2Id = 0;
    std::string spell2Name;
    uint32_t spell2SkillLevel = 0;
    SkillMastery spell2SkillMastery = SkillMastery::None;
    CombatDamageType spell2DamageType = CombatDamageType::Physical;
    bool spell2CastSupported = true;
    int spell1UseChance = 0;
    int spell2UseChance = 0;
    int attack2Chance = 0;
    std::string attack2MissileTypeOverride;
    AttackDamageFacts attack1Damage = {};
    AttackDamageFacts attack2Damage = {};
    CombatDamageType attack1DamageType = CombatDamageType::Physical;
    CombatDamageType attack2DamageType = CombatDamageType::Physical;
    GameplayActorAttackConstraintState attackConstraints = {};
};

struct ActorRuntimeFacts
{
    ActorAiMotionState motionState = ActorAiMotionState::Standing;
    ActorAiAnimationState animationState = ActorAiAnimationState::Standing;
    GameplayActorAttackAbility queuedAttackAbility = GameplayActorAttackAbility::Attack1;
    float animationTimeTicks = 0.0f;
    // A party hit's hitstop (GameplayActorHitFeedback) holds the animation clock still; action timing still runs.
    bool hitstopActive = false;
    float recoverySeconds = 0.0f;
    float attackAnimationSeconds = 0.0f;
    float meleeAttackAnimationSeconds = 0.0f;
    float rangedAttackAnimationSeconds = 0.0f;
    // Time from the start of the attack to its sprite's contact frame (rendering/sprite_attack_timing.yml); 0 applies
    // the attack when the action ends, as MM8 does.
    float meleeAttackImpactSeconds = 0.0f;
    float rangedAttackImpactSeconds = 0.0f;
    // Delay of the attack sound after the attack starts (sprite_attack_timing.yml); 0 plays it as the attack starts.
    float meleeAttackSoundDelaySeconds = 0.0f;
    float rangedAttackSoundDelaySeconds = 0.0f;
    float boredAnimationSeconds = 2.0f;
    float attackCooldownSeconds = 0.0f;
    float idleDecisionSeconds = 0.0f;
    float actionSeconds = 0.0f;
    float crowdSideLockRemainingSeconds = 0.0f;
    float crowdNoProgressSeconds = 0.0f;
    float crowdLastEdgeDistance = 0.0f;
    float crowdRetreatRemainingSeconds = 0.0f;
    float crowdStandRemainingSeconds = 0.0f;
    float crowdProbeEdgeDistance = 0.0f;
    float crowdProbeElapsedSeconds = 0.0f;
    float yawRadians = 0.0f;
    uint32_t idleDecisionCount = 0;
    uint32_t pursueDecisionCount = 0;
    uint32_t attackDecisionCount = 0;
    uint8_t crowdEscapeAttempts = 0;
    int8_t crowdSideSign = 0;
    bool attackImpactTriggered = false;
};

// Action time left when an attack applies at its contact time (seconds from the attack's start); 0 applies it when
// the action ends. The action lasts the attack sprite's animation (startAttack: at least 0.1 s).
inline float actorAttackImpactLeadSeconds(float attackAnimationSeconds, float impactSeconds)
{
    const float actionSeconds = std::max(0.1f, attackAnimationSeconds);
    return impactSeconds > 0.0f && impactSeconds < actionSeconds ? actionSeconds - impactSeconds : 0.0f;
}

struct ActorStatusFacts
{
    GameplayActorSpellEffectState spellEffects = {};
    bool invisible = false;
    bool dead = false;
    bool hostileToParty = false;
    bool bloodSplatSpawned = false;
    bool hasDetectedParty = false;
    bool defaultHostileToParty = false;
    bool suppressLowHealthFlee = false;
    bool forceFleeFromParty = false;
};

struct ActorTargetCandidateFacts
{
    ActorAiTargetKind kind = ActorAiTargetKind::None;
    size_t actorIndex = static_cast<size_t>(-1);
    uint32_t actorId = 0;
    int16_t monsterId = 0;
    int currentHp = 0;
    GameplayActorTargetPolicyState policy = {};
    GameplayWorldPoint position = {};
    GameplayWorldPoint audioPosition = {};
    uint16_t radius = 0;
    uint16_t height = 0;
    bool unavailable = false;
    bool hasLineOfSight = false;
};

struct ActorTargetFacts
{
    ActorAiTargetKind currentKind = ActorAiTargetKind::None;
    size_t currentActorIndex = static_cast<size_t>(-1);
    int currentRelationToTarget = 0;
    int currentHp = 0;
    GameplayWorldPoint currentPosition = {};
    GameplayWorldPoint currentMovementPosition = {};
    GameplayWorldPoint currentAudioPosition = {};
    float currentDistance = 0.0f;
    float currentEdgeDistance = 0.0f;
    bool currentCanSense = false;
    bool currentHasAttackLineOfSight = true;
    bool hasCurrentMovementPosition = false;
    bool partyCanSenseActor = false;
    std::vector<ActorTargetCandidateFacts> candidates;
};

struct ActorMovementFacts
{
    GameplayWorldPoint position = {};
    GameplayWorldPoint homePosition = {};
    float moveDirectionX = 0.0f;
    float moveDirectionY = 0.0f;
    float velocityX = 0.0f;
    float velocityY = 0.0f;
    float velocityZ = 0.0f;
    float wanderRadius = 0.0f;
    float effectiveMoveSpeed = 0.0f;
    float distanceToParty = 0.0f;
    float edgeDistanceToParty = 0.0f;
    size_t contactedActorCount = 0;
    GameplayWorldPoint contactedActorPosition = {};
    uint8_t contactedActorHostilityType = 0;
    bool hasContactedActor = false;
    bool meleePursuitActive = false;
    bool inMeleeRange = false;
    bool allowCrowdSteering = false;
    bool allowIdleWander = false;
    bool movementAllowed = false;
    bool movementBlocked = false;
    bool movementSuppressedByNavigation = false;
    // The map placed the actor `immobile` (a post) and faces it homeYawRadians. Only tactics read these
    // (ActorTacticsFacts::holdPost); spawned actors have no home facing.
    bool placedImmobile = false;
    bool hasHomeYaw = false;
    float homeYawRadians = 0.0f;
};

// Optional combat tactics from world data (worlds/<id>/behaviors.yml `tactics`, keyed by monster id and applied by
// ActorBehaviorRuntime::applyAiFacts). The defaults leave the native AI unchanged.
struct ActorTacticsFacts
{
    // Turns toward its target at this rate (radians per second) instead of snapping to it, keeps tracking it between
    // attacks and starts an attack only within fireToleranceRadians of it. 0 keeps the native instant facing.
    float traverseRadiansPerSecond = 0.0f;
    float fireToleranceRadians = 0.0f;
    // A ranged band (edge distance to the target): commits to its ranged attack instead of the native distance roll,
    // so melee is only chosen against an adjacent target, and between shots backs off inside rangedMinDistance, closes
    // in beyond rangedMaxDistance and circles the target in between. A maximum of 0 keeps the native choice.
    float rangedMinDistance = 0.0f;
    float rangedMaxDistance = 0.0f;
    // A post: an actor placed `immobile` that may still leave its spot to fight, at most postLeashRadius from home
    // (0: unlimited), and with no target walks back and faces its placement direction instead of wandering.
    bool holdPost = false;
    float postLeashRadius = 0.0f;

    bool keepsRangedBand() const
    {
        return rangedMaxDistance > 0.0f;
    }
};

struct ActorWorldFacts
{
    GameplayWorldPoint listenerPosition = {};
    float targetZ = 0.0f;
    float floorZ = 0.0f;
    int sectorId = 0;
    bool active = false;
    bool activeByDistance = false;
    bool sameSectorAsParty = false;
    bool portalPathToParty = false;
};

// Directives from an actor behaviour script (ActorBehaviorRuntime). Absent scripts leave these at defaults.
struct ActorBehaviorFacts
{
    // A scripted action is playing: the actor stands, faces its target and shows holdAnimation.
    bool holdActive = false;
    bool holdRestartAnimation = false;
    ActorAiAnimationState holdAnimation = ActorAiAnimationState::Standing;
};

struct ActorAiFacts
{
    size_t actorIndex = static_cast<size_t>(-1);
    uint32_t actorId = 0;

    ActorIdentityFacts identity = {};
    ActorStatsFacts stats = {};
    ActorRuntimeFacts runtime = {};
    ActorStatusFacts status = {};
    ActorTargetFacts target = {};
    ActorMovementFacts movement = {};
    ActorWorldFacts world = {};
    ActorBehaviorFacts behavior = {};
    ActorTacticsFacts tactics = {};
};

struct ActorStateUpdate
{
    std::optional<ActorAiMotionState> motionState;
    std::optional<uint8_t> hostilityType;
    std::optional<bool> hostileToParty;
    std::optional<bool> hasDetectedParty;
    std::optional<bool> dead;
    std::optional<int> currentHp;
    std::optional<bool> bloodSplatSpawned;
    std::optional<float> recoverySeconds;
    std::optional<float> attackCooldownSeconds;
    std::optional<float> idleDecisionSeconds;
    std::optional<float> actionSeconds;
    std::optional<float> crowdSideLockRemainingSeconds;
    std::optional<float> crowdNoProgressSeconds;
    std::optional<float> crowdLastEdgeDistance;
    std::optional<float> crowdRetreatRemainingSeconds;
    std::optional<float> crowdStandRemainingSeconds;
    std::optional<float> crowdProbeEdgeDistance;
    std::optional<float> crowdProbeElapsedSeconds;
    std::optional<uint32_t> idleDecisionCount;
    std::optional<uint32_t> pursueDecisionCount;
    std::optional<uint32_t> attackDecisionCount;
    std::optional<uint8_t> crowdEscapeAttempts;
    std::optional<int8_t> crowdSideSign;
    std::optional<bool> attackImpactTriggered;
    std::optional<GameplayActorAttackAbility> queuedAttackAbility;
    std::optional<GameplayActorSpellEffectState> spellEffects;
};

struct ActorAnimationUpdate
{
    std::optional<ActorAiAnimationState> animationState;
    std::optional<float> animationTimeTicks;
    bool resetAnimationTime = false;
    bool resetOnAnimationChange = false;
};

struct ActorMovementIntent
{
    ActorAiMovementAction action = ActorAiMovementAction::None;
    GameplayWorldPoint targetPosition = {};
    float moveDirectionX = 0.0f;
    float moveDirectionY = 0.0f;
    float desiredMoveX = 0.0f;
    float desiredMoveY = 0.0f;
    float desiredMoveZ = 0.0f;
    float yawRadians = 0.0f;
    float actionSeconds = 0.0f;
    float moveSpeed = 0.0f;
    float targetEdgeDistance = 0.0f;
    bool targetHasAttackLineOfSight = false;
    bool updateYaw = false;
    // The actor keeps yawRadians while it moves instead of turning to its movement (tactics' back-off: it steps back
    // still facing its target). Indoors the yaw already follows the AI except along a path.
    bool keepYawWhileMoving = false;
    bool clearVelocity = false;
    bool applyMovement = false;
    bool meleePursuitActive = false;
    bool inMeleeRange = false;
    bool resetCrowdSteering = false;
    bool crowdSteeringActive = false;
    bool updateCrowdProbePosition = false;
};

struct ActorAttackRequest
{
    ActorAiAttackRequestKind kind = ActorAiAttackRequestKind::None;
    GameplayActorAttackAbility ability = GameplayActorAttackAbility::Attack1;
    ActorAiTargetKind targetKind = ActorAiTargetKind::None;
    size_t targetActorIndex = static_cast<size_t>(-1);
    uint32_t spellId = 0;
    uint32_t skillLevel = 0;
    SkillMastery skillMastery = SkillMastery::None;
    std::string projectileTokenOverride;
    int damage = 0;
    int attackBonus = 0;
    CombatDamageType damageType = CombatDamageType::Physical;
    GameplayWorldPoint source = {};
    GameplayWorldPoint target = {};
};

struct ActorProjectileRequest
{
    size_t sourceActorIndex = static_cast<size_t>(-1);
    GameplayActorAttackAbility ability = GameplayActorAttackAbility::Attack1;
    ActorAiTargetKind targetKind = ActorAiTargetKind::None;
    uint32_t objectId = 0;
    uint32_t spellId = 0;
    uint32_t skillLevel = 0;
    SkillMastery skillMastery = SkillMastery::None;
    std::string projectileTokenOverride;
    int damage = 0;
    int attackBonus = 0;
    CombatDamageType damageType = CombatDamageType::Physical;
    GameplayWorldPoint source = {};
    GameplayWorldPoint target = {};
};

struct ActorAudioRequest
{
    ActorAiAudioRequestKind kind = ActorAiAudioRequestKind::None;
    size_t actorIndex = static_cast<size_t>(-1);
    GameplayWorldPoint position = {};
};

struct ActorFxRequest
{
    ActorAiFxRequestKind kind = ActorAiFxRequestKind::None;
    size_t actorIndex = static_cast<size_t>(-1);
    uint32_t spellId = 0;
    GameplayWorldPoint position = {};
};

struct ActorAiUpdate
{
    size_t actorIndex = static_cast<size_t>(-1);

    ActorStateUpdate state = {};
    ActorAnimationUpdate animation = {};
    ActorMovementIntent movementIntent = {};

    std::optional<ActorAttackRequest> attackRequest;
    std::vector<ActorAudioRequest> audioRequests;
    std::vector<ActorFxRequest> fxRequests;
};

struct ActorTurnBasedFrameFacts
{
    ActorTurnBasedMode mode = ActorTurnBasedMode::RealTime;
    // Actors the turn engine lets take their queued action in this step, by actor index.
    std::vector<size_t> actionGrantActorIndices;
};

struct ActorAiFrameFacts
{
    float deltaSeconds = 0.0f;
    float fixedStepSeconds = 0.0f;
    ActorPartyFacts party = {};
    ActorTurnBasedFrameFacts turnBased = {};
    std::vector<ActorAiFacts> backgroundActors;
    std::vector<ActorAiFacts> activeActors;
};

struct ActorAiFrameResult
{
    std::vector<ActorAiUpdate> actorUpdates;
    std::vector<size_t> activeActorIndices;
    std::vector<ActorProjectileRequest> projectileRequests;
    std::vector<ActorAudioRequest> audioRequests;
    std::vector<ActorFxRequest> fxRequests;
};
}
