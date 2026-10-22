#include "game/gameplay/ActorBehaviorRuntime.h"

#include "game/audio/SoundRef.h"
#include "game/events/EventRuntime.h"
#include "game/gameplay/ActorBehaviorCatalog.h"
#include "game/gameplay/GameMechanics.h"
#include "game/gameplay/MonsterSpellSupport.h"
#include "game/tables/SpellTable.h"

extern "C"
{
#include <lauxlib.h>
#include <lualib.h>
}

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

namespace OpenYAMM::Game
{
struct ActorBehaviorRuntime::LuaCallContext
{
    ActorBehaviorInstanceState *pState = nullptr;
    const ActorBehaviorActorFacts *pActor = nullptr;
    ActorBehaviorFrameResult *pResult = nullptr;
    GameplayWorldPoint partyPosition;
};

namespace
{
constexpr const char *RuntimeRegistryKey = "openyamm.actorBehaviorRuntime";
constexpr const char *SelfMetatableName = "openyamm.actorBehaviorSelf";
constexpr const char *SelfActorKey = "__actor";
constexpr float DefaultThinkIntervalSeconds = 0.5f;
constexpr float DefaultPerformSeconds = 1.5f;
constexpr int MaxSerializedDepth = 16;

enum class SerializedTag : uint8_t
{
    False = 1,
    True = 2,
    Integer = 3,
    Number = 4,
    String = 5,
    Table = 6,
};

uint32_t hashText(const std::string &text)
{
    uint32_t hash = 2166136261u;

    for (const char character : text)
    {
        hash = (hash ^ static_cast<uint8_t>(character)) * 16777619u;
    }

    return hash;
}

uint32_t nextRandom(uint32_t &state)
{
    state = state * 1664525u + 1013904223u;
    return state;
}

void appendBytes(std::vector<uint8_t> &out, const void *pData, size_t size)
{
    const uint8_t *pBytes = static_cast<const uint8_t *>(pData);
    out.insert(out.end(), pBytes, pBytes + size);
}

template <typename Value>
void appendValue(std::vector<uint8_t> &out, Value value)
{
    appendBytes(out, &value, sizeof(value));
}

template <typename Value>
bool readSerializedValue(const std::vector<uint8_t> &data, size_t &offset, Value &value)
{
    if (offset + sizeof(value) > data.size())
    {
        return false;
    }

    std::memcpy(&value, data.data() + offset, sizeof(value));
    offset += sizeof(value);
    return true;
}

// Script state holds plain data only: booleans, numbers, strings and nested tables of them.
bool serializeLuaValue(lua_State *pLuaState, int index, std::vector<uint8_t> &out, int depth, std::string &error)
{
    index = lua_absindex(pLuaState, index);

    switch (lua_type(pLuaState, index))
    {
        case LUA_TBOOLEAN:
            appendValue(out, lua_toboolean(pLuaState, index) ? SerializedTag::True : SerializedTag::False);
            return true;

        case LUA_TNUMBER:
            if (lua_isinteger(pLuaState, index))
            {
                appendValue(out, SerializedTag::Integer);
                appendValue(out, static_cast<int64_t>(lua_tointeger(pLuaState, index)));
            }
            else
            {
                appendValue(out, SerializedTag::Number);
                appendValue(out, static_cast<double>(lua_tonumber(pLuaState, index)));
            }
            return true;

        case LUA_TSTRING:
        {
            size_t length = 0;
            const char *pText = lua_tolstring(pLuaState, index, &length);
            appendValue(out, SerializedTag::String);
            appendValue(out, static_cast<uint32_t>(length));
            appendBytes(out, pText, length);
            return true;
        }

        case LUA_TTABLE:
        {
            if (depth >= MaxSerializedDepth)
            {
                error = "script state nests deeper than " + std::to_string(MaxSerializedDepth) + " tables";
                return false;
            }

            appendValue(out, SerializedTag::Table);
            const size_t countOffset = out.size();
            appendValue(out, uint32_t(0));
            uint32_t count = 0;
            lua_pushnil(pLuaState);

            while (lua_next(pLuaState, index) != 0)
            {
                const int keyType = lua_type(pLuaState, -2);

                if (keyType != LUA_TSTRING && keyType != LUA_TNUMBER && keyType != LUA_TBOOLEAN)
                {
                    lua_pop(pLuaState, 2);
                    error = "script state keys must be strings, numbers or booleans";
                    return false;
                }

                if (!serializeLuaValue(pLuaState, -2, out, depth + 1, error)
                    || !serializeLuaValue(pLuaState, -1, out, depth + 1, error))
                {
                    lua_pop(pLuaState, 2);
                    return false;
                }

                ++count;
                lua_pop(pLuaState, 1);
            }

            std::memcpy(out.data() + countOffset, &count, sizeof(count));
            return true;
        }

        default:
            error = std::string("script state cannot hold a ") + luaL_typename(pLuaState, index);
            return false;
    }
}

bool deserializeLuaValue(lua_State *pLuaState, const std::vector<uint8_t> &data, size_t &offset, int depth)
{
    SerializedTag tag = SerializedTag::False;

    if (depth > MaxSerializedDepth || !readSerializedValue(data, offset, tag))
    {
        return false;
    }

    switch (tag)
    {
        case SerializedTag::False:
        case SerializedTag::True:
            lua_pushboolean(pLuaState, tag == SerializedTag::True);
            return true;

        case SerializedTag::Integer:
        {
            int64_t value = 0;

            if (!readSerializedValue(data, offset, value))
            {
                return false;
            }

            lua_pushinteger(pLuaState, static_cast<lua_Integer>(value));
            return true;
        }

        case SerializedTag::Number:
        {
            double value = 0.0;

            if (!readSerializedValue(data, offset, value))
            {
                return false;
            }

            lua_pushnumber(pLuaState, static_cast<lua_Number>(value));
            return true;
        }

        case SerializedTag::String:
        {
            uint32_t length = 0;

            if (!readSerializedValue(data, offset, length) || offset + length > data.size())
            {
                return false;
            }

            lua_pushlstring(pLuaState, reinterpret_cast<const char *>(data.data() + offset), length);
            offset += length;
            return true;
        }

        case SerializedTag::Table:
        {
            uint32_t count = 0;

            if (!readSerializedValue(data, offset, count))
            {
                return false;
            }

            lua_createtable(pLuaState, 0, static_cast<int>(std::min<uint32_t>(count, 64)));

            for (uint32_t pairIndex = 0; pairIndex < count; ++pairIndex)
            {
                if (!deserializeLuaValue(pLuaState, data, offset, depth + 1))
                {
                    lua_pop(pLuaState, 1);
                    return false;
                }

                if (!deserializeLuaValue(pLuaState, data, offset, depth + 1))
                {
                    lua_pop(pLuaState, 2);
                    return false;
                }

                lua_rawset(pLuaState, -3);
            }

            return true;
        }
    }

    return false;
}

std::optional<ActorAiAnimationState> animationStateFromName(const std::string &name)
{
    if (name == "standing")
    {
        return ActorAiAnimationState::Standing;
    }

    if (name == "walking")
    {
        return ActorAiAnimationState::Walking;
    }

    if (name == "attack_melee")
    {
        return ActorAiAnimationState::AttackMelee;
    }

    if (name == "attack_ranged")
    {
        return ActorAiAnimationState::AttackRanged;
    }

    if (name == "hit")
    {
        return ActorAiAnimationState::GotHit;
    }

    if (name == "fidget")
    {
        return ActorAiAnimationState::Bored;
    }

    return std::nullopt;
}

std::optional<SkillMastery> masteryFromName(const std::string &name)
{
    if (name == "normal")
    {
        return SkillMastery::Normal;
    }

    if (name == "expert")
    {
        return SkillMastery::Expert;
    }

    if (name == "master")
    {
        return SkillMastery::Master;
    }

    if (name == "grandmaster")
    {
        return SkillMastery::Grandmaster;
    }

    return std::nullopt;
}

std::optional<GameplayActorAiType> aiTypeFromName(const std::string &name)
{
    if (name == "suicide")
    {
        return GameplayActorAiType::Suicide;
    }

    if (name == "wimp")
    {
        return GameplayActorAiType::Wimp;
    }

    if (name == "normal")
    {
        return GameplayActorAiType::Normal;
    }

    if (name == "aggressive")
    {
        return GameplayActorAiType::Aggressive;
    }

    return std::nullopt;
}

bool actorIsBusy(ActorAiAnimationState animationState)
{
    return animationState == ActorAiAnimationState::AttackMelee
        || animationState == ActorAiAnimationState::AttackRanged
        || animationState == ActorAiAnimationState::GotHit
        || animationState == ActorAiAnimationState::Dying
        || animationState == ActorAiAnimationState::Dead;
}

GameplayWorldPoint actorCenter(const ActorBehaviorActorFacts &actor)
{
    return {actor.position.x, actor.position.y, actor.position.z + actor.height * 0.5f};
}

ActorBehaviorRuntime &runtimeFromLua(lua_State *pLuaState)
{
    lua_getfield(pLuaState, LUA_REGISTRYINDEX, RuntimeRegistryKey);
    ActorBehaviorRuntime *pRuntime = static_cast<ActorBehaviorRuntime *>(lua_touserdata(pLuaState, -1));
    lua_pop(pLuaState, 1);
    return *pRuntime;
}

// Every self method acts on the actor whose handler is running; a self kept from another call raises an error.
ActorBehaviorRuntime::LuaCallContext &callContext(lua_State *pLuaState)
{
    ActorBehaviorRuntime::LuaCallContext *pContext = runtimeFromLua(pLuaState).activeCall();
    luaL_checktype(pLuaState, 1, LUA_TTABLE);
    lua_getfield(pLuaState, 1, SelfActorKey);
    const lua_Integer actorIndex = lua_tointeger(pLuaState, -1);
    lua_pop(pLuaState, 1);

    if (pContext == nullptr || static_cast<size_t>(actorIndex) != pContext->pState->actorIndex)
    {
        luaL_error(pLuaState, "actor behaviour self used outside its own handler");
    }

    return *pContext;
}

std::string optionalStringField(lua_State *pLuaState, int tableIndex, const char *pName)
{
    lua_getfield(pLuaState, tableIndex, pName);
    std::string value = lua_isstring(pLuaState, -1) ? lua_tostring(pLuaState, -1) : "";
    lua_pop(pLuaState, 1);
    return value;
}

std::optional<double> optionalNumberField(lua_State *pLuaState, int tableIndex, const char *pName)
{
    lua_getfield(pLuaState, tableIndex, pName);
    std::optional<double> value;

    if (lua_isnumber(pLuaState, -1))
    {
        value = lua_tonumber(pLuaState, -1);
    }

    lua_pop(pLuaState, 1);
    return value;
}

ActorBehaviorSpell readSpellOptions(lua_State *pLuaState, int tableIndex, const SpellTable *pSpellTable)
{
    luaL_checktype(pLuaState, tableIndex, LUA_TTABLE);
    ActorBehaviorSpell spell = {};
    lua_getfield(pLuaState, tableIndex, "spell");

    if (lua_isinteger(pLuaState, -1))
    {
        spell.spellId = static_cast<uint32_t>(lua_tointeger(pLuaState, -1));
    }
    else if (lua_isstring(pLuaState, -1) && pSpellTable != nullptr)
    {
        if (const SpellEntry *pEntry = pSpellTable->findByName(lua_tostring(pLuaState, -1)))
        {
            spell.spellId = static_cast<uint32_t>(pEntry->id);
        }
    }

    lua_pop(pLuaState, 1);

    if (spell.spellId == 0)
    {
        luaL_error(pLuaState, "unknown spell");
    }

    if (pSpellTable != nullptr)
    {
        const SpellEntry *pEntry = pSpellTable->findById(static_cast<int>(spell.spellId));

        if (pEntry == nullptr || !isMonsterProjectileSpellName(pEntry->name))
        {
            luaL_error(pLuaState, "actor behaviour spells must be monster projectile spells (spell %d)",
                static_cast<int>(spell.spellId));
        }
    }

    spell.skillLevel = static_cast<uint32_t>(std::max(1.0, optionalNumberField(pLuaState, tableIndex, "skill")
        .value_or(5.0)));
    const std::string masteryName = optionalStringField(pLuaState, tableIndex, "mastery");
    const std::optional<SkillMastery> mastery = masteryFromName(masteryName.empty() ? "expert" : masteryName);

    if (!mastery)
    {
        luaL_error(pLuaState, "unknown spell mastery: %s", masteryName.c_str());
    }

    spell.skillMastery = *mastery;
    spell.useChance = std::clamp(
        static_cast<int>(optionalNumberField(pLuaState, tableIndex, "chance").value_or(100.0)), 0, 100);
    return spell;
}

int luaSelfHp(lua_State *pLuaState)
{
    lua_pushinteger(pLuaState, callContext(pLuaState).pActor->currentHp);
    return 1;
}

int luaSelfMaxHp(lua_State *pLuaState)
{
    lua_pushinteger(pLuaState, callContext(pLuaState).pActor->maxHp);
    return 1;
}

int luaSelfHpFraction(lua_State *pLuaState)
{
    const ActorBehaviorActorFacts &actor = *callContext(pLuaState).pActor;
    lua_pushnumber(pLuaState, actor.maxHp > 0 ? double(actor.currentHp) / double(actor.maxHp) : 0.0);
    return 1;
}

int luaSelfPosition(lua_State *pLuaState)
{
    const ActorBehaviorActorFacts &actor = *callContext(pLuaState).pActor;
    lua_pushnumber(pLuaState, actor.position.x);
    lua_pushnumber(pLuaState, actor.position.y);
    lua_pushnumber(pLuaState, actor.position.z);
    return 3;
}

int luaSelfDistanceToParty(lua_State *pLuaState)
{
    const ActorBehaviorRuntime::LuaCallContext &context = callContext(pLuaState);
    const float dx = context.partyPosition.x - context.pActor->position.x;
    const float dy = context.partyPosition.y - context.pActor->position.y;
    const float dz = context.partyPosition.z - context.pActor->position.z;
    lua_pushnumber(pLuaState, std::sqrt(dx * dx + dy * dy + dz * dz));
    return 1;
}

int luaSelfIsHostile(lua_State *pLuaState)
{
    lua_pushboolean(pLuaState, callContext(pLuaState).pActor->hostileToParty);
    return 1;
}

int luaSelfMonsterId(lua_State *pLuaState)
{
    lua_pushinteger(pLuaState, callContext(pLuaState).pActor->monsterId);
    return 1;
}

int luaSelfAge(lua_State *pLuaState)
{
    lua_pushnumber(pLuaState, callContext(pLuaState).pState->ageSeconds);
    return 1;
}

// random() -> [0, 1); random(n) -> 1..n; random(a, b) -> a..b. Seeded per actor and saved with the map.
int luaSelfRandom(lua_State *pLuaState)
{
    ActorBehaviorInstanceState &state = *callContext(pLuaState).pState;
    const uint32_t value = nextRandom(state.randomState) >> 8;
    const int argumentCount = lua_gettop(pLuaState);

    if (argumentCount <= 1)
    {
        lua_pushnumber(pLuaState, double(value) / double(1u << 24));
        return 1;
    }

    const lua_Integer low = argumentCount >= 3 ? luaL_checkinteger(pLuaState, 2) : 1;
    const lua_Integer high = luaL_checkinteger(pLuaState, argumentCount >= 3 ? 3 : 2);
    luaL_argcheck(pLuaState, high >= low, argumentCount >= 3 ? 3 : 2, "empty random interval");
    lua_pushinteger(pLuaState, low + lua_Integer(value % uint32_t(high - low + 1)));
    return 1;
}

int luaSelfSay(lua_State *pLuaState)
{
    callContext(pLuaState).pResult->statusMessages.push_back(luaL_checkstring(pLuaState, 2));
    return 0;
}

int luaSelfPlaySound(lua_State *pLuaState)
{
    const ActorBehaviorRuntime::LuaCallContext &context = callContext(pLuaState);
    context.pResult->sounds.push_back({luaL_checkstring(pLuaState, 2), actorCenter(*context.pActor)});
    return 0;
}

// effect(id [, scale [, heightFraction]]): heightFraction 0 is the feet, 1 the head (default 0.5).
int luaSelfEffect(lua_State *pLuaState)
{
    const ActorBehaviorRuntime::LuaCallContext &context = callContext(pLuaState);
    const float scale = static_cast<float>(luaL_optnumber(pLuaState, 3, 1.0));
    const float heightFraction = static_cast<float>(luaL_optnumber(pLuaState, 4, 0.5));
    luaL_argcheck(pLuaState, std::isfinite(scale) && scale > 0.0f, 3, "effect scale must be positive");
    GameplayWorldPoint position = context.pActor->position;
    position.z += context.pActor->height * heightFraction;
    context.pResult->effects.push_back({luaL_checkstring(pLuaState, 2), position, scale});
    return 0;
}

// perform{clip=, seconds=, fallback=, sound=, effect=}: holds the native AI while the clip plays.
int luaSelfPerform(lua_State *pLuaState)
{
    const ActorBehaviorRuntime::LuaCallContext &context = callContext(pLuaState);
    luaL_checktype(pLuaState, 2, LUA_TTABLE);
    ActorBehaviorPerform perform = {};
    perform.clip = optionalStringField(pLuaState, 2, "clip");
    perform.durationSeconds = static_cast<float>(
        optionalNumberField(pLuaState, 2, "seconds").value_or(DefaultPerformSeconds));
    const std::string fallbackName = optionalStringField(pLuaState, 2, "fallback");
    const std::optional<ActorAiAnimationState> fallback =
        animationStateFromName(fallbackName.empty() ? "attack_ranged" : fallbackName);
    luaL_argcheck(pLuaState, fallback.has_value(), 2, "unknown fallback animation");
    luaL_argcheck(pLuaState, std::isfinite(perform.durationSeconds) && perform.durationSeconds > 0.0f, 2,
        "perform seconds must be positive");
    perform.fallbackAnimation = *fallback;
    context.pState->perform = perform;

    const std::string sound = optionalStringField(pLuaState, 2, "sound");
    const std::string effect = optionalStringField(pLuaState, 2, "effect");

    if (!sound.empty())
    {
        context.pResult->sounds.push_back({sound, actorCenter(*context.pActor)});
    }

    if (!effect.empty())
    {
        context.pResult->effects.push_back({effect, actorCenter(*context.pActor), 1.0f});
    }

    return 0;
}

int luaSelfIsPerforming(lua_State *pLuaState)
{
    lua_pushboolean(pLuaState, callContext(pLuaState).pState->perform.has_value());
    return 1;
}

// visual = false draws no dome; visual = {color = {r, g, b}, intensity =, rim =, noise =} styles it.
void readWardVisual(lua_State *pLuaState, int tableIndex, ActorBehaviorWardVisual &visual)
{
    lua_getfield(pLuaState, tableIndex, "visual");

    if (lua_isboolean(pLuaState, -1))
    {
        visual.dome = lua_toboolean(pLuaState, -1) != 0;
    }
    else if (lua_istable(pLuaState, -1))
    {
        const int visualIndex = lua_gettop(pLuaState);
        lua_getfield(pLuaState, visualIndex, "color");

        if (lua_istable(pLuaState, -1))
        {
            for (lua_Integer channel = 0; channel < 3; ++channel)
            {
                lua_geti(pLuaState, -1, channel + 1);
                visual.color[channel] = static_cast<uint8_t>(std::clamp<lua_Integer>(
                    lua_isinteger(pLuaState, -1) ? lua_tointeger(pLuaState, -1) : visual.color[channel], 0, 255));
                lua_pop(pLuaState, 1);
            }
        }

        lua_pop(pLuaState, 1);
        visual.intensity = static_cast<float>(std::clamp(
            optionalNumberField(pLuaState, visualIndex, "intensity").value_or(visual.intensity), 0.0, 4.0));
        visual.rimPower = static_cast<float>(std::clamp(
            optionalNumberField(pLuaState, visualIndex, "rim").value_or(visual.rimPower), 0.5, 12.0));
        visual.noise = static_cast<float>(std::clamp(
            optionalNumberField(pLuaState, visualIndex, "noise").value_or(visual.noise), 0.0, 1.0));
    }
    else if (!lua_isnil(pLuaState, -1))
    {
        luaL_error(pLuaState, "ward visual must be false or a table");
    }

    lua_pop(pLuaState, 1);
}

// addWard{id=, radius=, seconds=, effect=, effectScale=, effectInterval=, visual=}
int luaSelfAddWard(lua_State *pLuaState)
{
    const ActorBehaviorRuntime::LuaCallContext &context = callContext(pLuaState);
    luaL_checktype(pLuaState, 2, LUA_TTABLE);
    ActorBehaviorWard ward = {};
    ward.id = optionalStringField(pLuaState, 2, "id");
    ward.radius = static_cast<float>(optionalNumberField(pLuaState, 2, "radius").value_or(0.0));
    ward.remainingSeconds = static_cast<float>(optionalNumberField(pLuaState, 2, "seconds").value_or(-1.0));
    ward.effectId = optionalStringField(pLuaState, 2, "effect");
    ward.effectScale = static_cast<float>(optionalNumberField(pLuaState, 2, "effectScale").value_or(1.0));
    ward.effectIntervalSeconds =
        static_cast<float>(optionalNumberField(pLuaState, 2, "effectInterval").value_or(1.0));
    luaL_argcheck(pLuaState, !ward.id.empty(), 2, "ward id required");
    luaL_argcheck(pLuaState, std::isfinite(ward.radius) && ward.radius > 0.0f, 2, "ward radius must be positive");
    luaL_argcheck(pLuaState, std::isfinite(ward.effectScale) && ward.effectScale > 0.0f, 2,
        "ward effectScale must be positive");
    luaL_argcheck(pLuaState, std::isfinite(ward.effectIntervalSeconds) && ward.effectIntervalSeconds > 0.05f, 2,
        "ward effectInterval must exceed 0.05 seconds");
    readWardVisual(pLuaState, 2, ward.visual);

    std::vector<ActorBehaviorWard> &wards = context.pState->wards;
    wards.erase(std::remove_if(wards.begin(), wards.end(),
        [&](const ActorBehaviorWard &existing) { return existing.id == ward.id; }), wards.end());
    wards.push_back(std::move(ward));
    return 0;
}

int luaSelfRemoveWard(lua_State *pLuaState)
{
    std::vector<ActorBehaviorWard> &wards = callContext(pLuaState).pState->wards;
    const std::string id = luaL_checkstring(pLuaState, 2);
    wards.erase(std::remove_if(wards.begin(), wards.end(),
        [&](const ActorBehaviorWard &ward) { return ward.id == id; }), wards.end());
    return 0;
}

int luaSelfHasWard(lua_State *pLuaState)
{
    const std::vector<ActorBehaviorWard> &wards = callContext(pLuaState).pState->wards;
    const std::string id = luaL_checkstring(pLuaState, 2);
    lua_pushboolean(pLuaState, std::any_of(wards.begin(), wards.end(),
        [&](const ActorBehaviorWard &ward) { return ward.id == id; }));
    return 1;
}

int luaSelfStartTimer(lua_State *pLuaState)
{
    std::vector<ActorBehaviorTimer> &timers = callContext(pLuaState).pState->timers;
    const std::string name = luaL_checkstring(pLuaState, 2);
    const float seconds = static_cast<float>(luaL_checknumber(pLuaState, 3));
    luaL_argcheck(pLuaState, std::isfinite(seconds) && seconds >= 0.0f, 3, "timer seconds must not be negative");
    timers.erase(std::remove_if(timers.begin(), timers.end(),
        [&](const ActorBehaviorTimer &timer) { return timer.name == name; }), timers.end());
    timers.push_back({name, seconds});
    return 0;
}

int luaSelfStopTimer(lua_State *pLuaState)
{
    std::vector<ActorBehaviorTimer> &timers = callContext(pLuaState).pState->timers;
    const std::string name = luaL_checkstring(pLuaState, 2);
    timers.erase(std::remove_if(timers.begin(), timers.end(),
        [&](const ActorBehaviorTimer &timer) { return timer.name == name; }), timers.end());
    return 0;
}

int luaSelfHasTimer(lua_State *pLuaState)
{
    const std::vector<ActorBehaviorTimer> &timers = callContext(pLuaState).pState->timers;
    const std::string name = luaL_checkstring(pLuaState, 2);
    lua_pushboolean(pLuaState, std::any_of(timers.begin(), timers.end(),
        [&](const ActorBehaviorTimer &timer) { return timer.name == name; }));
    return 1;
}

// castNext{spell=, skill=, mastery=}: the next spell attack the native AI makes uses this spell.
int luaSelfCastNext(lua_State *pLuaState)
{
    ActorBehaviorRuntime::LuaCallContext &context = callContext(pLuaState);
    context.pState->nextCast = readSpellOptions(pLuaState, 2, runtimeFromLua(pLuaState).spellTable());
    context.pState->nextCast->useChance = 100;
    return 0;
}

// setSpell{spell=, skill=, mastery=, chance=}: replaces the monster's first spell until clearSpell().
int luaSelfSetSpell(lua_State *pLuaState)
{
    ActorBehaviorRuntime::LuaCallContext &context = callContext(pLuaState);
    context.pState->spellOverride = readSpellOptions(pLuaState, 2, runtimeFromLua(pLuaState).spellTable());
    return 0;
}

int luaSelfClearSpell(lua_State *pLuaState)
{
    callContext(pLuaState).pState->spellOverride.reset();
    return 0;
}

int luaSelfSetAiType(lua_State *pLuaState)
{
    ActorBehaviorInstanceState &state = *callContext(pLuaState).pState;

    if (lua_isnoneornil(pLuaState, 2))
    {
        state.aiTypeOverride.reset();
        return 0;
    }

    const std::string name = luaL_checkstring(pLuaState, 2);
    const std::optional<GameplayActorAiType> aiType = aiTypeFromName(name);
    luaL_argcheck(pLuaState, aiType.has_value(), 2, "unknown AI type");
    state.aiTypeOverride = aiType;
    return 0;
}

int luaSelfSetNoFlee(lua_State *pLuaState)
{
    callContext(pLuaState).pState->suppressFlee = lua_toboolean(pLuaState, 2) != 0;
    return 0;
}

// summon(monsterId, count [, radius]): hostile monsters around the actor.
int luaSelfSummon(lua_State *pLuaState)
{
    const ActorBehaviorRuntime::LuaCallContext &context = callContext(pLuaState);
    const lua_Integer monsterId = luaL_checkinteger(pLuaState, 2);
    const lua_Integer count = luaL_optinteger(pLuaState, 3, 1);
    const float radius = static_cast<float>(luaL_optnumber(pLuaState, 4, 256.0));
    luaL_argcheck(pLuaState, monsterId > 0 && monsterId <= 32767, 2, "invalid monster id");
    luaL_argcheck(pLuaState, count > 0 && count <= 16, 3, "summon count must be 1..16");

    for (lua_Integer index = 0; index < count; ++index)
    {
        const float angle = float(nextRandom(context.pState->randomState) >> 8) / float(1u << 24)
            * 6.2831853f;
        ActorBehaviorSummonRequest request = {};
        request.monsterId = static_cast<int16_t>(monsterId);
        request.count = 1;
        request.position = {
            context.pActor->position.x + std::cos(angle) * radius,
            context.pActor->position.y + std::sin(angle) * radius,
            context.pActor->position.z};
        context.pResult->summons.push_back(request);
    }

    return 0;
}

const luaL_Reg SelfMethods[] = {
    {"hp", luaSelfHp},
    {"maxHp", luaSelfMaxHp},
    {"hpFraction", luaSelfHpFraction},
    {"position", luaSelfPosition},
    {"distanceToParty", luaSelfDistanceToParty},
    {"isHostile", luaSelfIsHostile},
    {"monsterId", luaSelfMonsterId},
    {"age", luaSelfAge},
    {"random", luaSelfRandom},
    {"say", luaSelfSay},
    {"playSound", luaSelfPlaySound},
    {"effect", luaSelfEffect},
    {"perform", luaSelfPerform},
    {"isPerforming", luaSelfIsPerforming},
    {"addWard", luaSelfAddWard},
    {"removeWard", luaSelfRemoveWard},
    {"hasWard", luaSelfHasWard},
    {"startTimer", luaSelfStartTimer},
    {"stopTimer", luaSelfStopTimer},
    {"hasTimer", luaSelfHasTimer},
    {"castNext", luaSelfCastNext},
    {"setSpell", luaSelfSetSpell},
    {"clearSpell", luaSelfClearSpell},
    {"setAiType", luaSelfSetAiType},
    {"setNoFlee", luaSelfSetNoFlee},
    {"summon", luaSelfSummon},
    {nullptr, nullptr},
};

void applySpellFacts(ActorStatsFacts &stats, const ActorBehaviorSpell &spell, const SpellTable *pSpellTable)
{
    stats.hasSpell1 = true;
    stats.spell1Id = spell.spellId;
    stats.spell1SkillLevel = spell.skillLevel;
    stats.spell1SkillMastery = spell.skillMastery;
    stats.spell1UseChance = spell.useChance;
    stats.spell1CastSupported = true;
    stats.spell1DamageType = GameMechanics::spellCombatDamageType(spell.spellId, pSpellTable);

    if (pSpellTable != nullptr)
    {
        if (const SpellEntry *pEntry = pSpellTable->findById(static_cast<int>(spell.spellId)))
        {
            stats.spell1Name = pEntry->name;
        }
    }
}
}

void ActorBehaviorRuntime::bind(const ActorBehaviorCatalog *pCatalog, const SpellTable *pSpellTable)
{
    if (pCatalog != m_pCatalog)
    {
        destroyLua();
    }

    m_pCatalog = pCatalog;
    m_pSpellTable = pSpellTable;
}

const SpellTable *ActorBehaviorRuntime::spellTable() const
{
    return m_pSpellTable;
}

void ActorBehaviorRuntime::reset()
{
    destroyLua();
    m_instances.clear();
    m_scannedActorCount = 0;
    m_projectileWards.clear();
    m_wardDisplays.clear();
    m_tacticAims.clear();
}

void ActorBehaviorRuntime::step(IGameplayWorldRuntime &world, float deltaSeconds)
{
    if (m_pCatalog == nullptr || deltaSeconds <= 0.0f)
    {
        return;
    }

    if (m_instances.empty() && !m_pCatalog->hasMonsterDefaults())
    {
        m_scannedActorCount = static_cast<uint32_t>(world.mapActorCount());
        return;
    }

    const ActorBehaviorFrameResult result = update(collectFrameFacts(world, deltaSeconds));
    applyFrameResult(world, result);
}

ActorBehaviorFrameFacts ActorBehaviorRuntime::collectFrameFacts(
    const IGameplayWorldRuntime &world,
    float deltaSeconds) const
{
    ActorBehaviorFrameFacts facts = {};
    facts.deltaSeconds = deltaSeconds;
    facts.partyPosition = {world.partyX(), world.partyY(), world.partyFootZ()};
    facts.actorCount = static_cast<uint32_t>(world.mapActorCount());

    std::vector<size_t> actorIndices;
    actorIndices.reserve(m_instances.size() + (facts.actorCount - std::min(facts.actorCount, m_scannedActorCount)));

    for (const auto &[actorIndex, record] : m_instances)
    {
        if (actorIndex < m_scannedActorCount)
        {
            actorIndices.push_back(actorIndex);
        }
    }

    for (size_t actorIndex = m_scannedActorCount; actorIndex < facts.actorCount; ++actorIndex)
    {
        actorIndices.push_back(actorIndex);
    }

    std::sort(actorIndices.begin(), actorIndices.end());
    facts.actors.reserve(actorIndices.size());

    for (const size_t actorIndex : actorIndices)
    {
        GameplayRuntimeActorState state = {};

        if (!world.actorRuntimeState(actorIndex, state))
        {
            continue;
        }

        ActorBehaviorActorFacts actor = {};
        actor.actorIndex = actorIndex;
        actor.actorId = state.actorId;
        actor.monsterId = state.monsterId;
        actor.currentHp = state.currentHp;
        actor.maxHp = state.maxHp;
        // Indoor actors report isDead only after the dying animation; health reaching zero is the death.
        actor.dead = state.isDead || (state.maxHp > 0 && state.currentHp <= 0);
        actor.hostileToParty = state.hostileToParty;
        actor.position = {state.preciseX, state.preciseY, state.preciseZ};
        actor.height = static_cast<float>(state.bodyHeight > 0 ? state.bodyHeight : state.height);
        actor.animationState = state.animationState;
        facts.actors.push_back(actor);
    }

    return facts;
}

ActorBehaviorFrameResult ActorBehaviorRuntime::update(const ActorBehaviorFrameFacts &facts)
{
    ActorBehaviorFrameResult result = {};

    if (m_pCatalog == nullptr)
    {
        return result;
    }

    std::vector<size_t> seenActors;
    seenActors.reserve(facts.actors.size());

    for (const ActorBehaviorActorFacts &actor : facts.actors)
    {
        seenActors.push_back(actor.actorIndex);
        auto iterator = m_instances.find(actor.actorIndex);

        if (iterator == m_instances.end() && actor.actorIndex >= m_scannedActorCount)
        {
            const std::string *pBehaviorId = m_pCatalog->defaultBehaviorForMonster(actor.monsterId);
            std::string error;

            if (pBehaviorId != nullptr && !actor.dead && attach(actor.actorIndex, actor.actorId, *pBehaviorId, error))
            {
                iterator = m_instances.find(actor.actorIndex);
            }
            else if (!error.empty())
            {
                std::cerr << "Actor behaviour attach failed: " << error << '\n';
            }
        }

        if (iterator == m_instances.end())
        {
            continue;
        }

        if (iterator->second.state.actorId != actor.actorId)
        {
            m_instances.erase(iterator);
            continue;
        }

        updateInstance(iterator->second, actor, facts, result);
    }

    // Actors that no longer report state lose their behaviour.
    std::sort(seenActors.begin(), seenActors.end());

    for (auto iterator = m_instances.begin(); iterator != m_instances.end();)
    {
        const bool reported = std::binary_search(seenActors.begin(), seenActors.end(), iterator->first);
        iterator = reported ? std::next(iterator) : m_instances.erase(iterator);
    }

    m_scannedActorCount = std::max(m_scannedActorCount, facts.actorCount);
    rebuildProjectileWards(facts);
    return result;
}

void ActorBehaviorRuntime::applyFrameResult(IGameplayWorldRuntime &world, const ActorBehaviorFrameResult &result)
{
    EventRuntimeState *pEventRuntimeState = world.eventRuntimeState();

    if (pEventRuntimeState != nullptr)
    {
        for (const ActorBehaviorSoundRequest &sound : result.sounds)
        {
            EventRuntimeState::PendingSound request = {};
            request.soundScope = SoundScope::World;
            request.soundName = sound.soundName;
            request.x = static_cast<int32_t>(std::lround(sound.position.x));
            request.y = static_cast<int32_t>(std::lround(sound.position.y));
            request.z = static_cast<int32_t>(std::lround(sound.position.z));
            request.positional = true;
            request.hasExplicitZ = true;
            pEventRuntimeState->pendingSounds.push_back(std::move(request));
        }

        for (const ActorBehaviorEffectRequest &effect : result.effects)
        {
            EventRuntimeState::WorldEffectRequest request = {};
            request.id = effect.effectId;
            request.position = {effect.position.x, effect.position.y, effect.position.z};
            request.scale = effect.scale;
            pEventRuntimeState->worldEffectRequests.push_back(std::move(request));
        }

        for (const std::string &message : result.statusMessages)
        {
            pEventRuntimeState->statusMessages.push_back(message);
        }
    }

    for (const ActorBehaviorSummonRequest &summon : result.summons)
    {
        if (!world.summonHostileMonsterById(
                summon.monsterId,
                summon.count,
                summon.position.x,
                summon.position.y,
                summon.position.z,
                0))
        {
            std::cerr << "Actor behaviour summon failed: monster " << summon.monsterId << '\n';
        }
    }
}

void ActorBehaviorRuntime::applyAiFacts(ActorAiFrameFacts &facts)
{
    if (m_pCatalog != nullptr && m_pCatalog->hasTactics())
    {
        applyTactics(facts);
    }

    if (m_instances.empty())
    {
        return;
    }

    const auto applyToActor =
        [this](ActorAiFacts &actor)
        {
            const auto iterator = m_instances.find(actor.actorIndex);

            if (iterator == m_instances.end() || iterator->second.state.actorId != actor.actorId
                || iterator->second.state.deathHandled)
            {
                return;
            }

            InstanceRecord &record = iterator->second;
            const ActorBehaviorInstanceState &state = record.state;

            if (state.perform && state.perform->started)
            {
                actor.behavior.holdActive = true;
                actor.behavior.holdAnimation = state.perform->fallbackAnimation;
                actor.behavior.holdRestartAnimation = record.performRestartPending;
                record.performRestartPending = false;
            }

            if (state.nextCast)
            {
                applySpellFacts(actor.stats, *state.nextCast, m_pSpellTable);
            }
            else if (state.spellOverride)
            {
                applySpellFacts(actor.stats, *state.spellOverride, m_pSpellTable);
            }

            if (state.aiTypeOverride)
            {
                actor.stats.aiType = *state.aiTypeOverride;
            }

            if (state.suppressFlee)
            {
                actor.status.suppressLowHealthFlee = true;
            }
        };

    for (ActorAiFacts &actor : facts.activeActors)
    {
        applyToActor(actor);
    }

    for (ActorAiFacts &actor : facts.backgroundActors)
    {
        applyToActor(actor);
    }
}

void ActorBehaviorRuntime::applyTactics(ActorAiFrameFacts &facts)
{
    const auto applyToActor =
        [this](ActorAiFacts &actor, bool active)
        {
            const ActorTacticsFacts *pTactics = m_pCatalog->tacticsForMonster(actor.identity.monsterId);

            if (pTactics == nullptr)
            {
                return;
            }

            actor.tactics = *pTactics;

            // Only an actor the map placed `immobile` holds a post; the same monster placed free wanders as usual.
            // A post may leave its spot to fight unless its monster is Stationary (no wander radius).
            if (!actor.movement.placedImmobile)
            {
                actor.tactics.holdPost = false;
                actor.tactics.postLeashRadius = 0.0f;
            }
            else if (actor.tactics.holdPost && actor.movement.wanderRadius > 0.0f)
            {
                actor.movement.movementAllowed = true;
            }

            if (active && actor.target.currentKind != ActorAiTargetKind::None && actor.target.currentCanSense
                && !actor.status.dead)
            {
                m_tacticAims[actor.actorIndex] = TacticAim{actor.actorId, actor.target.currentPosition};
            }
            else
            {
                m_tacticAims.erase(actor.actorIndex);
            }
        };

    for (ActorAiFacts &actor : facts.activeActors)
    {
        applyToActor(actor, true);
    }

    for (ActorAiFacts &actor : facts.backgroundActors)
    {
        applyToActor(actor, false);
    }
}

void ActorBehaviorRuntime::observeAiResult(const ActorAiFrameResult &result)
{
    for (const ActorProjectileRequest &request : result.projectileRequests)
    {
        const auto iterator = m_instances.find(request.sourceActorIndex);

        if (iterator != m_instances.end() && iterator->second.state.nextCast
            && request.ability == GameplayActorAttackAbility::Spell1)
        {
            iterator->second.state.nextCast.reset();
        }
    }
}

void ActorBehaviorRuntime::applyPresentation(size_t actorIndex, GameplayRuntimeActorState &state) const
{
    if (!m_tacticAims.empty())
    {
        const auto aim = m_tacticAims.find(actorIndex);

        if (aim != m_tacticAims.end() && aim->second.actorId == state.actorId && !state.isDead)
        {
            state.hasAimTarget = true;
            state.aimTargetX = aim->second.point.x;
            state.aimTargetY = aim->second.point.y;
            state.aimTargetZ = aim->second.point.z;
        }
    }

    const auto iterator = m_instances.find(actorIndex);

    if (iterator == m_instances.end() || iterator->second.state.actorId != state.actorId)
    {
        return;
    }

    const std::optional<ActorBehaviorPerform> &perform = iterator->second.state.perform;

    if (perform && perform->started && !perform->clip.empty() && !state.isDead)
    {
        state.scriptedClip = perform->clip;
        state.scriptedClipSeconds = perform->elapsedSeconds;
    }
}

const std::vector<GameplayProjectileService::ProjectileWardFacts> &ActorBehaviorRuntime::projectileWards() const
{
    return m_projectileWards;
}

const std::vector<ActorBehaviorWardDisplay> &ActorBehaviorRuntime::wardDisplays() const
{
    return m_wardDisplays;
}

void ActorBehaviorRuntime::recordWardBlock(const GameplayProjectileService::ProjectileWardBlock &block)
{
    const auto iterator = m_instances.find(block.ownerActorIndex);

    if (iterator != m_instances.end() && iterator->second.state.actorId == block.ownerActorId)
    {
        iterator->second.pendingWardBlocks.push_back(block.wardId);
        iterator->second.wardImpacts.push_back({block.wardId, block.point, 0.0f});
    }
}

bool ActorBehaviorRuntime::attach(
    size_t actorIndex,
    uint32_t actorId,
    const std::string &behaviorId,
    std::string &error)
{
    if (m_pCatalog == nullptr || m_pCatalog->findBehavior(behaviorId) == nullptr)
    {
        error = "unknown actor behaviour: " + behaviorId;
        return false;
    }

    InstanceRecord record = {};
    record.state.actorIndex = actorIndex;
    record.state.actorId = actorId;
    record.state.behaviorId = behaviorId;
    record.state.randomState = (actorId * 2654435761u) ^ hashText(behaviorId);
    const auto existing = m_instances.find(actorIndex);

    if (existing != m_instances.end())
    {
        if (m_lua)
        {
            m_lua->releaseRegistryReference(existing->second.selfReference);
            m_lua->releaseRegistryReference(existing->second.stateReference);
        }

        m_instances.erase(existing);
    }

    m_instances.emplace(actorIndex, std::move(record));
    return true;
}

bool ActorBehaviorRuntime::attachToWorldActor(
    const IGameplayWorldRuntime &world,
    size_t actorIndex,
    const std::string &behaviorId,
    std::string &error)
{
    GameplayRuntimeActorState state = {};

    if (!world.actorRuntimeState(actorIndex, state))
    {
        error = "invalid actor index " + std::to_string(actorIndex);
        return false;
    }

    const auto existing = m_instances.find(actorIndex);

    if (existing != m_instances.end() && existing->second.state.actorId == state.actorId
        && existing->second.state.behaviorId == behaviorId)
    {
        return true;
    }

    return attach(actorIndex, state.actorId, behaviorId, error);
}

const ActorBehaviorInstanceState *ActorBehaviorRuntime::instance(size_t actorIndex) const
{
    const auto iterator = m_instances.find(actorIndex);
    return iterator != m_instances.end() ? &iterator->second.state : nullptr;
}

std::vector<const ActorBehaviorInstanceState *> ActorBehaviorRuntime::instances() const
{
    std::vector<const ActorBehaviorInstanceState *> result;
    result.reserve(m_instances.size());

    for (const auto &[actorIndex, record] : m_instances)
    {
        result.push_back(&record.state);
    }

    std::sort(result.begin(), result.end(),
        [](const ActorBehaviorInstanceState *pLeft, const ActorBehaviorInstanceState *pRight)
        {
            return pLeft->actorIndex < pRight->actorIndex;
        });
    return result;
}

ActorBehaviorRuntime::Snapshot ActorBehaviorRuntime::snapshot() const
{
    Snapshot snapshot = {};
    snapshot.scannedActorCount = m_scannedActorCount;

    for (const ActorBehaviorInstanceState *pState : instances())
    {
        snapshot.instances.push_back(*pState);
    }

    return snapshot;
}

void ActorBehaviorRuntime::restoreSnapshot(const Snapshot &snapshot)
{
    destroyLua();
    m_instances.clear();
    m_projectileWards.clear();
    m_wardDisplays.clear();
    m_tacticAims.clear();
    m_scannedActorCount = snapshot.scannedActorCount;

    for (const ActorBehaviorInstanceState &state : snapshot.instances)
    {
        InstanceRecord record = {};
        record.state = state;
        record.needsLoadCallback = state.started;
        m_instances.emplace(state.actorIndex, std::move(record));
    }
}

ActorBehaviorRuntime::LuaCallContext *ActorBehaviorRuntime::activeCall() const
{
    return m_pActiveCall;
}

bool ActorBehaviorRuntime::ensureLua()
{
    if (m_lua)
    {
        return true;
    }

    if (m_luaFailed || m_pCatalog == nullptr)
    {
        return false;
    }

    Engine::LuaStateOwner lua;

    if (!lua.isValid())
    {
        m_luaFailed = true;
        return false;
    }

    lua.openApprovedLibraries();
    lua_State *pLuaState = lua.state();

    // Behaviours only use what the engine hands them: no file or chunk loading, and self:random() for randomness so
    // a fight replays identically from a save.
    for (const char *pName : {"dofile", "loadfile", "load", "require"})
    {
        lua_pushnil(pLuaState);
        lua_setglobal(pLuaState, pName);
    }

    lua_getglobal(pLuaState, LUA_MATHLIBNAME);
    lua_pushnil(pLuaState);
    lua_setfield(pLuaState, -2, "random");
    lua_pushnil(pLuaState);
    lua_setfield(pLuaState, -2, "randomseed");
    lua_pop(pLuaState, 1);

    lua_pushlightuserdata(pLuaState, this);
    lua_setfield(pLuaState, LUA_REGISTRYINDEX, RuntimeRegistryKey);

    luaL_newmetatable(pLuaState, SelfMetatableName);
    luaL_newlib(pLuaState, SelfMethods);
    lua_setfield(pLuaState, -2, "__index");
    lua_pop(pLuaState, 1);

    std::optional<std::string> error;

    if (!m_pCatalog->librarySource().empty()
        && !lua.runChunk(m_pCatalog->librarySource(), "@" + m_pCatalog->librarySourcePath(), error))
    {
        std::cerr << "Actor behaviour library failed: " << error.value_or("unknown error") << '\n';
        m_luaFailed = true;
        return false;
    }

    m_lua.emplace(std::move(lua));
    return true;
}

void ActorBehaviorRuntime::destroyLua()
{
    for (auto &[actorIndex, record] : m_instances)
    {
        record.selfReference = LUA_NOREF;
        record.stateReference = LUA_NOREF;
    }

    m_moduleReferences.clear();
    m_lua.reset();
    m_luaFailed = false;
}

int ActorBehaviorRuntime::moduleReference(const std::string &behaviorId)
{
    const auto cached = m_moduleReferences.find(behaviorId);

    if (cached != m_moduleReferences.end())
    {
        return cached->second;
    }

    const ActorBehaviorDefinition *pDefinition = m_pCatalog->findBehavior(behaviorId);

    if (pDefinition == nullptr || !ensureLua())
    {
        return LUA_NOREF;
    }

    lua_State *pLuaState = m_lua->state();
    const std::string chunkName = "@" + pDefinition->sourcePath;

    if (luaL_loadbuffer(pLuaState, pDefinition->source.c_str(), pDefinition->source.size(), chunkName.c_str())
        != LUA_OK)
    {
        std::cerr << "Actor behaviour load failed: " << lua_tostring(pLuaState, -1) << '\n';
        lua_pop(pLuaState, 1);
        m_moduleReferences[behaviorId] = LUA_NOREF;
        return LUA_NOREF;
    }

    // Each behaviour gets its own globals table that falls back to the shared ones.
    lua_newtable(pLuaState);
    lua_newtable(pLuaState);
    lua_pushglobaltable(pLuaState);
    lua_setfield(pLuaState, -2, "__index");
    lua_setmetatable(pLuaState, -2);
    lua_setupvalue(pLuaState, -2, 1);

    std::optional<std::string> error;

    if (!m_lua->call(0, 1, error))
    {
        std::cerr << "Actor behaviour " << behaviorId << " failed: " << error.value_or("unknown error") << '\n';
        m_moduleReferences[behaviorId] = LUA_NOREF;
        return LUA_NOREF;
    }

    if (!lua_istable(pLuaState, -1))
    {
        std::cerr << "Actor behaviour " << behaviorId << " must return Behavior.define{...}\n";
        lua_pop(pLuaState, 1);
        m_moduleReferences[behaviorId] = LUA_NOREF;
        return LUA_NOREF;
    }

    const int reference = m_lua->createRegistryReference();
    m_moduleReferences[behaviorId] = reference;
    return reference;
}

bool ActorBehaviorRuntime::ensureInstanceLua(InstanceRecord &record)
{
    if (record.failed)
    {
        return false;
    }

    if (moduleReference(record.state.behaviorId) == LUA_NOREF)
    {
        // A removed or broken script must not leave its saved wards or overrides active.
        reportScriptError(record, "behaviour script unavailable");
        return false;
    }

    lua_State *pLuaState = m_lua->state();

    if (record.selfReference == LUA_NOREF)
    {
        lua_newtable(pLuaState);
        lua_pushinteger(pLuaState, static_cast<lua_Integer>(record.state.actorIndex));
        lua_setfield(pLuaState, -2, SelfActorKey);
        luaL_setmetatable(pLuaState, SelfMetatableName);
        record.selfReference = m_lua->createRegistryReference();
    }

    if (record.stateReference == LUA_NOREF)
    {
        const int top = lua_gettop(pLuaState);
        size_t offset = 0;

        if (record.state.scriptState.empty())
        {
            lua_newtable(pLuaState);
        }
        else if (!deserializeLuaValue(pLuaState, record.state.scriptState, offset, 0) || !lua_istable(pLuaState, -1))
        {
            lua_settop(pLuaState, top);
            lua_newtable(pLuaState);
            std::cerr << "Actor behaviour state of actor " << record.state.actorIndex << " was unreadable; reset\n";
        }

        record.stateReference = m_lua->createRegistryReference();
    }

    return true;
}

bool ActorBehaviorRuntime::pushHandler(InstanceRecord &record, const char *pHandlerName, const char *pTableKey)
{
    if (!ensureInstanceLua(record))
    {
        return false;
    }

    lua_State *pLuaState = m_lua->state();
    const int top = lua_gettop(pLuaState);
    m_lua->pushRegistryReference(m_moduleReferences.at(record.state.behaviorId));
    lua_getfield(pLuaState, -1, pHandlerName);

    // A handler table (onTimer = {name = fn}) is indexed by the key; a plain function handles every key.
    if (pTableKey != nullptr && lua_istable(pLuaState, -1))
    {
        lua_getfield(pLuaState, -1, pTableKey);
        lua_remove(pLuaState, -2);
    }

    if (!lua_isfunction(pLuaState, -1))
    {
        lua_settop(pLuaState, top);
        return false;
    }

    lua_remove(pLuaState, -2);
    return true;
}

bool ActorBehaviorRuntime::callPushedHandler(
    InstanceRecord &record,
    const ActorBehaviorActorFacts &actor,
    const ActorBehaviorFrameFacts &facts,
    ActorBehaviorFrameResult &result,
    const char *pHandlerName,
    int argumentCount,
    const std::function<void(lua_State *)> &pushArguments)
{
    lua_State *pLuaState = m_lua->state();
    const int top = lua_gettop(pLuaState) - 1;
    m_lua->pushRegistryReference(record.selfReference);
    m_lua->pushRegistryReference(record.stateReference);

    if (pushArguments)
    {
        pushArguments(pLuaState);
    }

    LuaCallContext context = {};
    context.pState = &record.state;
    context.pActor = &actor;
    context.pResult = &result;
    context.partyPosition = facts.partyPosition;
    m_pActiveCall = &context;
    std::optional<std::string> error;
    const bool called = m_lua->call(2 + argumentCount, 0, error);
    m_pActiveCall = nullptr;

    if (!called)
    {
        lua_settop(pLuaState, top);
        reportScriptError(record, std::string(pHandlerName) + ": " + error.value_or("unknown error"));
        return false;
    }

    std::vector<uint8_t> serialized;
    std::string serializeError;
    m_lua->pushRegistryReference(record.stateReference);

    if (serializeLuaValue(pLuaState, -1, serialized, 0, serializeError))
    {
        record.state.scriptState = std::move(serialized);
    }
    else
    {
        reportScriptError(record, serializeError);
    }

    lua_settop(pLuaState, top);
    return true;
}

bool ActorBehaviorRuntime::callHandler(
    InstanceRecord &record,
    const ActorBehaviorActorFacts &actor,
    const ActorBehaviorFrameFacts &facts,
    ActorBehaviorFrameResult &result,
    const char *pHandlerName,
    const char *pTableKey,
    int argumentCount,
    const std::function<void(lua_State *)> &pushArguments)
{
    if (record.failed || !pushHandler(record, pHandlerName, pTableKey))
    {
        return false;
    }

    return callPushedHandler(record, actor, facts, result, pHandlerName, argumentCount, pushArguments);
}

void ActorBehaviorRuntime::updateInstance(
    InstanceRecord &record,
    const ActorBehaviorActorFacts &actor,
    const ActorBehaviorFrameFacts &facts,
    ActorBehaviorFrameResult &result)
{
    ActorBehaviorInstanceState &state = record.state;
    const float deltaSeconds = facts.deltaSeconds;
    const auto call =
        [&](const char *pHandlerName, const char *pTableKey, int argumentCount,
            const std::function<void(lua_State *)> &pushArguments)
        {
            return callHandler(record, actor, facts, result, pHandlerName, pTableKey, argumentCount, pushArguments);
        };

    if (!state.started)
    {
        state.started = true;
        state.lastHp = actor.currentHp;
        call("onStart", nullptr, 0, {});
    }
    else if (record.needsLoadCallback)
    {
        record.needsLoadCallback = false;

        if (!state.deathHandled)
        {
            call("onLoad", nullptr, 0, {});
        }
    }

    if (state.deathHandled)
    {
        return;
    }

    if (actor.dead)
    {
        state.deathHandled = true;
        state.perform.reset();
        state.wards.clear();
        state.timers.clear();
        state.nextCast.reset();
        call("onDeath", nullptr, 0, {});
        return;
    }

    state.ageSeconds += deltaSeconds;

    if (actor.currentHp < state.lastHp)
    {
        const int damage = state.lastHp - actor.currentHp;
        state.lastHp = actor.currentHp;
        call("onDamaged", nullptr, 1, [damage](lua_State *pLuaState) { lua_pushinteger(pLuaState, damage); });
        fireHealthThresholds(record, actor, facts, result);
    }
    else
    {
        state.lastHp = actor.currentHp;
    }

    std::vector<std::string> wardBlocks = std::move(record.pendingWardBlocks);
    record.pendingWardBlocks.clear();

    for (const std::string &wardId : wardBlocks)
    {
        call("onWardBlocked", nullptr, 1,
            [&wardId](lua_State *pLuaState) { lua_pushstring(pLuaState, wardId.c_str()); });
    }

    if (state.perform)
    {
        if (!state.perform->started && !actorIsBusy(actor.animationState))
        {
            state.perform->started = true;
            state.perform->elapsedSeconds = 0.0f;
            record.performRestartPending = true;
        }
        else if (state.perform->started)
        {
            state.perform->elapsedSeconds += deltaSeconds;

            if (state.perform->elapsedSeconds >= state.perform->durationSeconds)
            {
                const std::string clip = state.perform->clip;
                state.perform.reset();
                call("onPerformDone", nullptr, 1,
                    [&clip](lua_State *pLuaState) { lua_pushstring(pLuaState, clip.c_str()); });
            }
        }
    }

    constexpr float WardImpactSeconds = 0.8f;

    for (InstanceRecord::WardImpact &impact : record.wardImpacts)
    {
        impact.ageSeconds += deltaSeconds;
    }

    record.wardImpacts.erase(std::remove_if(record.wardImpacts.begin(), record.wardImpacts.end(),
        [](const InstanceRecord::WardImpact &impact) { return impact.ageSeconds > WardImpactSeconds; }),
        record.wardImpacts.end());

    for (size_t wardIndex = 0; wardIndex < state.wards.size();)
    {
        ActorBehaviorWard &ward = state.wards[wardIndex];
        ward.ageSeconds += deltaSeconds;

        if (!ward.effectId.empty())
        {
            ward.effectCooldownSeconds -= deltaSeconds;

            if (ward.effectCooldownSeconds <= 0.0f)
            {
                ward.effectCooldownSeconds = ward.effectIntervalSeconds;
                result.effects.push_back({ward.effectId, actorCenter(actor), ward.effectScale});
            }
        }

        if (ward.remainingSeconds >= 0.0f)
        {
            ward.remainingSeconds -= deltaSeconds;

            if (ward.remainingSeconds <= 0.0f)
            {
                const std::string wardId = ward.id;
                state.wards.erase(state.wards.begin() + static_cast<std::ptrdiff_t>(wardIndex));
                call("onWardExpired", nullptr, 1,
                    [&wardId](lua_State *pLuaState) { lua_pushstring(pLuaState, wardId.c_str()); });
                continue;
            }
        }

        ++wardIndex;
    }

    std::vector<std::string> expiredTimers;

    for (ActorBehaviorTimer &timer : state.timers)
    {
        timer.remainingSeconds -= deltaSeconds;

        if (timer.remainingSeconds <= 0.0f)
        {
            expiredTimers.push_back(timer.name);
        }
    }

    for (const std::string &name : expiredTimers)
    {
        state.timers.erase(std::remove_if(state.timers.begin(), state.timers.end(),
            [&](const ActorBehaviorTimer &timer) { return timer.name == name && timer.remainingSeconds <= 0.0f; }),
            state.timers.end());
        call("onTimer", name.c_str(), 1,
            [&name](lua_State *pLuaState) { lua_pushstring(pLuaState, name.c_str()); });
    }

    state.thinkCooldownSeconds -= deltaSeconds;

    if (state.thinkCooldownSeconds <= 0.0f && !record.failed && ensureInstanceLua(record))
    {
        lua_State *pLuaState = m_lua->state();
        m_lua->pushRegistryReference(m_moduleReferences.at(state.behaviorId));
        lua_getfield(pLuaState, -1, "think_interval");
        const float interval = lua_isnumber(pLuaState, -1)
            ? std::max(0.05f, static_cast<float>(lua_tonumber(pLuaState, -1))) : DefaultThinkIntervalSeconds;
        lua_pop(pLuaState, 2);
        state.thinkCooldownSeconds = interval;
        call("onThink", nullptr, 0, {});
    }
}

// onHealthBelow = {[0.6] = fn, [0.3] = fn}: each fires once, highest first, when health falls to or below it.
void ActorBehaviorRuntime::fireHealthThresholds(
    InstanceRecord &record,
    const ActorBehaviorActorFacts &actor,
    const ActorBehaviorFrameFacts &facts,
    ActorBehaviorFrameResult &result)
{
    if (record.failed || actor.maxHp <= 0 || !ensureInstanceLua(record))
    {
        return;
    }

    lua_State *pLuaState = m_lua->state();
    const int top = lua_gettop(pLuaState);
    m_lua->pushRegistryReference(m_moduleReferences.at(record.state.behaviorId));
    lua_getfield(pLuaState, -1, "onHealthBelow");

    if (!lua_istable(pLuaState, -1))
    {
        lua_settop(pLuaState, top);
        return;
    }

    const double fraction = double(actor.currentHp) / double(actor.maxHp);
    // Keys stay exact Lua numbers for the lookup below; the saved fired list holds them as floats.
    const std::vector<float> &fired = record.state.firedHealthThresholds;
    std::vector<lua_Number> crossed;
    lua_pushnil(pLuaState);

    while (lua_next(pLuaState, -2) != 0)
    {
        if (lua_type(pLuaState, -2) == LUA_TNUMBER && lua_isfunction(pLuaState, -1))
        {
            const lua_Number threshold = lua_tonumber(pLuaState, -2);

            if (fraction <= threshold
                && std::find(fired.begin(), fired.end(), static_cast<float>(threshold)) == fired.end())
            {
                crossed.push_back(threshold);
            }
        }

        lua_pop(pLuaState, 1);
    }

    lua_settop(pLuaState, top);
    std::sort(crossed.begin(), crossed.end(), std::greater<lua_Number>());

    for (const lua_Number threshold : crossed)
    {
        record.state.firedHealthThresholds.push_back(static_cast<float>(threshold));

        if (record.failed || !ensureInstanceLua(record))
        {
            return;
        }

        m_lua->pushRegistryReference(m_moduleReferences.at(record.state.behaviorId));
        lua_getfield(pLuaState, -1, "onHealthBelow");
        lua_pushnumber(pLuaState, threshold);
        lua_gettable(pLuaState, -2);

        if (!lua_isfunction(pLuaState, -1))
        {
            lua_settop(pLuaState, top);
            continue;
        }

        lua_replace(pLuaState, top + 1);
        lua_settop(pLuaState, top + 1);
        callPushedHandler(record, actor, facts, result, "onHealthBelow", 1,
            [threshold](lua_State *pState) { lua_pushnumber(pState, threshold); });
    }
}

void ActorBehaviorRuntime::rebuildProjectileWards(const ActorBehaviorFrameFacts &facts)
{
    m_projectileWards.clear();
    m_wardDisplays.clear();

    for (const ActorBehaviorActorFacts &actor : facts.actors)
    {
        const auto iterator = m_instances.find(actor.actorIndex);

        if (iterator == m_instances.end() || actor.dead)
        {
            continue;
        }

        for (const ActorBehaviorWard &ward : iterator->second.state.wards)
        {
            GameplayProjectileService::ProjectileWardFacts wardFacts = {};
            wardFacts.ownerActorIndex = actor.actorIndex;
            wardFacts.ownerActorId = actor.actorId;
            wardFacts.center = actorCenter(actor);
            wardFacts.radius = ward.radius;
            wardFacts.wardId = ward.id;
            m_projectileWards.push_back(std::move(wardFacts));

            if (!ward.visual.dome)
            {
                continue;
            }

            ActorBehaviorWardDisplay display = {};
            display.actorId = actor.actorId;
            display.wardId = ward.id;
            display.center = actorCenter(actor);
            display.radius = ward.radius;
            display.groundZ = actor.position.z;
            display.ageSeconds = ward.ageSeconds;
            display.remainingSeconds = ward.remainingSeconds;
            display.visual = ward.visual;

            for (const InstanceRecord::WardImpact &impact : iterator->second.wardImpacts)
            {
                const float dx = impact.point.x - display.center.x;
                const float dy = impact.point.y - display.center.y;
                const float dz = impact.point.z - display.center.z;
                const float length = std::sqrt(dx * dx + dy * dy + dz * dz);

                if (impact.wardId == ward.id && length > 0.001f)
                {
                    display.impacts.push_back({{dx / length, dy / length, dz / length}, impact.ageSeconds});
                }
            }

            m_wardDisplays.push_back(std::move(display));
        }
    }
}

void ActorBehaviorRuntime::reportScriptError(InstanceRecord &record, const std::string &message)
{
    std::cerr << "Actor behaviour " << record.state.behaviorId << " (actor " << record.state.actorIndex
              << ") stopped: " << message << '\n';
    record.failed = true;
    record.state.perform.reset();
    record.state.wards.clear();
    record.state.nextCast.reset();
    record.state.spellOverride.reset();
}
}
