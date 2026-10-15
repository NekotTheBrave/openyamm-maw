#include "doctest/doctest.h"

#include "game/gameplay/ActorBehaviorCatalog.h"
#include "game/gameplay/ActorBehaviorRuntime.h"
#include "game/gameplay/GameplayActorAiSystem.h"
#include "game/gameplay/GameplayProjectileService.h"
#include "game/maps/SaveGame.h"
#include "game/tables/SpellTable.h"

#include "engine/TextTable.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using OpenYAMM::Game::ActorAiAnimationState;
using OpenYAMM::Game::ActorAiFacts;
using OpenYAMM::Game::ActorAiFrameFacts;
using OpenYAMM::Game::ActorAiFrameResult;
using OpenYAMM::Game::ActorAiMovementAction;
using OpenYAMM::Game::ActorAiMotionState;
using OpenYAMM::Game::ActorAiTargetKind;
using OpenYAMM::Game::ActorBehaviorActorFacts;
using OpenYAMM::Game::ActorBehaviorCatalog;
using OpenYAMM::Game::ActorBehaviorFrameFacts;
using OpenYAMM::Game::ActorBehaviorFrameResult;
using OpenYAMM::Game::ActorBehaviorRuntime;
using OpenYAMM::Game::ActorProjectileRequest;
using OpenYAMM::Game::GameplayActorAiSystem;
using OpenYAMM::Game::GameplayActorAiType;
using OpenYAMM::Game::GameplayActorAttackAbility;
using OpenYAMM::Game::GameplayProjectileService;
using OpenYAMM::Game::GameplayRuntimeActorState;
using OpenYAMM::Game::SkillMastery;

