#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace OpenYAMM::Game
{
class Party;
struct GameplayInputFrame;
class IGameplayWorldRuntime;
enum class ActorTurnBasedMode : uint8_t;

enum class TurnBasedCombatStage : uint8_t
{
    None,
    Wait,
    Attack,
    Movement,
};

class TurnBasedCombatRuntime
{
public:
    struct QueueEntry
    {
        enum class Kind : uint8_t
        {
            Character,
            Actor,
        };

        Kind kind = Kind::Character;
        size_t id = 0;
        int initiative = 0;
        float actionRecoverySeconds = 0.0f;
    };

    bool active() const;
    TurnBasedCombatStage stage() const;
    int movementActionPoints() const;
    int pendingActions() const;
    bool hasPendingActions() const;
    bool playerActionReady(const Party &party) const;
    bool shouldUpdateActorAi(const Party *pParty) const;
    ActorTurnBasedMode actorMode() const;
    bool frontQueueCharacter(size_t &memberIndex) const;
    const std::vector<QueueEntry> &queue() const;

    bool begin(Party &party, const IGameplayWorldRuntime *pWorldRuntime);
    void end(Party *pParty, IGameplayWorldRuntime *pWorldRuntime, bool preserveRecovery = true);
    void reset();

    bool canToggleOff(const Party &party) const;
    bool toggle(Party &party, IGameplayWorldRuntime *pWorldRuntime);
    bool update(Party *pParty, IGameplayWorldRuntime *pWorldRuntime, float deltaSeconds);

    // Movement-stage input spends the movement budget by walking time (one point per 1/128 s held).
    bool noteMovementInput(GameplayInputFrame &input, float deltaSeconds);
    float movementDeltaSecondsForFrame(float fallbackDeltaSeconds) const;
    float consumePendingGameTimeAdvanceMinutes();
    void clearMovementInput(GameplayInputFrame &input) const;
    bool finishMovementPhase();

    bool canBeginPlayerAction(const Party &party) const;
    bool beginPlayerActionOrFinishMovement(Party &party);
    bool applyPlayerAction(Party &party, size_t memberIndex, float recoverySeconds);
    // Pass ends the party's movement phase, or spends the ready member's action without acting.
    bool canPassTurn(const Party &party) const;
    bool passTurn(Party &party, float recoverySeconds);
    void storeMemberTurnRecovery(size_t memberIndex, float recoverySeconds);

    void registerPendingAction();
    void resolvePendingAction();

private:
    static constexpr size_t MaximumTrackedMembers = 5;

    void rebuildInitialQueue(Party &party, const IGameplayWorldRuntime *pWorldRuntime);
    void addArrivedCombatants(const Party &party, const IGameplayWorldRuntime *pWorldRuntime);
    void removeInvalidActorEntries(const IGameplayWorldRuntime *pWorldRuntime);
    std::optional<size_t> readyCharacterQueueIndex(const Party *pParty) const;
    bool readyActorQueued() const;
    void sortQueue(Party *pParty);
    void selectFrontCharacter(Party *pParty);
    void removeInvalidEntries(const Party &party);
    void advanceAttackQueue(Party *pParty);
    void beginReadyActorActions(Party *pParty, IGameplayWorldRuntime *pWorldRuntime);
    void updateAttackStage(Party *pParty, IGameplayWorldRuntime *pWorldRuntime, float deltaSeconds);
    void enterWaitStage();
    void enterAttackStage(Party *pParty, IGameplayWorldRuntime *pWorldRuntime);
    void enterMovementStage();
    float spendMovementBudget(float deltaSeconds);
    void clearMovementLatches();

    bool m_active = false;
    TurnBasedCombatStage m_stage = TurnBasedCombatStage::None;
    std::vector<QueueEntry> m_queue;
    int m_turnInitiative = 100;
    float m_aiTurnTimerTicks = 0.0f;
    float m_movementActionPoints = 0.0f;
    int m_pendingActions = 0;
    uint32_t m_turnCount = 0;
    float m_actorActionTimerTicks = 0.0f;
    bool m_movementFinished = false;
    bool m_movementStepThisFrame = false;
    float m_movementStepSeconds = 0.0f;
    bool m_waitingForActorActionResolution = false;
    // Attack stage: granted actors are taking their queued action; the queue waits for them.
    bool m_resolvingActorActions = false;
    float m_pendingGameTimeAdvanceMinutes = 0.0f;
    bool m_forwardLatch = false;
    bool m_backwardLatch = false;
    bool m_strafeLeftLatch = false;
    bool m_strafeRightLatch = false;
    std::optional<size_t> m_loggedReadyMemberIndex;
    float m_memberTurnRecoverySeconds[MaximumTrackedMembers] = {};
};
} // namespace OpenYAMM::Game
