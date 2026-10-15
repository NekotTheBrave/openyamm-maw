#pragma once

#include "game/gameplay/GameMechanics.h"
#include "game/tables/MonsterTable.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace OpenYAMM::Game
{
// Points in gameplay where a mod may change the outcome. Gameplay code asks activeModHooks() and, when a mod runtime
// is present, lets it adjust the value; with no runtime the pointer is null and gameplay is untouched.

// What a monster's body is about to be given, as the original's fields of the monster name it: gold as dice, the
// chance in 100 of an item and its treasure level. `anyItemKind` lifts the kind's restriction on what the item can
// be; `allowed` false leaves the body empty.
struct ModCorpseLoot
{
    int goldDiceCount = 0;
    int goldDiceSides = 0;
    int itemPercent = 0;
    int itemLevel = 0;
    bool anyItemKind = false;
    bool allowed = true;
};

// What the mod made of one of the player's actions (the original's Action event): `handled` when a handler did
// the action itself, and the game action a handler asked for in its place (DoGameAction), 0 for none.
struct ModPlayerActionResult
{
    bool handled = false;
    int requestedAction = 0;
};

// The original's numbers for the actions the event is raised for.
namespace Mm8Action
{
constexpr int Attack = 23;
// Param 0: the quick spell key; param 1: the attack spell in place of an attack.
constexpr int QuickSpell = 25;
constexpr int OpenSpellbook = 105;
// Param: the member's number from 1 (a click on the portrait).
constexpr int SelectCharacter = 110;
// A screen is left (raised while it is still the current one): the character screen, the spellbook.
constexpr int Exit = 113;
// The character screen is opened.
constexpr int OpenCharacterScreen = 114;
// Param: the skill's Merge number (the skill page's raise of a skill with skill points).
constexpr int RaiseSkill = 121;
// The held item put on the character's doll (Mouse.Item is the item).
constexpr int EquipOnDoll = 133;
// Param: the spell; param 2: the caster's member index (a cast from the spellbook).
constexpr int CastFromSpellbook = 142;
// The key that moves to the next member.
constexpr int NextCharacter = 176;
}

// A character's total in a skill the game asks of one character (the original's GetMerchantTotalSkill and
// GetDisarmTrapTotalSkill events).
enum class ModPlayerSkillTotal
{
    Merchant,
    DisarmTrap,
    // The percentage more experience the character gets (GetLearningTotalSkill).
    Learning,
};

// A random item being made. `treasureLevel` is the original's "strength", 1 to 6 (7 for artifacts); `mode` says
// what it is made for: 0 anything, 1 a shop's shelf, 2 a monster's body, 3 a chest.
struct ModItemGeneration
{
    int treasureLevel = 1;
    int mode = 0;
};

// The texts of an item's information box, as the original's BuildItemInformationBox event names them: the item's
// name, its type line, its basic statistic ("Damage: 2d3", "Armor: +18"), its enchantment line, its description.
struct ModItemTooltip
{
    std::string name;
    std::string type;
    std::string basicStat;
    std::string enchantment;
    std::string description;

    bool operator==(const ModItemTooltip &) const = default;
};

// The party hits a map actor. `rawDamage` is the attack's damage before the target's resistances, `resolvedDamage`
// what the engine would apply after them; the hook returns the damage to apply.
struct ModDamageToMonsterEvent
{
    size_t actorIndex = 0;
    std::optional<size_t> partyMemberIndex;
    int rawDamage = 0;
    int resolvedDamage = 0;
    CombatDamageType damageType = CombatDamageType::Physical;
    uint32_t spellId = 0;
    bool melee = true;
    // Set when the hit comes from something in the world (an arrow, a spell missile, a spell's effect) rather than
    // from a blow: where that is, and for a spell the skill it was cast with.
    bool fromObject = false;
    uint32_t skillLevel = 0;
    uint32_t skillMastery = 0;
    float objectX = 0.0f;
    float objectY = 0.0f;
    float objectZ = 0.0f;
};

// A party member takes damage. `rawDamage` is the amount before the character's resistances, `resolvedDamage` what
// the engine would apply after them; the hook returns the damage to apply. The attacker is a map actor when there
// is one (a trap has none); `attackAction` says which of its attacks it was (0 and 1 its two attacks, 2 and 3 its
// two spells), and `fromObject` that it arrived as a missile rather than a blow.
struct ModDamageToPlayerEvent
{
    size_t partyMemberIndex = 0;
    int rawDamage = 0;
    int resolvedDamage = 0;
    CombatDamageType damageType = CombatDamageType::Physical;
    std::optional<size_t> actorIndex;
    int attackAction = 0;
    uint32_t spellId = 0;
    bool fromObject = false;
};

// A monster's attack is about to be resolved against a party member, before any roll. The mod may pick another
// member (`partyMemberIndex`) or take the attack over altogether (`handled`: the engine then does nothing more).
struct ModPlayerAttackedEvent
{
    size_t partyMemberIndex = 0;
    std::optional<size_t> actorIndex;
    int attackAction = 0;
    uint32_t spellId = 0;
    bool fromObject = false;
    bool handled = false;
};

// A party member's spell is about to take effect: targets are chosen and the cost is known. The mod may change
// the skill the spell is cast with, or take the cast over (`handled`: no effect, no cost, no recovery).
struct ModPlayerCastSpellEvent
{
    size_t partyMemberIndex = 0;
    uint32_t spellId = 0;
    int manaCost = 0;
    uint32_t skillLevel = 0;
    uint32_t skillMastery = 0;
    std::optional<size_t> targetActorIndex;
    std::optional<size_t> targetPartyMemberIndex;
    bool fromScroll = false;
    bool handled = false;
};

// A teacher is asked for a skill mastery (the original's CanTeachSkillMastery). The fields come in as the engine's
// verdict: whether it would teach, for how much gold, and what the teacher says; the mod may change any of them.
// `mastery` is 2 Expert, 3 Master, 4 Grandmaster.
struct ModTeachSkillMasteryEvent
{
    size_t partyMemberIndex = 0;
    std::string skillName;
    int mastery = 0;
    int cost = 0;
    bool allow = false;
    std::string text;
};

class Party;

// The texts of a monster's information box (right click), as the original lays them out: a row is its label, a
// tab stop and its value ("Hit Points\t100" then the value). A mod may change any of them, or empty a row.
struct ModMonsterInfoBox
{
    size_t actorIndex = 0;
    std::string name;
    std::string hitPoints;
    std::string armorClass;
    std::string attack;
    std::string damage;
    std::string spellFirst;
    std::string spellSecond;
    std::string effectsHeader;
    std::vector<std::string> effects;
    std::string resistancesHeader;
    std::vector<std::string> resistances;

    bool operator==(const ModMonsterInfoBox &other) const = default;
};

class IModHooks
{
public:
    virtual ~IModHooks() = default;

    virtual int calcDamageToMonster(const ModDamageToMonsterEvent &event) = 0;
    virtual int calcDamageToPlayer(const ModDamageToPlayerEvent &event) = 0;

    // A map actor has just died, whatever killed it.
    virtual void monsterKilled(size_t actorIndex) = 0;

    // The experience the party gets for a monster it has just killed; `engineExperience` is the engine's answer
    // from the monster table. Mods give single monsters an experience value of their own.
    virtual uint32_t monsterKillExperience(size_t actorIndex, uint32_t engineExperience) = 0;

    // The two rolls that decide whether an attack lands; `engineHit` is the engine's own roll and the answer is
    // the one to use. For the party's attack the member is given by slot when the caller knows it, otherwise by
    // the character itself.
    virtual bool playerHitOrMiss(
        std::optional<size_t> partyMemberIndex,
        const Character *pCharacter,
        size_t actorIndex,
        bool ranged,
        bool engineHit) = 0;
    virtual bool monsterHitOrMiss(size_t actorIndex, size_t partyMemberIndex, bool engineHit) = 0;

    virtual void playerAttacked(ModPlayerAttackedEvent &event) = 0;
    virtual void playerCastSpell(ModPlayerCastSpellEvent &event) = 0;

    // A monster's special attack (curse, poison, breaking an item, ...) is about to be tried on a party member,
    // before the member's saving throw. `thing` is the original engine's number for it (const.MonsterBonus).
    // Returns false to forbid it.
    virtual bool allowBadThingToPlayer(size_t partyMemberIndex, std::optional<size_t> actorIndex, int thing) = 0;

    // Called at the end of Party::rebuildMagicalBonusesFromBuffs for each member. At that point the member's
    // magicalBonuses holds buffs plus equipped items and itemSkillBonuses holds item skill bonuses; `buffBonuses`
    // is the buffs-only part. The mod may replace the item part, the magic part and the skill bonuses.
    virtual void adjustCharacterBonuses(
        Party &party,
        size_t memberIndex,
        const CharacterStatBonuses &buffBonuses) = 0;

    // True while the mod is answering a bonus rebuild. A rebuild asked for in the middle of that would run without
    // the mod and overwrite what it has already answered, so the caller skips it; the runtime runs one afterwards.
    virtual bool deferBonusRebuild() = 0;

    // The effect of a statistic's value on what it governs (the engine's threshold table: 13 gives 0, 15 gives 1,
    // ...). `engineEffect` is the table's answer; the hook returns the effect to use.
    virtual int statisticEffect(int value, int engineEffect) = 0;

    // Recovery after an attack, in the original engine's ticks (100 is an unarmed swing, 30 its floor for melee).
    // `ranged` is true for the bow slot and false for whatever the main hand does. `engineTicks` is the engine's
    // answer; the hook returns the ticks to use.
    virtual int attackDelay(const Character &character, bool ranged, int engineTicks) = 0;
    // What an item is worth; `engineValue` is the engine's answer. Asked for wherever the engine prices an item: a
    // tooltip, a shop's offer, the cost of identifying or repairing it, what a corpse or chest holds.
    virtual int itemValue(const InventoryItem &item, int engineValue) = 0;
    // A character's total in a skill; `engineValue` is the engine's answer.
    virtual int playerSkillTotal(ModPlayerSkillTotal kind, const Character &character, int engineValue) = 0;
    // Whether the party can identify (or repair) an item of this kind (the original's CanIdentifyItem and
    // CanRepairItem); `engineAnswer` is the engine's.
    virtual bool canHandleItem(bool repair, uint32_t itemId, bool engineAnswer) = 0;
    // The player is about to take an action (the original's Action event, which comes before the game acts).
    virtual ModPlayerActionResult playerAction(int action, int param, int param2) = 0;
    // A dead monster's loot is about to be rolled (the original's PickCorpse, which there comes when the body is
    // searched; here the loot is rolled when the monster dies). `loot` comes in as the engine's.
    virtual void pickCorpse(size_t actorIndex, ModCorpseLoot &loot) = 0;
    // A random item is about to be made: the mod may change its treasure level (the original's GenerateItem).
    virtual void generateItem(ModItemGeneration &generation) = 0;
    // A random item has been made: the mod may rewrite it, its kind included (the original's ItemGenerated).
    virtual void itemGenerated(InventoryItem &item, const ModItemGeneration &generation) = 0;
    // The texts of the item's information box; `texts` come in as the engine's and go out as the mod leaves them.
    // The box is built on every frame it shows, so the answer for the same item and texts is reused for a moment
    // unless `fresh` asks the scripts again.
    virtual void itemTooltip(const InventoryItem &item, ModItemTooltip &texts, bool fresh) = 0;

    // The part of a statistic that comes from the character's skills: Bodybuilding in hit points, the armor
    // skills in armor class, the weapon skills in attack and damage. `statId` is the original engine's statistic
    // number (7 HP, 8 SP, 9 armor class, 25 melee attack, 26 melee damage, 29 ranged attack, 30 ranged damage).
    virtual int skillStatBonus(const Character &character, int statId, int engineBonus) = 0;
    // A teacher is asked for a skill mastery; see ModTeachSkillMasteryEvent.
    virtual void canTeachSkillMastery(ModTeachSkillMasteryEvent &event) = 0;
    // The active member has trained a level: how long the training takes, in game minutes (the original's
    // CalcTrainingTime). `engineMinutes` is the engine's.
    virtual float trainingMinutes(float engineMinutes) = 0;
    // The text of the stats page's information box for the row `stat` (the original's numbering: 0 to 6 the
    // statistics, 7 hit points ... 18 ranged damage, 19 to 24 the resistances), as the mod leaves it (the original's
    // BuildStatInformationBox). `text` comes in as the engine's.
    virtual void statInformationBox(int stat, std::string &text) = 0;
    // A character is about to learn a spell from a book: the mastery of its school it takes (the original's
    // CanLearnSpell, NeedMastery: 1 to 4, above 4 not at all). `engineMastery` is the engine's.
    virtual int canLearnSpell(const Character &character, uint32_t spellId, int engineMastery) = 0;
    // The character may put on the held item, item `itemId`, as far as the game goes (the original's CanWearItem):
    // whether the mod lets it.
    virtual bool canWearItem(const Character &character, uint32_t itemId) = 0;
    // The party asks a town hall about its bounty hunt: the mod's text, if it runs the hunts itself (MAW's
    // BountyHunt.lua: the hunt, the monster's summoning and the reward are its own).
    virtual bool townHallBounty(std::string &text) = 0;
    // The monster's information box is being shown (the original's BuildMonsterInformationBox).
    virtual void monsterInformationBox(ModMonsterInfoBox &box) = 0;
    // The size monster `actorIndex` is drawn at, as a factor of the game's (the original's MonsterSpriteScale).
    virtual float monsterSpriteScale(size_t actorIndex) = 0;
    // The held item is used on the party member in `memberIndex` (the original's UseMouseItem; Mouse.Item is the
    // item): whether the game may go on with its own use of it.
    virtual bool useMouseItem(size_t memberIndex) = 0;
    // The held item, potion `potionId` of the potion settings table with power `power`, is used on the item the
    // event item is (the original's evt.PotionEffects[potionId](false, item, power)): whether the mod has an effect
    // for that potion, which then took the use over.
    virtual bool potionEffect(uint32_t potionId, int power) = 0;
    // Map events a mod adds to or takes over from the map's own (the original's evt.map[id] handlers and
    // Game.MapEvtLines:RemoveEvent(id)): whether the mod has anything for event `eventId` of the current map.
    virtual bool hasMapEvent(uint16_t eventId) = 0;
    // Runs the mod's handlers for the event, within it: ModMapEvent flags.
    virtual int mapEvent(uint16_t eventId) = 0;
    // The mod's hint for the event (the original's evt.hint[id]), shown when the player points at what it is on.
    virtual bool mapEventHint(uint16_t eventId, std::string &hint) = 0;
    // Whether the mod removed the map's own lines for the event (their timer goes with them).
    virtual bool mapEventRemoved(uint16_t eventId) = 0;
};

// mapEvent's answer: the mod ran handlers, and the map's own lines for the event are removed (the mod's replace
// them).
enum ModMapEvent : int
{
    ModMapEventRan = 1,
    ModMapEventReplaces = 2,
};

IModHooks *activeModHooks();
void setActiveModHooks(IModHooks *pModHooks);

// Runs the mod's event over the texts of an item's information box; without a mod runtime they stay as they are.
void modItemTooltip(const InventoryItem &item, ModItemTooltip &texts, bool fresh = false);

// The mod's word on a character's skill total, and on whether an item can be identified or repaired; the engine's
// without a mod runtime.
int modPlayerSkillTotal(ModPlayerSkillTotal kind, const Character &character, int engineValue);
bool modCanHandleItem(bool repair, uint32_t itemId, bool engineAnswer);

// Runs the mod's CanTeachSkillMastery event; the event stays the engine's without a mod runtime.
void modCanTeachSkillMastery(ModTeachSkillMasteryEvent &event);

// Runs the mod's CalcTrainingTime event and returns the training's game minutes, `engineMinutes` without a mod.
float modTrainingMinutes(float engineMinutes);

// Runs the mod's BuildStatInformationBox event over `text`; it stays the engine's without a mod.
void modStatInformationBox(int stat, std::string &text);

// Runs the mod's UseMouseItem event for the held item used on a party member: whether the game may use it its own
// way (true without a mod).
bool modUseMouseItem(size_t memberIndex);
// Runs the mod's potion effect for the held potion used on `target`: whether the mod took the use over (false
// without a mod, or when it has no effect for that potion).
bool modPotionEffect(uint32_t potionId, int power, InventoryItem &target);

// A text a mod puts on one of the original's screens (CustomUI.CreateText), at the original's 640x480 position.
struct ModHudText
{
    std::string text;
    float x = 0.0f;
    float y = 0.0f;
    int screen = 0;
    bool alignLeft = true;
    float width = 0.0f;
    bool visible = true;
};

// The mods' screen texts by their number; the game view draws those of Screen 0.
const std::map<int, ModHudText> &modHudTexts();
void setModHudText(int id, const ModHudText &text);

// Native rules a mod sets otherwise (from its scripts: modrt.setRule(name, value)), for the Merge features a mod
// leaves out or changes in its own copies of Merge's scripts. An empty value is the engine's own rule.
//   meditation_regeneration  "off": no spell points from Meditation every five minutes (MAW regains them itself)
//   unarmed_dodge            "off": no Grandmaster Unarmed dodge (Merge's repair of it, which MAW leaves out)
//   slow_resistance          "off": Slow always takes hold (no resistance roll)
//   follower_skill_bonuses   "largest": followers' bonuses to a skill do not add up; the largest counts
//   catalyst_combination     "alchemy_capped": two catalysts make the larger of target + held / 2 and
//                            target / 2 + held, at most Alchemy level x mastery / 2, never less than either
const std::string &modRule(const std::string &name);
void setModRule(const std::string &name, const std::string &value);

// The mod's hint for event `eventId` of the current map (the original's evt.hint[id]); none without a mod.
std::optional<std::string> modMapEventHint(uint16_t eventId);
// Whether a mod removed the current map's own lines for event `eventId`; false without a mod.
bool modMapEventRemoved(uint16_t eventId);

// The factor the mod draws monster `actorIndex` at (1 without a mod). The mod is asked about a monster once a second;
// the map's monsters are drawn every frame.
float modMonsterSpriteScale(size_t actorIndex);

// The mod's answer to a town hall's bounty hunt topic, when the mod runs the hunts itself; none without a mod.
std::optional<std::string> modTownHallBounty();

// Runs the mod's BuildMonsterInformationBox event over the box's texts; they stay the engine's without a mod.
void modMonsterInformationBox(ModMonsterInfoBox &box);

// Runs the mod's CanWearItem event for the held item the game would let the character put on: whether the mod lets
// it too (true without a mod).
bool modCanWearItem(const Character &character, uint32_t itemId);

// Runs the mod's CanLearnSpell event and returns the mastery the spell takes, `engineMastery` without a mod.
int modCanLearnSpell(const Character &character, uint32_t spellId, int engineMastery);

// A skill's number in the Merge's numbering (the skills a mod adds included), for the events that name skills by it.
std::optional<int> modMergeSkillId(const std::string &skillName);

// Runs the mod's Action event; nothing handled and nothing asked for without a mod runtime.
ModPlayerActionResult modPlayerAction(int action, int param = 0, int param2 = 0);
// The same, with Mouse.Item the item the player holds.
ModPlayerActionResult modPlayerActionWithHeldItem(int action, InventoryItem &heldItem, int param = 0, int param2 = 0);

// Runs the mod's event for a dead monster's loot and returns what to roll, which is `engineLoot` without a mod
// runtime.
MonsterTable::LootPrototype modCorpseLoot(size_t actorIndex, const MonsterTable::LootPrototype &engineLoot);

// The original's interface texts (global.txt) by number, as a mod has rewritten them (Game.GlobalTxt); nullptr where
// no mod has. MAW shows its own numbers on the stats page by rewriting the page's labels. An empty text gives the
// game its own back.
const std::string *modGlobalText(int index);
void setModGlobalText(int index, const std::string &text);

// The original marks coloured text with a form feed and five digits ("\f00000" ends a colour). The engine's text
// drawing does not know the marks, so they are taken out before a mod's text is shown.
std::string stripMm8ColorCodes(const std::string &text);

// const.Damage: the original engine's number for a kind of damage, which is how mods name it.
int mm8DamageKind(CombatDamageType damageType);

// const.AIState: the original engine's numbers for what a map monster is doing. Scripts mostly ask whether a
// monster is dead (5), gone (11) or hidden (19).
namespace Mm8AiState
{
constexpr int Stand = 0;
constexpr int Active = 1;
constexpr int MeleeAttack = 2;
constexpr int RangedAttack = 3;
constexpr int Dying = 4;
constexpr int Dead = 5;
constexpr int Pursue = 6;
constexpr int Flee = 7;
constexpr int Stunned = 8;
constexpr int Removed = 11;
constexpr int Paralyzed = 15;
constexpr int Invisible = 19;
}

// What an actor is doing, for mm8AiState. `motion` is the engine's motion state as a number: both world runtimes
// number theirs alike (standing 0, wandering, pursuing, fleeing, stunned, attacking, dying, dead 7).
struct ModActorActivity
{
    int motion = 0;
    bool dead = false;
    bool invisible = false;
    bool rangedAttack = false;
    bool paralyzed = false;
};

// A dead actor that is also hidden is the original's "removed": no body, nothing to interact with.
int mm8AiState(const ModActorActivity &activity);

// Runs the mod's event for damage to a party member and returns the damage to apply, which is
// event.resolvedDamage when no mod runtime is present.
int modDamageToPlayer(const ModDamageToPlayerEvent &event);

// The experience to grant for a kill: the mod's answer, or `engineExperience` when no mod runtime is present.
uint32_t modKillExperience(size_t actorIndex, uint32_t engineExperience);

// The party's missile or spell hits a map actor: runs the mod's damage event and returns the damage to apply, which
// is `resolvedDamage` when no mod runtime is present. `spellId` is 0 for an arrow or a blaster shot.
int modObjectDamageToMonster(
    size_t actorIndex,
    std::optional<size_t> partyMemberIndex,
    int rawDamage,
    int resolvedDamage,
    CombatDamageType damageType,
    uint32_t spellId,
    uint32_t skillLevel,
    uint32_t skillMastery,
    float objectX,
    float objectY,
    float objectZ);
}
