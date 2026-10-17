---
name: creature-restore
description: Restore MM6–MM8 creature sprites from native BMP frames using packed complete animations per view, outline-only anatomy guides and detailed appearance references, with enhanced atlases, placement and selective palette variants. Use for CREATURE_RESTORE requests or equivalent restoration, planning and resume work in OpenYAMM. Rigged 3D creation uses the separate sprite-to-3D workflow.
---

# Creature restoration

Accept a source pattern or creature name plus optional natural-language scope overrides:

```text
$creature-restore m230*.bmp
CREATURE_RESTORE gob*.bmp
$creature-restore m230*.bmp — front attack only
$creature-restore m242*.bmp — inspect and plan only; do not generate
$creature-restore gob*.bmp — resume existing work
$creature-restore gob*.bmp — packed outline sheets, all native walking views
```

These are agent requests, not shell commands. Resolve patterns against OpenYAMM's native creature assets.
Explicit user limits override defaults. Planning, questions, and skill maintenance do not authorize image generation.
For plan-only work, inspect sources and report scope, batch strategy, and uncertainties, then stop before generation.

## Authoritative procedure

The preferred generation recipe is
[Packed animation sheets with outline-only anatomy guides](../../../level_generation/creatures/PACKED_ANIMATION_OUTLINE_WORKFLOW.md),
selected by the user after the detailed MM6 Goblin batch on 2026-09-30. Submit one complete original animation per
stored view with untouched native colour, a matching separate outline-only geometry sheet and selected detailed
appearance art. Preserve fine texture and natural shading; do not add internal contrast guides or flatten detail.
Read that guide for packing, outline construction, input authority, the corrected prompt and the worked evidence.
Explicit user scope/method overrides still apply. Preserve whole cycles when reporting a resolution shortfall;
do not silently substitute individual poses or smaller batches.

