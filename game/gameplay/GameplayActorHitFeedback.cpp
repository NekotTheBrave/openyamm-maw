#include "game/gameplay/GameplayActorHitFeedback.h"

#include <algorithm>
#include <iterator>

namespace OpenYAMM::Game
{
namespace
{
// The kick reaches its offset quickly, then eases back.
constexpr float CameraKickRiseSeconds = 0.03f;
constexpr float CameraKickReturnSeconds = 0.16f;
}

void GameplayActorHitFeedback::trigger(size_t actorIndex, float flashSeconds, float flashPeak, float hitstopSeconds)
{
    std::vector<Entry>::iterator entryIt = std::find_if(m_entries.begin(), m_entries.end(),
        [actorIndex](const Entry &entry)
        {
            return entry.actorIndex == actorIndex;
        });

    if (entryIt == m_entries.end())
    {
        m_entries.push_back({actorIndex});
        entryIt = std::prev(m_entries.end());
    }

    // The flash restarts on every hit; a hitstop already running is never cut short by a lighter one.
    entryIt->flashDurationSeconds = std::max(0.0f, flashSeconds);
    entryIt->flashRemainingSeconds = entryIt->flashDurationSeconds;
    entryIt->flashPeak = std::clamp(flashPeak, 0.0f, 1.0f);
    entryIt->hitstopRemainingSeconds = std::max(entryIt->hitstopRemainingSeconds, std::max(0.0f, hitstopSeconds));
}

void GameplayActorHitFeedback::triggerCameraKick(float screenRight, float screenUp, float amplitudeRadians)
{
    // A positive yaw turns the view left and a positive pitch raises it.
    m_cameraKickYawRadians = -screenRight * amplitudeRadians;
    m_cameraKickPitchRadians = screenUp * amplitudeRadians;
    m_cameraKickElapsedSeconds = 0.0f;
    m_cameraKickActive = amplitudeRadians != 0.0f;
}

void GameplayActorHitFeedback::update(float deltaSeconds)
{
    if (deltaSeconds <= 0.0f)
    {
        return;
    }

    if (m_cameraKickActive)
    {
        m_cameraKickElapsedSeconds += deltaSeconds;
        m_cameraKickActive = m_cameraKickElapsedSeconds < CameraKickRiseSeconds + CameraKickReturnSeconds;
    }

    if (m_entries.empty())
    {
        return;
    }

    for (Entry &entry : m_entries)
    {
        entry.hitstopRemainingSeconds = std::max(0.0f, entry.hitstopRemainingSeconds - deltaSeconds);
        entry.flashRemainingSeconds = std::max(0.0f, entry.flashRemainingSeconds - deltaSeconds);
    }

    std::erase_if(m_entries, [](const Entry &entry)
    {
        return entry.flashRemainingSeconds <= 0.0f && entry.hitstopRemainingSeconds <= 0.0f;
    });
}

void GameplayActorHitFeedback::clear()
{
    m_entries.clear();
    m_cameraKickActive = false;
}

float GameplayActorHitFeedback::cameraKickEnvelope() const
{
    if (!m_cameraKickActive)
    {
        return 0.0f;
    }

    if (m_cameraKickElapsedSeconds < CameraKickRiseSeconds)
    {
        return m_cameraKickElapsedSeconds / CameraKickRiseSeconds;
    }

    const float remaining = 1.0f - (m_cameraKickElapsedSeconds - CameraKickRiseSeconds) / CameraKickReturnSeconds;
    return std::clamp(remaining, 0.0f, 1.0f) * std::clamp(remaining, 0.0f, 1.0f);
}

float GameplayActorHitFeedback::cameraKickYawRadians() const
{
    return m_cameraKickYawRadians * cameraKickEnvelope();
}

float GameplayActorHitFeedback::cameraKickPitchRadians() const
{
    return m_cameraKickPitchRadians * cameraKickEnvelope();
}

float GameplayActorHitFeedback::flashAmount(size_t actorIndex) const
{
    const Entry *pEntry = find(actorIndex);

    if (pEntry == nullptr || pEntry->flashDurationSeconds <= 0.0f)
    {
        return 0.0f;
    }

    // Quadratic fade: a bright first frames, then a quick fall-off.
    const float remaining = pEntry->flashRemainingSeconds / pEntry->flashDurationSeconds;
    return pEntry->flashPeak * remaining * remaining;
}

bool GameplayActorHitFeedback::hitstopActive(size_t actorIndex) const
{
    const Entry *pEntry = find(actorIndex);
    return pEntry != nullptr && pEntry->hitstopRemainingSeconds > 0.0f;
}

const GameplayActorHitFeedback::Entry *GameplayActorHitFeedback::find(size_t actorIndex) const
{
    for (const Entry &entry : m_entries)
    {
        if (entry.actorIndex == actorIndex)
        {
            return &entry;
        }
    }

    return nullptr;
}
}
