#pragma once

#include "game/mod/ModHooks.h"
#include "game/mod/ModHost.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

struct lua_State;

namespace OpenYAMM::Engine
{
class AssetFileSystem;
struct ApplicationConfig;
}

namespace OpenYAMM::Game
{
// Hosts MMExtension-style Lua mods in their own Lua state, separate from the sandboxed map-event state.
// The host is inert unless mods.yml enables it, so an unconfigured game pays nothing for it.
class ModRuntime : public IModHooks
{
public:
    struct ModDefinition
    {
        std::string id;
        std::filesystem::path rootPath;
        // Optional folder mounted over the engine assets (table overrides built by tools/maw/convert_tables.py).
        std::filesystem::path assetsPath;
        std::string baseOverrides = "skip";
        std::vector<std::string> skippedScripts;
        // Optional folder shaped like the mod (Scripts/...): a script there replaces the mod's script of the same
        // path, or is added to the load order. For OpenYAMM versions of files that cannot run as shipped.
        std::filesystem::path portPath;
        // Free-form values from mods.yml, handed to the scripts as ModSettings.
        std::vector<std::pair<std::string, std::string>> settings;
    };

    ModRuntime();
    ~ModRuntime();

    ModRuntime(const ModRuntime &) = delete;
    ModRuntime &operator=(const ModRuntime &) = delete;

    // Looks for mods.yml in the working directory, next to the executable, then beside the development asset root,
    // so the game finds it however it was launched. Logs and reports go to a logs folder next to that file.
    bool initialize(const Engine::AssetFileSystem &assetFileSystem);
    // Reads mods.yml and mounts each enabled mod's asset overlay. Call before game tables are loaded; the Lua
    // side is started later by initialize().
    bool configure(Engine::AssetFileSystem &assetFileSystem);
    void shutdown();
    bool isActive() const;
    // Game state the bindings may reach. Set once, before initialize().
    void setHost(ModHost host);
    const ModHost &host() const;

    // Mod state that belongs to a save (Lua vars and mapvars). An empty string restores a clean state.
    std::string captureState();
    void restoreState(const std::string &state);

    int calcDamageToMonster(const ModDamageToMonsterEvent &event) override;
    int calcDamageToPlayer(const ModDamageToPlayerEvent &event) override;
    void monsterKilled(size_t actorIndex) override;
    uint32_t monsterKillExperience(size_t actorIndex, uint32_t engineExperience) override;
    bool playerHitOrMiss(
        std::optional<size_t> partyMemberIndex,
        const Character *pCharacter,
        size_t actorIndex,
        bool ranged,
        bool engineHit) override;
    bool monsterHitOrMiss(size_t actorIndex, size_t partyMemberIndex, bool engineHit) override;
    void playerAttacked(ModPlayerAttackedEvent &event) override;
    void playerCastSpell(ModPlayerCastSpellEvent &event) override;
    bool allowBadThingToPlayer(size_t partyMemberIndex, std::optional<size_t> actorIndex, int thing) override;
    void adjustCharacterBonuses(
        Party &party,
        size_t memberIndex,
        const CharacterStatBonuses &buffBonuses) override;
    bool deferBonusRebuild() override;
    int statisticEffect(int value, int engineEffect) override;
    int attackDelay(const Character &character, bool ranged, int engineTicks) override;
    int itemValue(const InventoryItem &item, int engineValue) override;
    void itemTooltip(const InventoryItem &item, ModItemTooltip &texts, bool fresh) override;
    ModPlayerActionResult playerAction(int action, int param, int param2) override;
    int playerSkillTotal(ModPlayerSkillTotal kind, const Character &character, int engineValue) override;
    bool canHandleItem(bool repair, uint32_t itemId, bool engineAnswer) override;
    void canTeachSkillMastery(ModTeachSkillMasteryEvent &event) override;
    void teacherText(ModTeachSkillMasteryEvent &event) override;
    float trainingMinutes(float engineMinutes) override;
    void statInformationBox(int stat, std::string &text) override;
    int canLearnSpell(const Character &character, uint32_t spellId, int engineMastery) override;
    bool useMouseItem(size_t memberIndex) override;
    bool potionEffect(uint32_t potionId, int power) override;
    bool canWearItem(const Character &character, uint32_t itemId) override;
    void monsterInformationBox(ModMonsterInfoBox &box) override;
    bool townHallBounty(std::string &text) override;
    void mercenaryItemGenerated(InventoryItem &item) override;
    bool monsterDropItem(size_t actorIndex, uint32_t itemId) override;
    void npcTopics(uint32_t npcId, std::vector<ModNpcTopic> &topics) override;
    void questionAnswered(const std::string &answer) override;
    void hudButtons(std::vector<ModHudButton> &buttons) override;
    void hudButtonClicked(int id) override;
    std::vector<std::string> difficultyChoices() override;
    std::string difficulty() override;
    void setDifficulty(const std::string &name) override;
    bool npcGreeting(uint32_t npcId, bool seen, std::string &text) override;
    void npcDialogClosed() override;
    void questLog(std::vector<std::string> &texts) override;
    bool hasGlobalEvent(uint16_t eventId) override;
    int globalEvent(uint16_t eventId, bool topicClick) override;
    bool globalEventDone(uint16_t eventId) override;
    float monsterSpriteScale(size_t actorIndex) override;
    bool hasMapEvent(uint16_t eventId) override;
    int mapEvent(uint16_t eventId) override;
    bool mapEventHint(uint16_t eventId, std::string &hint) override;
    bool mapEventRemoved(uint16_t eventId) override;
    void pickCorpse(size_t actorIndex, ModCorpseLoot &loot) override;
    void generateItem(ModItemGeneration &generation) override;
    void itemGenerated(InventoryItem &item, const ModItemGeneration &generation) override;
    int skillStatBonus(const Character &character, int statId, int engineBonus) override;
    // Takes the scripts' answer for one statistic of the member whose bonuses are being rebuilt: what the items
    // give and what magic gives.
    void setCharacterStatAnswer(size_t memberIndex, int statId, double itemsAnswer, double magicAnswer);