namespace
{
constexpr float Step = 1.0f / 128.0f;

std::string readSourceFile(const std::string &relativePath)
{
    std::ifstream input(std::filesystem::path(OPENYAMM_SOURCE_DIR) / relativePath);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

OpenYAMM::Game::SpellTable loadSpellTable()
{
    const std::optional<OpenYAMM::Engine::TextTable> table =
        OpenYAMM::Engine::TextTable::parseTabSeparated(readSourceFile("assets_dev/engine/data_tables/spells.txt"));
    std::vector<std::vector<std::string>> rows;

    for (size_t rowIndex = 0; table && rowIndex < table->getRowCount(); ++rowIndex)
    {
        rows.push_back(table->getRow(rowIndex));
    }

    OpenYAMM::Game::SpellTable spellTable;
    spellTable.loadFromRows(rows);
    return spellTable;
}

ActorBehaviorCatalog makeCatalog(const std::string &behaviorSource, int16_t monsterId = 307)
{
    ActorBehaviorCatalog catalog;
    catalog.setLibrary("engine/behaviors/behavior.lua", readSourceFile("assets_dev/engine/behaviors/behavior.lua"));
    catalog.addBehavior("test/boss", "test/boss.lua", behaviorSource);
    catalog.setMonsterDefault(monsterId, "test/boss");
    return catalog;
}

ActorBehaviorActorFacts makeActor(int hp, int maxHp = 100)
{
    ActorBehaviorActorFacts actor = {};
    actor.actorIndex = 0;
    actor.actorId = 41;
    actor.monsterId = 307;
    actor.currentHp = hp;
    actor.maxHp = maxHp;
    actor.hostileToParty = true;
    actor.position = {1000.0f, 0.0f, 0.0f};
    actor.height = 200.0f;
    return actor;
}

ActorBehaviorFrameFacts makeFrame(const ActorBehaviorActorFacts &actor, float deltaSeconds = Step)
{
    ActorBehaviorFrameFacts facts = {};
    facts.deltaSeconds = deltaSeconds;
    facts.actorCount = 1;
    facts.actors.push_back(actor);
    return facts;
}

ActorAiFacts makeAiActor()
{
    ActorAiFacts actor = {};
    actor.actorIndex = 0;
    actor.actorId = 41;
    actor.identity.actorId = 41;
    actor.stats.currentHp = 100;
    actor.stats.maxHp = 100;
    actor.stats.radius = 32;
    actor.stats.height = 128;
    actor.runtime.motionState = ActorAiMotionState::Standing;
    actor.runtime.animationState = ActorAiAnimationState::Standing;
    actor.movement.position = {100.0f, 0.0f, 0.0f};
    return actor;
}

bool hasMessage(const ActorBehaviorFrameResult &result, const std::string &message)
{
    return std::find(result.statusMessages.begin(), result.statusMessages.end(), message)
        != result.statusMessages.end();
}

GameplayProjectileService::ProjectileWardFacts makeWard(float radius)
{
    GameplayProjectileService::ProjectileWardFacts ward = {};
    ward.ownerActorIndex = 0;
    ward.ownerActorId = 41;
    ward.center = {0.0f, 0.0f, 0.0f};
    ward.radius = radius;
    ward.wardId = "shadow";
    return ward;
}
}

TEST_CASE("actor behaviour attaches by monster id and runs onStart once")
{
    const ActorBehaviorCatalog catalog = makeCatalog(R"(
        return Behavior.define{
            onStart = function(self, s)
                s.starts = (s.starts or 0) + 1
                self:say("start " .. s.starts .. " hp " .. self:hp())
            end,
        }
    )");
    ActorBehaviorRuntime runtime;
    runtime.bind(&catalog, nullptr);

    const ActorBehaviorFrameResult first = runtime.update(makeFrame(makeActor(100)));
    CHECK(hasMessage(first, "start 1 hp 100"));
    REQUIRE(runtime.instance(0) != nullptr);
    CHECK(runtime.instance(0)->behaviorId == "test/boss");

    const ActorBehaviorFrameResult second = runtime.update(makeFrame(makeActor(100)));
    CHECK(second.statusMessages.empty());
}

TEST_CASE("actor behaviour ignores monsters without a default behaviour")
{
    const ActorBehaviorCatalog catalog = makeCatalog("return Behavior.define{}", 308);
    ActorBehaviorRuntime runtime;
    runtime.bind(&catalog, nullptr);
    runtime.update(makeFrame(makeActor(100)));
    CHECK(runtime.instance(0) == nullptr);
}

TEST_CASE("actor behaviour fires health thresholds once, highest first")
{
    const ActorBehaviorCatalog catalog = makeCatalog(R"(
        return Behavior.define{
            onDamaged = function(self, s, amount)
                self:say("damaged " .. amount)
            end,
            onHealthBelow = {
                [0.6] = function(self, s) s.order = (s.order or "") .. "60;" self:say(s.order) end,
                [0.3] = function(self, s) s.order = (s.order or "") .. "30;" self:say(s.order) end,
            },
        }
    )");
    ActorBehaviorRuntime runtime;
    runtime.bind(&catalog, nullptr);
    runtime.update(makeFrame(makeActor(100)));

    const ActorBehaviorFrameResult hit = runtime.update(makeFrame(makeActor(20)));
    CHECK(hasMessage(hit, "damaged 80"));
    CHECK(hasMessage(hit, "60;"));
    CHECK(hasMessage(hit, "60;30;"));

    const ActorBehaviorFrameResult again = runtime.update(makeFrame(makeActor(10)));
    CHECK(hasMessage(again, "damaged 10"));
    CHECK(again.statusMessages.size() == 1);
}

TEST_CASE("actor behaviour perform holds the native AI with its fallback animation")
{
    const ActorBehaviorCatalog catalog = makeCatalog(R"(
        return Behavior.define{
            onStart = function(self, s)
                self:perform{clip = "cast_ward", seconds = 0.5, fallback = "attack_ranged", sound = "17shield03"}
            end,
            onPerformDone = function(self, s, clip) self:say("done " .. clip) end,
        }
    )");
    ActorBehaviorRuntime runtime;
    runtime.bind(&catalog, nullptr);

    ActorBehaviorActorFacts busy = makeActor(100);
    busy.animationState = ActorAiAnimationState::AttackMelee;
    const ActorBehaviorFrameResult started = runtime.update(makeFrame(busy));
    REQUIRE(started.sounds.size() == 1);
    CHECK(started.sounds[0].soundName == "17shield03");
    REQUIRE(runtime.instance(0)->perform.has_value());
    CHECK_FALSE(runtime.instance(0)->perform->started);

    runtime.update(makeFrame(makeActor(100)));
    CHECK(runtime.instance(0)->perform->started);

    ActorAiFrameFacts aiFacts = {};
    aiFacts.deltaSeconds = Step;
    aiFacts.fixedStepSeconds = Step;
    aiFacts.activeActors.push_back(makeAiActor());
    runtime.applyAiFacts(aiFacts);
    CHECK(aiFacts.activeActors[0].behavior.holdActive);
    CHECK(aiFacts.activeActors[0].behavior.holdRestartAnimation);
    CHECK(aiFacts.activeActors[0].behavior.holdAnimation == ActorAiAnimationState::AttackRanged);

    GameplayRuntimeActorState presentation = {};
    presentation.actorId = 41;
    runtime.applyPresentation(0, presentation);
    CHECK(presentation.scriptedClip == "cast_ward");

    const ActorBehaviorFrameResult finished = runtime.update(makeFrame(makeActor(100), 0.6f));
    CHECK(hasMessage(finished, "done cast_ward"));
    CHECK_FALSE(runtime.instance(0)->perform.has_value());
}

TEST_CASE("actor AI holds still for a scripted action")
{
    ActorAiFrameFacts frame = {};
    frame.deltaSeconds = Step;
    frame.fixedStepSeconds = Step;
    ActorAiFacts actor = makeAiActor();
    actor.world.active = true;
    actor.status.hostileToParty = true;
    actor.target.currentKind = ActorAiTargetKind::Party;
    actor.target.currentCanSense = true;
    actor.target.currentPosition = {100.0f, 500.0f, 0.0f};
    actor.behavior.holdActive = true;
    actor.behavior.holdRestartAnimation = true;
    actor.behavior.holdAnimation = ActorAiAnimationState::AttackRanged;
    frame.activeActors.push_back(actor);

    const GameplayActorAiSystem aiSystem = {};
    const ActorAiFrameResult result = aiSystem.updateActors(frame);
    REQUIRE(result.actorUpdates.size() == 1);
    const OpenYAMM::Game::ActorAiUpdate &update = result.actorUpdates[0];
    CHECK(update.state.motionState == ActorAiMotionState::Standing);
    CHECK(update.animation.animationState == ActorAiAnimationState::AttackRanged);
    CHECK(update.animation.animationTimeTicks == doctest::Approx(0.0f));
    CHECK(update.movementIntent.action == ActorAiMovementAction::Stand);
    CHECK_FALSE(update.attackRequest.has_value());
    CHECK(update.movementIntent.updateYaw);
}

TEST_CASE("actor behaviour wards feed projectile facts and report blocks")
{
    const ActorBehaviorCatalog catalog = makeCatalog(R"(
        return Behavior.define{
            onStart = function(self, s)
                self:addWard{id = "shadow", radius = 400, seconds = 1, effect = "openyamm:fx/shadow_ward",
                    effectInterval = 0.25}
            end,
            onWardBlocked = function(self, s, id) self:say("blocked " .. id) end,
            onWardExpired = function(self, s, id) self:say("expired " .. id) end,
        }
    )");
    ActorBehaviorRuntime runtime;
    runtime.bind(&catalog, nullptr);
    const ActorBehaviorFrameResult started = runtime.update(makeFrame(makeActor(100)));
    CHECK(started.effects.size() == 1);
    REQUIRE(runtime.projectileWards().size() == 1);
    CHECK(runtime.projectileWards()[0].radius == doctest::Approx(400.0f));
    CHECK(runtime.projectileWards()[0].center.z == doctest::Approx(100.0f));

    GameplayProjectileService::ProjectileWardBlock block = {};
    block.ownerActorIndex = 0;
    block.ownerActorId = 41;
    block.wardId = "shadow";
    runtime.recordWardBlock(block);
    CHECK(hasMessage(runtime.update(makeFrame(makeActor(100))), "blocked shadow"));

    const ActorBehaviorFrameResult expired = runtime.update(makeFrame(makeActor(100), 1.0f));
    CHECK(hasMessage(expired, "expired shadow"));
    CHECK(runtime.projectileWards().empty());
}

TEST_CASE("projectile wards stop projectiles entering from outside only")
{
    std::vector<GameplayProjectileService::ProjectileWardFacts> wards = {makeWard(400.0f)};
    GameplayProjectileService::ProjectileState projectile = {};
    projectile.sourceKind = GameplayProjectileService::ProjectileState::SourceKind::Party;

    GameplayProjectileService::ProjectileMotionSegment inbound = {};
    inbound.startX = -600.0f;
    inbound.endX = -200.0f;
    const auto block = GameplayProjectileService::findProjectileWardBlock(projectile, inbound, wards, 1.0f);
    REQUIRE(block.has_value());
    CHECK(block->point.x == doctest::Approx(-400.0f));
    CHECK(block->wardId == "shadow");

    // A collision with something outside the ward comes first.
    CHECK_FALSE(GameplayProjectileService::findProjectileWardBlock(projectile, inbound, wards, 0.25f).has_value());

    GameplayProjectileService::ProjectileMotionSegment inside = {};
    inside.startX = -100.0f;
    inside.endX = -900.0f;
    CHECK_FALSE(GameplayProjectileService::findProjectileWardBlock(projectile, inside, wards, 1.0f).has_value());

    GameplayProjectileService::ProjectileMotionSegment miss = {};
    miss.startX = -600.0f;
    miss.startY = 500.0f;
    miss.endX = 600.0f;
    miss.endY = 500.0f;
    CHECK_FALSE(GameplayProjectileService::findProjectileWardBlock(projectile, miss, wards, 1.0f).has_value());

    projectile.sourceKind = GameplayProjectileService::ProjectileState::SourceKind::Actor;
    projectile.sourceId = 41;
    CHECK_FALSE(GameplayProjectileService::findProjectileWardBlock(projectile, inbound, wards, 1.0f).has_value());
}

TEST_CASE("actor behaviour spells, AI type and flee overrides adjust AI facts")
{
    const ActorBehaviorCatalog catalog = makeCatalog(R"(
        return Behavior.define{
            onStart = function(self, s)
                self:setSpell{spell = 15, skill = 8, mastery = "master", chance = 40}
                self:castNext{spell = 90, skill = 10, mastery = "grandmaster"}
                self:setAiType("aggressive")
                self:setNoFlee(true)
            end,
        }
    )");
    ActorBehaviorRuntime runtime;
    runtime.bind(&catalog, nullptr);
    runtime.update(makeFrame(makeActor(100)));

    ActorAiFrameFacts aiFacts = {};
    aiFacts.activeActors.push_back(makeAiActor());
    runtime.applyAiFacts(aiFacts);
    const ActorAiFacts &forced = aiFacts.activeActors[0];
    CHECK(forced.stats.hasSpell1);
    CHECK(forced.stats.spell1Id == 90);
    CHECK(forced.stats.spell1SkillLevel == 10);
    CHECK(forced.stats.spell1SkillMastery == SkillMastery::Grandmaster);
    CHECK(forced.stats.spell1UseChance == 100);
    CHECK(forced.stats.aiType == GameplayActorAiType::Aggressive);
    CHECK(forced.status.suppressLowHealthFlee);

    ActorAiFrameResult aiResult = {};
    ActorProjectileRequest request = {};
    request.sourceActorIndex = 0;
    request.ability = GameplayActorAttackAbility::Spell1;
    aiResult.projectileRequests.push_back(request);
    runtime.observeAiResult(aiResult);

    ActorAiFrameFacts nextFacts = {};
    nextFacts.activeActors.push_back(makeAiActor());
    runtime.applyAiFacts(nextFacts);
    CHECK(nextFacts.activeActors[0].stats.spell1Id == 15);
    CHECK(nextFacts.activeActors[0].stats.spell1UseChance == 40);
    CHECK(nextFacts.activeActors[0].stats.spell1SkillMastery == SkillMastery::Master);
}

TEST_CASE("actor behaviour state, timers and thresholds survive a snapshot")
{
    const ActorBehaviorCatalog catalog = makeCatalog(R"(
        return Behavior.define{
            onStart = function(self, s)
                s.phase = 1
                s.nested = {list = {3, 2, 1}, name = "mage", ratio = 0.5, flag = true}
                self:startTimer("again", 2)
            end,
            onHealthBelow = {[0.5] = function(self, s) s.phase = 2 end},
            onLoad = function(self, s)
                self:say("load " .. s.phase .. " " .. s.nested.name .. " " .. s.nested.list[1] .. " "
                    .. tostring(s.nested.flag) .. " " .. s.nested.ratio)
            end,
            onTimer = {again = function(self, s) self:say("timer " .. s.phase) end},
        }
    )");
    ActorBehaviorRuntime runtime;
    runtime.bind(&catalog, nullptr);
    runtime.update(makeFrame(makeActor(100)));
    runtime.update(makeFrame(makeActor(40)));
    const ActorBehaviorRuntime::Snapshot snapshot = runtime.snapshot();
    REQUIRE(snapshot.instances.size() == 1);
    CHECK(snapshot.instances[0].timers.size() == 1);
    CHECK(snapshot.instances[0].firedHealthThresholds.size() == 1);

    ActorBehaviorRuntime restored;
    restored.bind(&catalog, nullptr);
    restored.restoreSnapshot(snapshot);
    const ActorBehaviorFrameResult loaded = restored.update(makeFrame(makeActor(40)));
    CHECK(hasMessage(loaded, "load 2 mage 3 true 0.5"));

    // The fired 0.5 threshold does not fire again, and the saved timer still runs.
    const ActorBehaviorFrameResult timer = restored.update(makeFrame(makeActor(30), 2.0f));
    CHECK(hasMessage(timer, "timer 2"));
}

TEST_CASE("actor behaviour death clears directives and runs onDeath once")
{
    const ActorBehaviorCatalog catalog = makeCatalog(R"(
        return Behavior.define{
            onStart = function(self, s) self:addWard{id = "w", radius = 100} end,
            onDeath = function(self, s) self:say("dead") end,
        }
    )");
    ActorBehaviorRuntime runtime;
    runtime.bind(&catalog, nullptr);
    runtime.update(makeFrame(makeActor(100)));
    CHECK(runtime.projectileWards().size() == 1);

    ActorBehaviorActorFacts dead = makeActor(0);
    dead.dead = true;
    CHECK(hasMessage(runtime.update(makeFrame(dead)), "dead"));
    CHECK(runtime.projectileWards().empty());
    CHECK(runtime.update(makeFrame(dead)).statusMessages.empty());
}

TEST_CASE("actor behaviour sandbox and script errors stop only that script")
{
    const ActorBehaviorCatalog catalog = makeCatalog(R"(
        return Behavior.define{
            onStart = function(self, s)
                self:say("random " .. tostring(math.random) .. " load " .. tostring(load) .. " io " .. tostring(io))
                local value = self:random(1, 6)
                self:say(value >= 1 and value <= 6 and "dice ok" or "dice bad")
                s.fn = function() end
            end,
            onThink = function(self, s) self:say("think") end,
        }
    )");
    ActorBehaviorRuntime runtime;
    runtime.bind(&catalog, nullptr);
    const ActorBehaviorFrameResult result = runtime.update(makeFrame(makeActor(100)));
    CHECK(hasMessage(result, "random nil load nil io nil"));
    CHECK(hasMessage(result, "dice ok"));
    // Storing a function in the saved state is an error: the behaviour stops, the actor keeps its native AI.
    CHECK(runtime.update(makeFrame(makeActor(100), 1.0f)).statusMessages.empty());
}

TEST_CASE("Behavior.define rejects misspelt handlers")
{
    const ActorBehaviorCatalog catalog = makeCatalog(R"(
        return Behavior.define{onStrat = function(self, s) self:say("never") end}
    )");
    ActorBehaviorRuntime runtime;
    runtime.bind(&catalog, nullptr);
    CHECK(runtime.update(makeFrame(makeActor(100))).statusMessages.empty());
}

TEST_CASE("boss mage behaviour raises its ward at 60 percent health")
{
    ActorBehaviorCatalog catalog;
    catalog.setLibrary("engine/behaviors/behavior.lua", readSourceFile("assets_dev/engine/behaviors/behavior.lua"));
    catalog.addBehavior("bosses/necro_mage", "engine/behaviors/bosses/necro_mage.lua",
        readSourceFile("assets_dev/engine/behaviors/bosses/necro_mage.lua"));
    catalog.setMonsterDefault(307, "bosses/necro_mage");
    const OpenYAMM::Game::SpellTable spellTable = loadSpellTable();
    REQUIRE(spellTable.findByName("Shrapmetal") != nullptr);
    ActorBehaviorRuntime runtime;
    runtime.bind(&catalog, &spellTable);

    runtime.update(makeFrame(makeActor(137, 137)));
    REQUIRE(runtime.instance(0) != nullptr);
    CHECK(runtime.instance(0)->suppressFlee);
    CHECK(runtime.projectileWards().empty());

    const ActorBehaviorFrameResult phase2 = runtime.update(makeFrame(makeActor(80, 137)));
    CHECK(runtime.projectileWards().size() == 1);
    CHECK(runtime.instance(0)->perform.has_value());
    CHECK(runtime.instance(0)->spellOverride.has_value());
    CHECK_FALSE(phase2.statusMessages.empty());

    const ActorBehaviorFrameResult phase3 = runtime.update(makeFrame(makeActor(30, 137)));
    CHECK(phase3.summons.size() == 2);
    CHECK(runtime.instance(0)->nextCast.has_value());
}

TEST_CASE("actor behaviour snapshots round-trip through save files")
{
    OpenYAMM::Game::ActorBehaviorInstanceState instance = {};
    instance.actorIndex = 12;
    instance.actorId = 77;
    instance.behaviorId = "bosses/necro_mage";
    instance.started = true;
    instance.lastHp = 80;
    instance.ageSeconds = 14.5f;
    instance.randomState = 123456789u;
    instance.scriptState = {6, 0, 0, 0, 0};
    instance.firedHealthThresholds = {0.6f};
    instance.timers.push_back({"ward_return", 3.5f});
    OpenYAMM::Game::ActorBehaviorWard ward = {};
    ward.id = "shadow";
    ward.radius = 400.0f;
    ward.remainingSeconds = 12.0f;
    ward.effectId = "openyamm:fx/shadow_ward";
    instance.wards.push_back(ward);
    instance.perform = OpenYAMM::Game::ActorBehaviorPerform{"cast_ward", ActorAiAnimationState::AttackRanged, 1.6f,
        0.4f, true};
    instance.spellOverride = OpenYAMM::Game::ActorBehaviorSpell{15, 8, SkillMastery::Master, 40};
    instance.aiTypeOverride = GameplayActorAiType::Aggressive;
    instance.suppressFlee = true;

    OpenYAMM::Game::GameSaveData save;
    save.mapFileName = "oute3.odm";
    save.hasOutdoorRuntimeState = true;
    save.outdoorWorld.actorBehaviors.instances.push_back(instance);
    save.outdoorWorld.actorBehaviors.scannedActorCount = 13;
    save.outdoorWorldStates[save.mapFileName] = save.outdoorWorld;
    save.indoorSceneStates["6d19.blv"].worldRuntime.actorBehaviors.instances.push_back(instance);

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "openyamm_actor_behaviour.oysav";
    std::string error;
    REQUIRE_MESSAGE(OpenYAMM::Game::saveGameDataToPath(path, save, error), error.c_str());
    const std::optional<OpenYAMM::Game::GameSaveData> restored = OpenYAMM::Game::loadGameDataFromPath(path, error);
    std::filesystem::remove(path);
    REQUIRE_MESSAGE(restored.has_value(), error.c_str());

    const ActorBehaviorRuntime::Snapshot &outdoor = restored->outdoorWorldStates.at("oute3.odm").actorBehaviors;
    CHECK(outdoor.scannedActorCount == 13);
    REQUIRE(outdoor.instances.size() == 1);
    const OpenYAMM::Game::ActorBehaviorInstanceState &loaded = outdoor.instances[0];
    CHECK(loaded.actorIndex == 12);
    CHECK(loaded.actorId == 77);
    CHECK(loaded.behaviorId == "bosses/necro_mage");
    CHECK(loaded.started);
    CHECK(loaded.randomState == 123456789u);
    CHECK(loaded.scriptState == instance.scriptState);
    CHECK(loaded.firedHealthThresholds == instance.firedHealthThresholds);
    REQUIRE(loaded.timers.size() == 1);
    CHECK(loaded.timers[0].name == "ward_return");
    REQUIRE(loaded.wards.size() == 1);
    CHECK(loaded.wards[0].effectId == "openyamm:fx/shadow_ward");
    REQUIRE(loaded.perform.has_value());
    CHECK(loaded.perform->clip == "cast_ward");
    CHECK(loaded.perform->started);
    REQUIRE(loaded.spellOverride.has_value());
    CHECK(loaded.spellOverride->skillMastery == SkillMastery::Master);
    CHECK(loaded.aiTypeOverride == GameplayActorAiType::Aggressive);
    CHECK(loaded.suppressFlee);
    CHECK(restored->indoorSceneStates.at("6d19.blv").worldRuntime.actorBehaviors.instances.size() == 1);
}

TEST_CASE("actor behaviour wards publish dome displays with their visual and recent hits")
{
    const ActorBehaviorCatalog catalog = makeCatalog(R"(
        return Behavior.define{
            onStart = function(self, s)
                self:addWard{id = "shadow", radius = 300,
                    visual = {color = {255, 170, 60}, intensity = 1.5, rim = 4, noise = 0.2}}
                self:addWard{id = "hidden", radius = 100, visual = false}
            end,
        }
    )");
    ActorBehaviorRuntime runtime;
    runtime.bind(&catalog, nullptr);
    runtime.update(makeFrame(makeActor(100)));
    CHECK(runtime.projectileWards().size() == 2);
    REQUIRE(runtime.wardDisplays().size() == 1);
    const OpenYAMM::Game::ActorBehaviorWardDisplay &display = runtime.wardDisplays()[0];
    CHECK(display.wardId == "shadow");
    CHECK(display.radius == doctest::Approx(300.0f));
    CHECK(display.groundZ == doctest::Approx(0.0f));
    CHECK(display.center.z == doctest::Approx(100.0f));
    CHECK(display.visual.color[0] == 255);
    CHECK(display.visual.color[1] == 170);
    CHECK(display.visual.intensity == doctest::Approx(1.5f));
    CHECK(display.visual.rimPower == doctest::Approx(4.0f));
    CHECK(display.impacts.empty());

    GameplayProjectileService::ProjectileWardBlock block = {};
    block.ownerActorIndex = 0;
    block.ownerActorId = 41;
    block.wardId = "shadow";
    block.point = {1300.0f, 0.0f, 100.0f};
    runtime.recordWardBlock(block);
    runtime.update(makeFrame(makeActor(100)));
    REQUIRE(runtime.wardDisplays()[0].impacts.size() == 1);
    CHECK(runtime.wardDisplays()[0].impacts[0].direction.x == doctest::Approx(1.0f));
    CHECK(runtime.wardDisplays()[0].ageSeconds > 0.0f);

    // Ripples last under a second.
    runtime.update(makeFrame(makeActor(100), 1.0f));
    CHECK(runtime.wardDisplays()[0].impacts.empty());
}
