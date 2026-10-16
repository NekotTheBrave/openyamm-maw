#pragma once

#include "game/app/GameSettings.h"
#include "game/mod/ModHooks.h"
#include "game/ui/MenuDesignScreen.h"

#include <filesystem>
#include <functional>

namespace OpenYAMM::Game
{
class GameAudioSystem;
class GameDataRepository;

class MainMenuScreen : public MenuDesignScreen
{
  public:
    using Action = std::function<void()>;
    struct Actions
    {
        Action newGame, loadGame, saveGame, quit, resume, mainMenu;
        std::function<bool(const std::filesystem::path &)> continueGame;
        std::function<bool(const GameSettings &, bool, std::string &)> applySettings;
    };
    MainMenuScreen(const Engine::AssetFileSystem &assets, const GameDataRepository &gameData, GameAudioSystem *pAudio,
                   const GameSettings &settings, Actions actions, bool paused = false, std::string location = {},
                   std::string date = {});
    AppMode mode() const override;
    void prepareForFirstFrame();
    void onExit() override;
    void handleSdlEvent(const SDL_Event &event) override;

  private:
    struct Setting
    {
        std::string id, section, group, name, description, type, unit, depends;
        int minimum = 0, maximum = 100;
        std::vector<std::pair<std::string, std::string>> options;
    };
    struct Binding
    {
        KeyboardAction action;
        std::string id, name, group;
    };
    struct Section
    {
        std::string id, name;
    };
    enum class Page
    {
        Home,
        Settings,
        Credits
    };
    void drawScreen(float deltaSeconds) override;
    void drawHome();
    void drawSettings(float deltaSeconds);
    void drawKeyboard(const std::string &prefix, const Rect &viewport);
    void drawModOptions(const std::string &prefix, const Rect &viewport);
    void refreshModOptions();
    void loadCatalog();
    void editSetting(const Setting &setting, const std::string &value);
    void chooseSetting(const Setting &setting);
    size_t dirtyCount() const;
    void leaveSettings();
    void applySettings(bool leaveAfter = false);
    void restoreDefaults();
    void captureBinding(KeyboardAction action);
    void acceptBinding(InputBinding binding);
    void revertDisplay();
    Actions m_actions;
    const GameDataRepository &m_gameData;
    GameSettings m_committed;
    GameSettings m_draft;
    std::vector<Setting> m_settings;
    std::vector<Binding> m_bindings;
    std::vector<Section> m_sections;
    // The active mod's own tab (section "mod"): its title and options, which take effect as they are changed.
    std::string m_modTitle;
    std::vector<ModSettingsOption> m_modOptions;
    std::optional<std::filesystem::path> m_latestSave;
    std::string m_latestSaveMap;
    std::string m_latestSaveLocation;
    std::optional<KeyboardAction> m_capture;
    Page m_page = Page::Home;
    std::string m_section = "gameplay";
    std::string m_location, m_date, m_status, m_search;
    std::string m_bindingGroup = "All actions";
    bool m_paused = false;
    bool m_searchEditing = false;
    bool m_displayPreview = false;
    bool m_leaveAfterApply = false;
    bool m_suppressEscape = false;
    float m_displaySeconds = 0;
    float m_scroll = 0;
};
} // namespace OpenYAMM::Game
