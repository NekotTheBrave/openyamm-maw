# MM9 RUDE Dialogue

## Decision

MM9 maps will use MM9 RUDE dialogue semantics and the MM9 dialogue presentation. They will not convert dialogue into
the MM6-MM8 flat topic list or shorten the party's sentences into topic labels.

RUDE remains the authoritative dialogue content. An importer may convert the source files into a lossless typed runtime
format, but it must preserve every row, original field, prompt, response, condition, action, and identifier. OpenYAMM
must not parse RUDE into MM6-MM8 NPC topic tables.

See `MM9_WORLD_CONTENT_MIGRATION_INVENTORY.md` for the world-owned dialogue, journal, UI, and deferred asset scope.
See `MM9_RUDE_SCR_INTEGRATION.md` for the authoritative gameplay integration contract covering dialogue-linked SCR,
items, rewards, promotions, skills, services, and completion gates.

## Data And Runtime

RUDE is a conditional dialogue graph. A choice contains the party prompt, NPC response, destination node or service
operation, and conditions/actions controlling availability and state changes.

The validated 30-column key groups are:

- c07, c09, c11, c13, c15: required keys;
- c16-c20: keys granted by ordinary rows;
- c21-c25: forbidden keys; and
- c26-c30: keys removed by ordinary rows.

Service rows (`next < -1`) reuse action-side tail fields as opcode-specific parameters, so c16-c20 and c26-c30 must
not be applied to them as generic key mutations.

The implementation must:

- import `NPC*.rude`, `NPCNAME.rude`, and `TOPBLURB.rude` from `mm9/extracted/RUDE/RUDE/`;
- preserve source row identity and unknown/raw fields for validation and future coverage;
- expose only choices whose conditions currently pass, ordered by their RUDE choice slot;
- display the selected row's response and follow its next node, close action, or service operation;
- implement script entry through `DoRude` and the corresponding `OnRudeExit` behavior;
- integrate RUDE services such as shops, training, travel, banks, inns, healing, hiring, dismissal, donations, quest
  handoffs, and item combination with shared OpenYAMM gameplay systems;
- keep RUDE state in normal world/save state rather than a separate MM9 engine mode.

## Presentation

Use the original MM9-style lower-screen NPC popup:

- a wide dialogue panel suitable for complete party sentences;
- NPC name and response text above the choices;
- the original party prompts shown verbatim as selectable lines;
- three visible choices per page, with previous/next controls when needed;
- MM9 background, fonts, normal/hover colors, spacing, and interaction behavior;
- wrapped text instead of truncation or generated short labels.

The renderer may reuse shared OpenYAMM UI infrastructure, input, dialogue state, and gameplay services, but MM9 must
have its own presentation profile. The MM6-MM8 narrow vertical topic column is not a fallback for MM9 RUDE dialogue.

### Presentation References

OpenMM9's reference implementation uses:

- background: `OpenMM9/assets_dev/source/art/INTERFACESCREENS/NPCPOPUP/RUDEBG.pcx`;
- normal NPC names, body text, responses, and choices: `OpenMM9/assets_dev/source/art/FONTS/RUDEFONT.pcx`;
- hovered choices and service headings/status: `OpenMM9/assets_dev/source/art/FONTS/RUDERED.pcx`;
- layout source: `OpenMM9/assets_dev/source/art/INTERFACESCREENS/NPCPOPUP/NPCPOPUPCOORDS.txt`;
- implementation: `OpenMM9/game/dialogue/DialogueOverlay.cpp`, `OpenMM9/game/ui/UiTextService.cpp`, and
  `OpenMM9/assets_dev/engine/ui/screens/npc_popup.yml`.

The fonts are PCX bitmap atlases. OpenMM9 uses the non-`256` variants for this popup. OpenYAMM already has the source
assets under `mm9/extracted/ART/ART/FONTS/` and `mm9/extracted/ART/ART/INTERFACESCREENS/NPCPOPUP/`, including the
divider and navigation-arrow images.

## Initial Slice

MM9 raw quest keys map reversibly into the global party QBit set as `qbit = 90000 + raw_key`. The declared MM9
namespace is `90000-99999`; raw key `0` is invalid and QBit `90000` therefore remains reserved. The currently observed
highest raw key, `9516`, maps to QBit `99516`.

Implement NPC 436 in Bootcamp first. Replace the temporary Ravensford Guide MM6-MM8 NPC-table dialogue with the
imported RUDE graph, verify node navigation, and then add its script exit callback and required service/condition
coverage incrementally. Keep this desktop-only while MM9 support remains desktop-only.

The initial OpenYAMM runtime now loads the complete raw RUDE directory only for MM9, presents the MM9-specific RUDE
background, resolves NPC names/top blurbs, navigates nodes (including node 0 and node 999), handles close rows, gates
topics on every required/forbidden key column, applies ordinary-row key mutations, and invokes source-object-specific
`OnRudeExit` callbacks exactly once. Service opcodes are decoded and reported, but their complete transactions remain
explicit follow-up work.

OpenMM9 and GodotMM9 may be used as behavioral references for RUDE graph handling and the three-choice popup, but no
code is to be copied from them.
