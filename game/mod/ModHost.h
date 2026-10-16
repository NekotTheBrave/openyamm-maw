#pragma once

#include <functional>
#include <string>

namespace OpenYAMM::Engine
{
class AssetFileSystem;
}

namespace OpenYAMM::Game
{
class GameDataLoader;
class IGameplayWorldRuntime;
class Party;
struct InventoryItem;

// What the mod runtime may reach in the running game. The application fills this in; every accessor may return
// null (or an empty value) when there is no session yet, and bindings treat that as "nothing there".
struct ModHost
{
    // The game's assets (set by the mod runtime): a mod's icons, say.
    const Engine::AssetFileSystem *pAssetFileSystem = nullptr;
    std::function<Party *()> party;
    std::function<IGameplayWorldRuntime *()> world;
    std::function<const GameDataLoader *()> data;
    std::function<std::string()> currentMapFileName;
    // True once the current map's world exists and is not in the middle of being loaded.
    std::function<bool()> mapReady;
    std::function<float()> gameMinutes;
    std::function<void(const std::string &)> showStatusText;
    // Which screen is up, in the original's numbers (const.Screens): 0 the game itself, 7 the character screen, 13 a
    // house, 16 the main menu, and so on.
    std::function<int()> currentScreen;
    // The character screen's page, in the original's numbers (const.CharScreens): 100 stats, 101 skills, 102 awards,
    // 103 inventory.
    std::function<int()> currentCharScreen;
    // The item the player holds on the mouse pointer, nullptr when none.
    std::function<InventoryItem *()> heldItem;
    // Puts an item on the mouse pointer in place of the one there; nullptr takes it off.
    std::function<void(const InventoryItem *)> setHeldItem;
    // Brings up the dialogue an event left waiting (a travel prompt, an NPC), as the next frame would.
    std::function<void()> presentPendingDialogue;
    // Hands the items an event gave to the party (into the hand, the one held before into the pack), as the game
    // does after an event the player set off.
    std::function<void()> handOverEventItems;
    // The difficulty the game's settings name for a new game ("" for the mod's own default).
    std::function<std::string()> difficultySetting;
    // Turn-based combat's phase in the original's numbers (Game.TurnBasedPhase): 0 not in turn-based combat, 1 the
    // monsters' turn (and the wait between), 2 the party's attacks, 3 the party walking.
    std::function<int()> turnBasedPhase;
    // Ends the party's turn-based walk, as the original does when its phase is set away from 3.
    std::function<void()> endTurnBasedWalk;
    // The shop page open, in the original's numbers (Game.HouseScreen): 2 a shop's goods, 95 its special goods, 0
    // none.
    std::function<int()> houseShopPage;
    // Leaves the screen a script asks to leave (Game:ExitHouseScreen(), ExitCurrentScreen()): the character screen
    // at once, a house's or an NPC's dialog at the start of the next frame.
    std::function<void()> exitScreen;
};
}
