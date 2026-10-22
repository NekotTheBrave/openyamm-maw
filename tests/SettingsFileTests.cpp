#include "game/app/GameSettings.h"

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using OpenYAMM::Game::GameSettings;
using OpenYAMM::Game::SettingsEntry;

namespace
{
std::filesystem::path temporarySettingsPath(const char *pName)
{
    const uint64_t tickCount = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / ("openyamm_settings_" + std::to_string(tickCount));
    std::filesystem::create_directories(directory);
    return directory / pName;
}

std::string readText(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::vector<std::string> savedNames(const GameSettings &settings)
{
    std::vector<std::string> names;

    for (const SettingsEntry &entry : OpenYAMM::Game::settingsEntriesToSave(settings))
    {
        names.push_back(entry.section + '.' + entry.key);
    }

    return names;
}

bool contains(const std::vector<std::string> &names, const std::string &name)
{
    return std::find(names.begin(), names.end(), name) != names.end();
}

GameSettings loadText(const std::string &text)
{
    std::string error;
    std::optional<GameSettings> settings = OpenYAMM::Game::loadGameSettingsFromText(text, error);
    REQUIRE_MESSAGE(settings, error);
    return *settings;
}

const std::vector<std::string> ProfileAndState = {"profile.name", "profile.version", "debug.start_world"};
}

TEST_CASE("saving default settings writes only the profile and remembered state")
{
    CHECK(savedNames(GameSettings::createDefault()) == ProfileAndState);
}

TEST_CASE("sparse settings round trip and only changed values are written")
{
    GameSettings settings = GameSettings::createDefault();
    settings.soundVolume = 7;
    settings.alwaysRun = !settings.alwaysRun;
    const std::vector<std::string> names = savedNames(settings);
    CHECK(names.size() == ProfileAndState.size() + 2);
    CHECK(contains(names, "audio.sound_volume"));
    CHECK(contains(names, "controls.always_run"));

    const std::filesystem::path path = temporarySettingsPath("settings.ini");
    std::string error;
    REQUIRE(OpenYAMM::Game::saveGameSettings(path, settings, error));
    std::optional<GameSettings> loaded = OpenYAMM::Game::loadGameSettings(path, error);
    REQUIRE_MESSAGE(loaded, error);
    CHECK(loaded->soundVolume == 7);
    CHECK(loaded->alwaysRun == settings.alwaysRun);
    CHECK(OpenYAMM::Game::serializeGameSettings(*loaded) == OpenYAMM::Game::serializeGameSettings(settings));
    CHECK_FALSE(OpenYAMM::Game::migrateGameSettings(*loaded));
    std::filesystem::remove_all(path.parent_path());
}

TEST_CASE("a changed default reaches files without the key and spares files with it")
{
    // The file of a player who never touched the volume, and of one who chose today's default explicitly.
    const GameSettings untouched = loadText("[profile]\nname=desktop\nversion=5\n");
    const GameSettings chosen = loadText("[profile]\nname=desktop\nversion=5\n[audio]\nsound_volume=3\n");
    CHECK(untouched.soundVolume == GameSettings::createDefault().soundVolume);
    CHECK_FALSE(contains(savedNames(untouched), "audio.sound_volume"));
    // An earlier choice stays a choice even while it equals the default, so a later default change spares it.
    CHECK(contains(savedNames(chosen), "audio.sound_volume"));
}

TEST_CASE("restoring defaults releases the stored keys")
{
    GameSettings settings = loadText(
        "[profile]\nname=desktop\nversion=5\n[audio]\nsound_volume=3\n[input]\nattack=Mouse Left\n");
    REQUIRE(contains(savedNames(settings), "audio.sound_volume"));
    REQUIRE(contains(savedNames(settings), "input.attack"));
    OpenYAMM::Game::releaseStoredSettings(settings, {"sound_volume", "input."});
    CHECK(savedNames(settings) == ProfileAndState);
}

TEST_CASE("unknown settings survive a save and launch-only directives do not")
{
    const std::filesystem::path path = temporarySettingsPath("settings.ini");
    {
        std::ofstream file(path);
        file << "[profile]\nname=desktop\nversion=5\n[audio]\nfuture_volume=4\n[mod_section]\nflavour=spicy\n"
             << "[debug]\nscreenshot_path=/tmp/once.png\n[controls]\nwalk_sound=false\n";
    }

    std::string error;
    std::optional<GameSettings> loaded = OpenYAMM::Game::loadGameSettings(path, error);
    REQUIRE_MESSAGE(loaded, error);
    CHECK(loaded->screenshotPath == "/tmp/once.png");
    CHECK_FALSE(loaded->walksound);
    REQUIRE(OpenYAMM::Game::saveGameSettings(path, *loaded, error));

    const std::string saved = readText(path);
    CHECK(saved.find("[audio]\nfuture_volume=4\n") != std::string::npos);
    CHECK(saved.find("[mod_section]\nflavour=spicy\n") != std::string::npos);
    CHECK(saved.find("screenshot_path") == std::string::npos);
    // The retired key became its replacement.
    CHECK(saved.find("walk_sound") == std::string::npos);
    CHECK(saved.find("walksound=false") != std::string::npos);
    std::filesystem::remove_all(path.parent_path());
}

TEST_CASE("settings written by a newer version keep their version and are not migrated")
{
    GameSettings settings = loadText("[profile]\nname=desktop\nversion=9\n[audio]\nsound_volume=3\n");
    CHECK_FALSE(OpenYAMM::Game::migrateGameSettings(settings));
    CHECK(settings.settingsProfileVersion == 9);
    const std::vector<SettingsEntry> entries = OpenYAMM::Game::settingsEntriesToSave(settings);
    REQUIRE(entries.size() >= 2);
    CHECK(entries[1].value == "9");
    CHECK(contains(savedNames(settings), "audio.sound_volume"));
}

TEST_CASE("converting shipped release settings files leaves only the player's choices")
{
    const std::filesystem::path fixtures = std::filesystem::path(OPENYAMM_SOURCE_DIR) / "tests/fixtures/settings";

    for (const char *pFixture : {"desktop_release_1.0.ini", "desktop_release_1.5.ini"})
    {
        CAPTURE(pFixture);
        GameSettings untouched = loadText(readText(fixtures / pFixture));
        CHECK(OpenYAMM::Game::migrateGameSettings(untouched));
        CHECK(untouched.settingsProfileVersion == OpenYAMM::Game::CurrentSettingsProfileVersion);
        CHECK(savedNames(untouched) == ProfileAndState);

        std::string changed = readText(fixtures / pFixture);
        const size_t volume = changed.find("sound_volume=");
        REQUIRE(volume != std::string::npos);
        changed.replace(volume, changed.find('\n', volume) - volume, "sound_volume=8");
        changed += "\n[controls]\nmouse_sensitivity=77\n";
        GameSettings choices = loadText(changed);
        CHECK(OpenYAMM::Game::migrateGameSettings(choices));
        CHECK(choices.soundVolume == 8);
        CHECK(choices.mouseSensitivity == 77);
        std::vector<std::string> expected = ProfileAndState;
        expected.insert(expected.begin() + 2, {"audio.sound_volume", "controls.mouse_sensitivity"});
        CHECK(savedNames(choices) == expected);
    }
}

TEST_CASE("settings saves keep the previous file as a backup")
{
    const std::filesystem::path path = temporarySettingsPath("settings.ini");
    GameSettings settings = GameSettings::createDefault();
    settings.soundVolume = 5;
    std::string error;
    REQUIRE(OpenYAMM::Game::saveGameSettings(path, settings, error));
    const std::string first = readText(path);

    settings.soundVolume = 6;
    REQUIRE(OpenYAMM::Game::saveGameSettings(path, settings, error));
    CHECK(readText(path.string() + ".bak") == first);
    CHECK(readText(path).find("sound_volume=6") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(path.string() + ".tmp"));
    std::filesystem::remove_all(path.parent_path());
}


