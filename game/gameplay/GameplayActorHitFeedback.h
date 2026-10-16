#pragma once

#include <cstddef>
#include <vector>

namespace OpenYAMM::Game
{
// Brief presentation after a party hit lands on an actor: an optional flash over its sprite or model, a hitstop that
// holds its animation still, and an optional small camera kick along the blow. Combat timing (action and recovery
// clocks) is unaffected. Transient state, never saved; each world runtime owns one, indexed by its actor indices.
class GameplayActorHitFeedback
{
public:
    void trigger(size_t actorIndex, float flashSeconds, float flashPeak, float hitstopSeconds);
    // Turns the view briefly along the blow's screen direction (right, up), by up to amplitudeRadians.
    void triggerCameraKick(float screenRight, float screenUp, float amplitudeRadians);
    void update(float deltaSeconds);
    void clear();

    // Offsets the renderers add to the camera's yaw and pitch; zero without a kick.
    float cameraKickYawRadians() const;
    float cameraKickPitchRadians() const;

    // Fraction of the flash colour mixed over the actor this frame, 0 when none.
    float flashAmount(size_t actorIndex) const;
    bool hitstopActive(size_t actorIndex) const;

private:
    struct Entry
    {
        size_t actorIndex = 0;
        float flashDurationSeconds = 0.0f;
        float flashRemainingSeconds = 0.0f;
        float flashPeak = 0.0f;
        float hitstopRemainingSeconds = 0.0f;
    };

    const Entry *find(size_t actorIndex) const;
    float cameraKickEnvelope() const;

    std::vector<Entry> m_entries;
    float m_cameraKickYawRadians = 0.0f;
    float m_cameraKickPitchRadians = 0.0f;
    float m_cameraKickElapsedSeconds = 0.0f;
    bool m_cameraKickActive = false;
};
}
