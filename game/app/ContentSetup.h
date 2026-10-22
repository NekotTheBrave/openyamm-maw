#pragma once

#include <filesystem>
#include <string>

namespace OpenYAMM::Game
{
enum class ContentSetupResult
{
    // The content is installed; the game starts.
    Ready,
    // A newer release was installed and its launcher started; this process ends.
    Restarted,
    // The player closed the window or the content cannot be installed.
    Quit
};

// Whether the index in assetRoot names packs that are not in its store: a fresh installation from the small download
// package, which carries the game but none of its content (packaging/UPDATER_PLAN.md 9.1).
bool installedContentMissing(const std::filesystem::path &assetRoot);

// The first start of such an installation: a window that needs no game content downloads it through the updater, or a
// newer release when one is published.
ContentSetupResult runContentSetup(const std::string &updateChannelUrl);
}