    void fireEvent(const std::string &eventName);
    void tick(float deltaSeconds);
    void keyEvent(bool pressed, int32_t sdlKeycode, bool repeat);
    bool executeConsole(const std::string &code, std::string &output);

    // One line saying whether mods are running and, if not, where mods.yml was looked for.
    std::string statusDescription() const;
    const std::filesystem::path &logDirectory() const;
    // Folder where mods may create and change files (their settings and caches); empty when no mod is enabled.
    const std::filesystem::path &dataDirectory() const;

    void writeLog(const std::string &level, const std::string &text);
    bool isReadablePath(const std::filesystem::path &path) const;
    const Engine::AssetFileSystem *assetFileSystem() const;

    static int windowsVirtualKeyFromSdlKeycode(int32_t sdlKeycode);

private:
    std::filesystem::path findConfigPath();
    bool loadConfiguration();
    bool loadConfig(const std::filesystem::path &configPath, std::string &error);
    void registerNatives();
    void pushConfig();
    bool runAssetChunk(const std::string &virtualPath);
    bool beginDispatch(const char *pKind);
    bool endDispatch(int argumentCount, int resultCount);

    lua_State *m_pLuaState = nullptr;
    const Engine::AssetFileSystem *m_pAssetFileSystem = nullptr;
    std::vector<ModDefinition> m_mods;
    std::filesystem::path m_mergeBasePath;
    ModHost m_host;
    std::filesystem::path m_configPath;
    std::filesystem::path m_logDirectory = "logs";
    std::filesystem::path m_dataDirectory;
    std::vector<std::filesystem::path> m_searchedConfigPaths;
    std::vector<std::filesystem::path> m_readRoots;
    std::ofstream m_logFile;
    int m_dispatchReference = -1;
    bool m_adjustingBonuses = false;
    // Rebuilds the game's party's bonuses when they do not carry this runtime's answers: the party was replaced
    // (a new game, a load, a copy of a party built elsewhere) or a rebuild ran while the scripts were busy.
    void ensureBonusesCurrent();
    bool m_bonusRebuildPending = false;
    // An action happened: the scripts may answer the bonus events differently once their next tick has run (MAW
    // works its items' bonuses out again then), so the bonuses are rebuilt after it.
    bool m_rebuildAfterNextTick = false;
    uint64_t m_bonusRebuildToken = 0;
    // The engine's own figures for the member whose bonuses are being rebuilt, kept while the scripts answer.
    struct BonusRebuild
    {
        bool active = false;
        size_t memberIndex = 0;
        std::vector<int> engineItemBonuses;
        int equippedArmorClass = 0;
        GameMechanics::WeaponDamageItemBonuses weaponDamage = {};
    };
    BonusRebuild m_bonusRebuild;
    // Statistic effects already asked of the scripts this frame. The engine asks for the same few values many
    // times while drawing one frame, and the scripts answer from a formula.
    std::unordered_map<int, int> m_statisticEffects;
    int m_attackDelayDepth = 0;
    int m_itemValueDepth = 0;
    int m_itemGenerationDepth = 0;
    int m_skillTotalDepth = 0;

    // The information box is built again on every frame it is on screen. The scripts' answer for the same item
    // and the same engine texts is kept for a short while instead of being asked for sixty times a second.
    struct ItemTooltipCache
    {
        bool valid = false;
        std::chrono::steady_clock::time_point time;
        std::string itemKey;
        ModItemTooltip engineTexts;
        ModItemTooltip modTexts;
    };
    ItemTooltipCache m_itemTooltipCache;
    int m_skillStatDepth = 0;
    bool m_enabled = false;
    bool m_configured = false;
    bool m_active = false;
};

int runModTableProbe(
    const std::filesystem::path &basePath,
    const Engine::ApplicationConfig &config,
    const std::vector<std::string> &probes);

int runModRuntimeHeadlessReport(
    const std::filesystem::path &basePath,
    const Engine::ApplicationConfig &config,
    int tickCount,
    const std::string &consoleCode);
}
