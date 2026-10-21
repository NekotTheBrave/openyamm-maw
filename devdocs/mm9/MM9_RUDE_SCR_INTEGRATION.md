# MM9 RUDE And NPC SCR Integration Contract

## Purpose

This document defines the desired end state for MM9 dialogue in OpenYAMM. It covers RUDE graphs, the dialogue-linked
subset of SCR behavior, quest state, items, rewards, promotions, skills, services, and save state.

`MM9_RUDE_DIALOGUE.md` remains the presentation and RUDE-format reference. This document is the gameplay integration
contract.

MM9 support remains desktop-only until that restriction is explicitly changed.

## Completion Standard

The target is fully functional dialogue, not a text-only RUDE viewer. Every reachable choice must:

- use the original prompt, response, ordering, conditions, and destination;
- execute its state changes and dialogue-linked SCR callback exactly once and in source order;
- preserve the affected party, object, and dialogue state across save/load;
- enter a working shared OpenYAMM service when the row requests one; or
- be visibly unavailable with a specific reason while an intentionally deferred dependency is missing.

No imported condition, command, item, promotion, skill operation, or service may silently succeed as a no-op.
Importer and test reports must identify every unsupported reachable operation by source file and line or RUDE row.

## RUDE Ownership

The original `.rude` files are authoritative. Import must preserve every source row, column, identifier, prompt,
response, condition, action, and unknown field. Generated indexes or typed caches must be lossless and reproducible.

Raw MM9 quest keys map to global party QBits as:

```text
qbit = 90000 + raw_mm9_key
```

Raw key `0` is invalid, QBit `90000` is reserved, and the MM9 range is `90000-99999`.
Generated MM9 Lua must use the full-width quest-bit event API for this range. The legacy packed EVT selector stores a
16-bit index and therefore cannot represent MM9 QBits above `65535` without truncation.

MM9 current quests remain world-owned content. `NPC997.rude` generates an optional active-world
`data_tables/english/quests_overlay.txt` whose rows declare required and forbidden full-width QBits. The shared journal
loader merges this with the base quest table and evaluates the conditions generically. A newly satisfied quest row uses
the normal OpenYAMM portrait effect, `QuestGot` reaction, and `SoundId::Quest`; explicit MM9 `QUEST.wav` calls map to the
same deduplicated shared cue.

The runtime must evaluate all validated RUDE conditions, including QBits, inventory, class/promotion state, skills,
and other decoded state. Selecting a row must perform its validated action or service and must invoke the correct
dialogue exit behavior when the conversation closes.

## SCR Conversion Boundary

Do not port or recreate OpenMM9's general SCR virtual machine/runtime. Extend the existing MM9 importer into a strict,
build-time SCR-to-Lua compiler for dialogue-linked behavior.

Select source scripts through imported map objects with both `DoRude` and `ScriptName`. Do not select scripts only by
an `NPC*.scr` filename pattern because shops, retainers, named characters, and other dialogue actors use shared or
descriptive script names.

Compile only the routines transitively required by dialogue and its quest consequences, principally:

- `Use` and `DoRude` entry paths;
- `OnRudeExit` and routines called by it;
- `OnFoundPlayer` when it starts dialogue;
- map/object callbacks that are required to maintain dialogue or quest state; and
- scalar declarations, parameters, conditions, assignments, calls, and exits needed by those routines.

The generated Lua should call a small MM9 compatibility surface backed by shared OpenYAMM systems. It must not expose
or duplicate a second party, inventory, class, skill, audio, map, or save runtime.

Do not compile general actor AI from `NPCBASE.inc`, `BASEWANDER.inc`, `FOLLOWPLAYER.inc`, door-navigation includes,
combat scripts, socializing, or unrelated animation machinery as part of dialogue support. Use shared OpenYAMM actor
behavior or explicit authored overlays for those cases. Standard conversation interaction should make the actor face
the party through the shared interaction path.

Unsupported syntax or commands in a required routine are generation failures for that binding. Do not emit a generic
runtime `unimplemented()` fallback. Keep source provenance in generated Lua and reports.

Generic scripts may be attached to multiple actors with different parameters. Bind generated state and callbacks by
map source-object identity, not only by RUDE id or script filename. Persist source-equivalent object/script variables
that affect later dialogue outcomes.

Generated MM9 Lua must remain generated. Fix the importer or add an explicit authored map overlay instead of editing
generated output.

## Shared Party Rewards

Dialogue-linked commands use the existing OpenYAMM party state:

- `HasKey`, `GiveKey`, and `TakeKey` use the MM9 QBit mapping above;
- `GiveGold`, `TakeGold`, and gold checks use shared carried gold;
- `GiveExp` adds the original amount to shared party/character experience according to recovered source behavior;
- item commands use the canonical MM9-to-global item mapping; and
- promotion and skill commands use the compatibility rules below.

