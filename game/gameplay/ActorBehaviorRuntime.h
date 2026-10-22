#pragma once

#include "engine/scripting/LuaStateOwner.h"
#include "game/gameplay/GameplayActorAiTypes.h"
#include "game/gameplay/GameplayProjectileService.h"
#include "game/party/SkillData.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace OpenYAMM::Game
{
class ActorBehaviorCatalog;
class IGameplayWorldRuntime;
class SpellTable;

struct ActorBehaviorSpell
{
    uint32_t spellId = 0;
    uint32_t skillLevel = 0;
    SkillMastery skillMastery = SkillMastery::None;
    int useChance = 100;
};

struct ActorBehaviorTimer
{
    std::string name;
    float remainingSeconds = 0.0f;
};

// The shield dome drawn for a ward (WorldFxSystem::syncShieldDomes, ShieldDomeRenderer).
struct ActorBehaviorWardVisual
{
    bool dome = true;
    std::array<uint8_t, 3> color = {170, 110, 255};
    float intensity = 1.0f;
    // Rim sharpness: higher values keep the glow closer to the silhouette.
    float rimPower = 3.0f;
    // 0 = smooth surface, 1 = strongly broken into drifting energy.
    float noise = 0.35f;
};

struct ActorBehaviorWard
{
    std::string id;
    float radius = 0.0f;
    // Negative: until removed or the actor dies.
    float remainingSeconds = -1.0f;
    float ageSeconds = 0.0f;
    ActorBehaviorWardVisual visual;
    // Optional effect respawned at the actor while the ward holds, so the player can see its extent.
    std::string effectId;
    float effectScale = 1.0f;
    float effectIntervalSeconds = 0.0f;
    float effectCooldownSeconds = 0.0f;
};

struct ActorBehaviorPerform
{
    std::string clip;
    ActorAiAnimationState fallbackAnimation = ActorAiAnimationState::AttackRanged;
    float durationSeconds = 0.0f;
    float elapsedSeconds = 0.0f;
    // Waits for an attack already under way; the hold begins once the actor is free.
    bool started = false;
};

// Everything a behaviour instance keeps between frames. It is saved with the map, so scripts may rely on it; the
// Lua state itself is not saved (script data lives in scriptState, written after every handler call).
struct ActorBehaviorInstanceState
{
    size_t actorIndex = static_cast<size_t>(-1);
    uint32_t actorId = 0;
    std::string behaviorId;
    bool started = false;
    bool deathHandled = false;
    int lastHp = 0;
    float ageSeconds = 0.0f;
    float thinkCooldownSeconds = 0.0f;
    uint32_t randomState = 0;
    std::vector<uint8_t> scriptState;
    std::vector<float> firedHealthThresholds;
    std::vector<ActorBehaviorTimer> timers;
    std::vector<ActorBehaviorWard> wards;
    std::optional<ActorBehaviorPerform> perform;
    std::optional<ActorBehaviorSpell> spellOverride;
    std::optional<ActorBehaviorSpell> nextCast;
    std::optional<GameplayActorAiType> aiTypeOverride;
    bool suppressFlee = false;
};

// Presentation view of an active ward: where to draw its dome and the projectile hits that ripple it.
struct ActorBehaviorWardDisplay
{
    struct Impact
    {
        // Unit direction from the ward centre to the hit.
        GameplayWorldPoint direction;
        float ageSeconds = 0.0f;
    };

    uint32_t actorId = 0;
    std::string wardId;
    GameplayWorldPoint center;
    float radius = 0.0f;
    float groundZ = 0.0f;
    float ageSeconds = 0.0f;
    float remainingSeconds = -1.0f;
    ActorBehaviorWardVisual visual;
    std::vector<Impact> impacts;
};

struct ActorBehaviorActorFacts
{
    size_t actorIndex = static_cast<size_t>(-1);
    uint32_t actorId = 0;
    int16_t monsterId = 0;
    int currentHp = 0;
    int maxHp = 0;
    bool dead = false;
    bool hostileToParty = false;
    GameplayWorldPoint position;
    float height = 0.0f;
    ActorAiAnimationState animationState = ActorAiAnimationState::Standing;
};

struct ActorBehaviorFrameFacts
{
    float deltaSeconds = 0.0f;
    GameplayWorldPoint partyPosition;
    uint32_t actorCount = 0;
    // Scripted actors, plus actors appended since the last frame (candidates for a monster's default behaviour).
    std::vector<ActorBehaviorActorFacts> actors;
};

struct ActorBehaviorSoundRequest
{
    std::string soundName;
    GameplayWorldPoint position;
};

struct ActorBehaviorEffectRequest
{
    std::string effectId;
    GameplayWorldPoint position;
    float scale = 1.0f;
};

struct ActorBehaviorSummonRequest
{
    int16_t monsterId = 0;
    uint32_t count = 0;
    GameplayWorldPoint position;
};

struct ActorBehaviorFrameResult
{
    std::vector<ActorBehaviorSoundRequest> sounds;
    std::vector<ActorBehaviorEffectRequest> effects;
    std::vector<std::string> statusMessages;
    std::vector<ActorBehaviorSummonRequest> summons;
};

// Per-map Lua behaviours layered on the shared monster AI. Scripts react to events (start, damage, health thresholds,
// timers, wards, death) and issue commands; native AI keeps moving, targeting and attacking. Each world runtime owns
// one, steps it before the actor AI, lets it adjust the AI facts of scripted actors and saves its snapshot.
class ActorBehaviorRuntime
{
public:
    struct Snapshot
    {
        std::vector<ActorBehaviorInstanceState> instances;
        uint32_t scannedActorCount = 0;
    };

    ActorBehaviorRuntime() = default;
    ActorBehaviorRuntime(const ActorBehaviorRuntime &) = delete;
    ActorBehaviorRuntime &operator=(const ActorBehaviorRuntime &) = delete;

    void bind(const ActorBehaviorCatalog *pCatalog, const SpellTable *pSpellTable);
    void reset();

    // Collects facts from the world, runs scripts and applies their commands.
    void step(IGameplayWorldRuntime &world, float deltaSeconds);

    ActorBehaviorFrameFacts collectFrameFacts(const IGameplayWorldRuntime &world, float deltaSeconds) const;
    ActorBehaviorFrameResult update(const ActorBehaviorFrameFacts &facts);
    static void applyFrameResult(IGameplayWorldRuntime &world, const ActorBehaviorFrameResult &result);

    // Applies script directives to scripted actors and the catalogue's tactics to every actor of a monster with them.
    void applyAiFacts(ActorAiFrameFacts &facts);
    void observeAiResult(const ActorAiFrameResult &result);
    void applyPresentation(size_t actorIndex, GameplayRuntimeActorState &state) const;

    const std::vector<GameplayProjectileService::ProjectileWardFacts> &projectileWards() const;
    const std::vector<ActorBehaviorWardDisplay> &wardDisplays() const;
    void recordWardBlock(const GameplayProjectileService::ProjectileWardBlock &block);

    bool attach(size_t actorIndex, uint32_t actorId, const std::string &behaviorId, std::string &error);
    // Map scripts and the console: keeps a running instance of the same behaviour (map on-load events re-run on
    // every load), replaces a different one.
    bool attachToWorldActor(
        const IGameplayWorldRuntime &world,
        size_t actorIndex,
        const std::string &behaviorId,
        std::string &error);
    const ActorBehaviorInstanceState *instance(size_t actorIndex) const;
    std::vector<const ActorBehaviorInstanceState *> instances() const;

    Snapshot snapshot() const;
    void restoreSnapshot(const Snapshot &snapshot);

    // Lua-facing implementation; public only for the C callbacks in ActorBehaviorRuntime.cpp.
    struct LuaCallContext;
    LuaCallContext *activeCall() const;
    const SpellTable *spellTable() const;

private:
    struct InstanceRecord
    {
        ActorBehaviorInstanceState state;
        int selfReference = LUA_NOREF;
        int stateReference = LUA_NOREF;
        bool needsLoadCallback = false;
        bool failed = false;
        std::vector<std::string> pendingWardBlocks;
        // Recent blocks for the dome ripple; presentation only, never saved.
        struct WardImpact
        {
            std::string wardId;
            GameplayWorldPoint point;
            float ageSeconds = 0.0f;
        };
        std::vector<WardImpact> wardImpacts;
        bool performRestartPending = false;
    };

    bool ensureLua();
    void destroyLua();
    int moduleReference(const std::string &behaviorId);
    bool ensureInstanceLua(InstanceRecord &record);
    bool pushHandler(InstanceRecord &record, const char *pHandlerName, const char *pTableKey);
    bool callPushedHandler(
        InstanceRecord &record,
        const ActorBehaviorActorFacts &actor,
        const ActorBehaviorFrameFacts &facts,
        ActorBehaviorFrameResult &result,
        const char *pHandlerName,
        int argumentCount,
        const std::function<void(lua_State *)> &pushArguments);
    bool callHandler(
        InstanceRecord &record,
        const ActorBehaviorActorFacts &actor,
        const ActorBehaviorFrameFacts &facts,
        ActorBehaviorFrameResult &result,
        const char *pHandlerName,
        const char *pTableKey,
        int argumentCount,
        const std::function<void(lua_State *)> &pushArguments);
    void updateInstance(
        InstanceRecord &record,
        const ActorBehaviorActorFacts &actor,
        const ActorBehaviorFrameFacts &facts,
        ActorBehaviorFrameResult &result);
    void fireHealthThresholds(
        InstanceRecord &record,
        const ActorBehaviorActorFacts &actor,
        const ActorBehaviorFrameFacts &facts,
        ActorBehaviorFrameResult &result);
    void rebuildProjectileWards(const ActorBehaviorFrameFacts &facts);
    void applyTactics(ActorAiFrameFacts &facts);
    void reportScriptError(InstanceRecord &record, const std::string &message);

    const ActorBehaviorCatalog *m_pCatalog = nullptr;
    const SpellTable *m_pSpellTable = nullptr;
    std::unordered_map<size_t, InstanceRecord> m_instances;
    uint32_t m_scannedActorCount = 0;
    std::vector<GameplayProjectileService::ProjectileWardFacts> m_projectileWards;
    std::vector<ActorBehaviorWardDisplay> m_wardDisplays;
    // The target each actor with tactics engaged at the last AI step, for its model's aim (presentation only, never
    // saved: the next step after a load fills it again).
    struct TacticAim
    {
        uint32_t actorId = 0;
        GameplayWorldPoint point;
    };
    std::unordered_map<size_t, TacticAim> m_tacticAims;

    std::optional<Engine::LuaStateOwner> m_lua;
    bool m_luaFailed = false;
    std::unordered_map<std::string, int> m_moduleReferences;
    LuaCallContext *m_pActiveCall = nullptr;
};
}
