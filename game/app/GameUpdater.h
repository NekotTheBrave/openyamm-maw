#pragma once

#include "engine/update/AppUpdater.h"

#include <memory>
#include <optional>
#include <ostream>
#include <string>

namespace OpenYAMM::Game
{
// The main menu's view of the self-updater (packaging/UPDATER_PLAN.md 8–10): the versioned Windows and Linux installations
// update in the game; Android hands over to its update screen. Development runs have none.
class GameUpdater
{
public:
    // Null where this build does not update itself.
    static std::unique_ptr<GameUpdater> create(const std::string &baseUrl);

    virtual ~GameUpdater() = default;
    virtual std::string installedVersion() const = 0;
    virtual void check() = 0;
    virtual Engine::UpdateStatus status() const = 0;
    // After an update was found: the desktop downloads it while the menu waits; Android opens its update screen, and
    // the game should quit.
    virtual void startUpdate() = 0;
    virtual bool updatesInGame() const = 0;
    virtual void cancel() = 0;
    // A downloaded update becomes current and starts; the game should quit.
    virtual bool restartIntoUpdate(std::string &error) = 0;
    // The game reached its main menu or finished a headless run.
    virtual void markHealthy(std::ostream &log) = 0;
    // The version a failed update rolled back from, once.
    virtual std::optional<std::string> takeRollbackNotice() = 0;
};

// --headless-update: checks, downloads and installs an update without the menu. 0 when up to date or updated.
int runHeadlessUpdate(const std::string &baseUrl, std::ostream &log);
}