Preserve source order and original idempotence. Do not add hidden anti-duplication around rewards; the original key and
item checks are responsible for preventing repeated rewards.

## MM9 Items

MM9 items used by dialogue will ultimately be real MM9 items imported from authoritative MM9 item data, initially
`MASTERITEMLIST.csv`. They are not aliases for unrelated MM6-MM8 items.

Import the dialogue-referenced item subset first and expand it as content is enabled. Each imported item must have a
canonical MM9 identity and an explicit mapping to its global runtime item id; raw MM9 ids must not be used directly in
the merged item namespace.

Original MM9 item visuals will later be converted into inventory-compatible 2D sprites from their source icons. Item
statistics and gameplay properties may be deliberately retuned for OpenYAMM, but the source identity and provenance
must remain available and such changes must be explicit data overrides.

Until a referenced MM9 item is imported, that individual item operation must fail visibly and emit a runtime diagnostic
with source provenance. The generated SCR routine continues with later independent operations, so a missing item cannot
prevent subsequent quest keys, gold, experience, or other state from being applied. An unavailable item still tests as
absent and must never grant, remove, or test an unrelated item with a colliding raw id.

MM9 spellbooks and spell scrolls are excluded. Their dialogue/content references must use the existing OpenYAMM spell,
spellbook, and spell-scroll set. Do not add MM9 spells as part of this work.

## Promotions And Classes

Do not create a separate MM9 party/class runtime. Initially, MM9 `GivePromo` operations map into the existing shared
OpenYAMM class set through an explicit data-owned compatibility table. Preserve the raw MM9 promotion token in
provenance and diagnostics.

The initial nearest-mechanics mapping is:

| MM9 promotion | OpenYAMM class result |
| --- | --- |
| Mercenary | Cavalier |
| Gladiator | Champion |
| Crusader | Crusader |
| Assassin | Assassin |
| Ranger | Ranger Lord |
| Paladin | Hero |
| Scholar | Wizard |
| Mage | Arch Mage |
| Lich | Lich |
| Healer | Priest |
| Priest | High Priest |
| Druid | Arch Druid |

Promotion eligibility and RUDE visibility checks must use the same compatibility data as reward application. An
ineligible promotion must not overwrite a character class silently.

This mapping may be refined when testing exposes a better correspondence. If authentic MM9 character creation or
class progression is implemented later, additional canonical MM9 class entries may be added deliberately to the
shared class registry. Do not introduce them implicitly as a side effect of dialogue import.

## Skills And Mastery

Use only the existing OpenYAMM skill set and mastery levels. Do not add MM9-only skills.

Direct MM9 skills map to their OpenYAMM equivalents. Composite MM9 skills use explicit groups:

| MM9 skill | OpenYAMM skill operation |
| --- | --- |
| Blade | Dagger, Sword, and Axe topics |
| Cudgel | Mace |
| Spear | Staff and Spear topics |
| Armor | Leather Armor, Chain Armor, and Plate Armor topics |
| Elemental | Fire Magic, Air Magic, Water Magic, and Earth Magic topics |

Composite MM9 trainer labels expand into separate OpenYAMM topics. Each expanded topic uses the shared mastery-training
evaluation and transaction independently, including its normal skill-level prerequisite, class/race cap, gold cost,
and already-learned result. For example, Expert Elemental exposes Expert Fire, Air, Water, and Earth Magic; a character
may buy only the schools permitted by the active shared class and race tables.

MM9 spell identities are not imported. Spell learning, spellbooks, and spell scrolls continue to use the OpenYAMM
spell catalogue.

## Services

The final dialogue path includes all observed RUDE service operations through shared OpenYAMM gameplay systems:

- shops and training;
- skill/mastery training;
- travel and town portal;
- bank, inn, healer, and donation;
- hire and dismiss;
- item combination; and
- quest handoff.

Services may be delivered incrementally, but a service opcode is not complete merely because its name is displayed.
It must perform the transaction, update state, and return to or close dialogue according to recovered behavior.

## Verification Gates

Completion requires focused automated coverage proving at least:

- all 436 normal RUDE graphs load without losing source rows or fields;
- visibility reacts to mapped QBits, items, promotions, and skills;
- node navigation, close rows, and paging preserve source ordering;
- each dialogue actor resolves the correct source-object-specific script and parameters;
- `OnRudeExit` executes exactly once on every close path;
- item, gold, experience, promotion, and skill rewards survive save/load;
- composite skill learning/mastery is atomic and affects the complete mapped group;
- every reachable service opcode has implemented behavior or a specific visible unavailable result; and
- the generated dialogue/SCR coverage report contains no silently ignored reachable operation.