Start with the **Lean default workflow** in
[Sprite Atlas and Variants Pipeline](../../../level_generation/creatures/SPRITE_ATLAS_VARIANTS_PIPELINE.md#lean-default-workflow).
Read its detailed sections only for the current operation or a demonstrated problem; do not load historical case
studies or future engine work for an ordinary restoration. It owns sampling, masks and the export contract.
Read relevant sections of [Direct Sprite Workflow](../../../level_generation/creatures/DIRECT_SPRITE_WORKFLOW.md)
when preparing prompts, extracting alpha, aligning frames, or fixing a demonstrated fault.
Resolve these links relative to this skill's installed directory, `.agents/skills/creature-restore/`.

Before AI generation, use the available imagegen skill and inspect the local reference images. Use the built-in
image-generation tool; do not switch to a separately billed API without explicit user authorization.
Start every generation prompt with explicit existing-asset context, before the animation instructions. For MM6:

```text
TASK CONTEXT — MM6 FANTASY-GAME ASSET RESTORATION: This is an upscale and faithful restoration of an EXISTING
Might and Magic VI (MM6) fantasy role-playing game sprite asset, using the supplied original animation bitmaps.
Restore the existing fantasy-game character and its original animation; preserve its native costume, material
coverage, anatomy and poses. The request is game-art restoration.
```

Use the actual source game/version for MM7 or MM8. On a requested context-only retry, prepend this header to the
existing prompt without rewriting its body, version the prompt, and retain the old submission and reference hashes.
This context describes the task; it does not guarantee that imagegen will return an image.
For a requested rigged model, follow [Sprite-to-3D Workflow](../../../level_generation/creatures/SPRITE_TO_3D_WORKFLOW.md)
instead of silently substituting direct sprites for the requested deliverable.

## Defaults and creature-specific decisions

- Restore all original unique frames, actions, stored views, and native palette variants for the resolved family.
  Resolve descriptors and frame groups before inferring meaning from filenames. Discover palette IDs, frame counts,
  timing, shared images, mirrored views, scale, transparency, and `Center`/anchor flags from source data.
  Keep native reuse and mirror aliases; extra poses or missing-view synthesis require expanded user scope.
- Use one correctly paletted native in-game appearance as the enhanced base. Choose and record a representative
  variant after comparing native palettes. Embedded BMP colors are not automatically the intended appearance.
  Discover recolorable regions and protected details; do not hardcode green cloth, three variants, or one mask.
- Reuse adequate selected detailed identity art. Otherwise let the first useful complete animation/view sheet
  establish it, inspect it and continue without an extra disposable identity call or driver approval checkpoint
  unless requested. Carry selected appearance references into subsequent sheets; geometry stays native.
- Pack one complete original animation per stored view using a shared native union crop and minimal safe padding,
  with a matching separate outline-only sheet. Optimize its grid for actual generated detail, targeting ≥2×,
  preferably ≥2.2×. The shared `sheet_checks.py plan` may suggest fewer poses; do not adopt that reduction in
  complete-animation mode. The user's 2026-10-01 decision prioritizes one whole animation/view sheet submitted and
  returned over the 2× target: below-2× results may pass normal visual/technical acceptance with their measured
  sampling reported. Do not split or regenerate solely for that shortfall. Explicit strict-minimum requests override
  this default; export enlargement never counts as generated detail.
  Preserve anatomy, equipment, material placement, lighting, fine detail and original tonal weight.
- Generate the base art once. Author the smallest sufficient set of soft coverage masks on the enhanced pixels;
  native palette differences guide region selection but are not directly resized masks for changed anatomy.
  Derive all other palette appearances with fixed, documented recoloring parameters and deterministic previews.
  Recoloring must preserve alpha, shading detail, and protected regions; the base preset is an exact bypass.
- Target 2× physical export resolution while preserving native logical size and anchoring. Report actual generated
  detail separately from export size. Clean matte spill before premultiplied-alpha resizing; preserve thin features.
  Keep crop offsets and identical base/mask atlas placement. Do not independently fit silhouettes to equal bounds.
- Automatically preserve and validate exact native anchors before final placement review and acceptance, following
  [exact native anchor placement](../../../level_generation/creatures/SPRITE_ATLAS_VARIANTS_PIPELINE.md#exact-native-anchor-placement).
  Keep fractional logical offsets; at 2×, a half native pixel is one physical pixel. Check the exported mapping too.
  Correct placement before freezing acceptance hashes; any later correction requires affected checks and reacceptance.
  This arithmetic check does not replace independent anatomical review or authorize bulk migration of accepted art.
- Automatically run the pipeline's [residual spill check](../../../level_generation/creatures/DIRECT_SPRITE_WORKFLOW.md#automatic-residual-spill-check)
  on every cleaned frame and final atlas-extracted frame. Reuse existing processing/checking tools;
  inspect a compact worst-edge light/dark contact, then correct confirmed fringe RGB locally before resizing/masks.
  Preserve alpha, thin features and intended material/variant colors. Rebuild affected outputs and verify the result.
  Reuse raw AI sheets for color-only repairs; successful extraction or a lower chroma score alone does not pass review.

## Efficient completion and handoff

Stage work under `level_generation/creatures/<resolved-creature>/`, reusing its existing project when present.
Retain source provenance, raw outputs, exact prompts, selected versions, and generation/cleanup/export settings.
On resume, inspect existing manifests and accepted outputs first; reuse valid checkpoints and rerun only affected
stages. Existing goblin scripts are worked examples with creature-specific assumptions, not a general exporter.

Run cheap static checks first, then inspect every selected pose in compact native/restored sequence contacts, plus
one combined edge/mask/variant review. Inspect representative fine detail at 100%, and every flagged region.
Numeric sampling is necessary but never proves faithful, nonpixelated, nonblurry reconstruction. Static sequence
review is the default; use a timed preview only to resolve motion uncertainty or when requested. Do not claim to
have watched playback when only contacts were inspected. Repair affected poses/stages, preserving accepted work.

For placement acceptance, inspect the selected landmarks drawn on the actual art at native logical size or larger.
Check a second anatomical feature that did not determine the transform; a near-zero residual on the alignment
landmark only proves arithmetic. Follow the independent placement review in Direct Sprite Workflow. When a weapon
or other feature fools a detector, audit every pose using that detector before accepting the repair.

For user-style manual scale/offset adaptation or an agent-prepared placement checkpoint, follow
[Anatomical placement proposals](../../../level_generation/creatures/ANATOMICAL_PLACEMENT_WORKFLOW.md).
Classify the action's contact mechanics, distinguish anatomical size from weapon/wing silhouette height,
and record the scale/anchor choices plus an independent body check. Use the shared proposal compiler for
explicitly measured landmarks; preserve approved checkpoints and native motion in isolated proposals.

Use shared scripts and existing project metadata; do not author a new Python tool, viewer, README, per-frame report
or duplicate JSON audit for each creature. A shared-template viewer generated by one local script call is fine;
do not spend model/API calls writing per-creature viewer code. Add special code only for an unsupported operation.
Keep tool output to summaries/failures and update the existing ledger once after acceptance, not after every check.

Deliver checked 2× physical masters, base/mask atlas pages and the placement/animation/variant metadata needed to use them.
Report actual generated sampling separately, including accepted below-2× whole-animation results.
Keep source/raw/prompt provenance and one compact, artifact-bound acceptance record; reuse existing JSON formats.
Cheap deterministic HTML viewer export may run automatically. Browser traces, viewer coding/customization and
separate review documents are not required; use them only when requested or needed to resolve a specific defect.
Do not remove existing accepted artifacts. Keep aligned masters and machine-computed memory totals in the manifest.
Finish with output paths, coverage, validation, and any remaining visible limitation.
Stage assets without overwriting native art or installing them into gameplay unless requested. Runtime atlas loading,
selective shaders, and renderer integration are separate scope: inspect current support and state honestly what works.
