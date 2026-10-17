# Stage 3: animation, textures, exports and game delivery

Use the selected anatomy, repaired rig and motion specification. Resume at the requested substep; a texture-only
request does not authorize reauthoring existing motion. Preserve selected clips and explicit user choices.

Follow the shared playbooks linked from [the skill](../SKILL.md#default-production-workflow-codex-and-claude),
including the current humanoid G4–G7 gates and "Clip timing". Use the selected local source clips and native
identity/action evidence; the older
[creature guidelines](../../../../level_generation/creatures/CREATURE_3D_GUIDELINES.md)
supplement them for compatible motion and sockets. The current
[model runtime contract](../../../../engine/render/MODEL_RENDERING.md) supersedes historical proposed-feature notes.
An explicit native-animation reconstruction or directional sprite request retains its requested source timing.

## Pilot motion before the complete set

Fit the native standing/ready pose with one common model scale and fixed comparison cameras, using all genuine
stored views. Then review/adapt the selected locomotion cycle and a demanding attack or equivalent action as pilots.
For native reconstruction, fit the important source anchors before interpolation. Do not expand the full set while
these expose unresolved relevant controls, poor likeness or implausible choreography. For a creature without walking or attack, choose
its actual locomotion and most demanding available motion; do not invent clips to satisfy this sequence.

Resolve depth and joint rotations on the actual rig using fixed-length chains, consistent elbow/knee poles,
correct parent/rest-space transforms and continuous quaternion/twist paths. A projected wrist position does
not determine palm roll. Keep bone scale unchanged unless a deliberately specified deformation needs it.
Do not translate/scale individual bones to chase guessed screen landmarks or shorten an offset wrist.

Review the pilot's motion arc and timing at normal speed. Check proximal initiation, distal follow-through,
secondary-arm balance, support, head/body contribution and recovery against the study. Revise the fitting seeds
when actual surfaces expose a bad inference, and update the selected specification to explain the correction.
Avoid uniform easing that introduces a stop at every observed pose. More keys or FPS alone cannot add force.

## Complete and review the requested animation set

Cover all discovered/requested actions, preserving gameplay timing, aliases and endpoint rules. Native-animation
reconstruction preserves native durations and unequal source holds; selected in-game 3D locomotion can use a
reviewed preset and movement-driven timing. Separate authored smooth interpolation from the original held bitmaps.
For native reconstruction, keep held states held. Game production can use the playbook's reviewed idle/run clips;
preserve their provenance and the corpse endpoint rather than describing presets as native reconstruction.
Keep source clips at their intended speed. Measure and mark attack/cast impact, adjust the lead-in to fit the
native wind-up, keep Hit within its native duration, and allow death/fidget to finish under the runtime rules.

Death may require collapse, disappearance, disintegration or another effect. Use visible source evidence for
skeletal motion; do not infer hidden bones from flame tips. Specify separate effect, visibility and remains
tracks plus the consumer's capabilities. Keep synchronization and clip-state resets. A Blender-only particle
effect or a skeletal GLB alone may not deliver the promised death appearance.

Run the playbook's numerical gates on every clip and inspect the exported motion with fixed cameras, including
extreme and intermediate poses, native comparisons and alternate 3D views. For explicit native reconstruction,
compare every observed source pose/view at its anchor; stock clips are judged on motion and creature identity.
Inspect head/shoulder/elbow/wrist/palm/fingers, feet, attachments and extra structures at useful detail size.
Review actual playback at normal and slower speeds, all loops and one-shot endpoints. Do not claim playback
review from contact sheets. Exact outgoing blends may be a caller responsibility; document rather than silently
extend a native action or alter a gameplay damage event.

Check evaluated deformed surfaces for contacts, rigid anatomy, volume loss, joint flips, collapse and relevant
intersections. Add dense subframe checks around fast twists, swing/landing and other demonstrated risks.
Review all clips for final completion; after a local repair, recheck the affected regions/clips and unchanged
dependencies rather than rerendering the whole set after every small edit.

For each finding, record the clip, exact time/frame, view, region and expected/observed behavior. Provide a
native/before/after comparison with the affected area highlighted when needed, retaining an unmarked view.
Fix the cause, preserve selected unrelated curves/geometry and verify the changed interpolation too. Keep a
short correction ledger in the canonical progress record; an export-parity pass does not clear visual findings.

## Final geometry and UVs

Settle anatomy/deformation and any hard triangle/influence budget before freezing UVs. Record the actual counts.
Preserve the animated master and export filtering. Freeze intended triangulation before tangent-normal baking.
Unwrap the rest mesh using existing tools and choose chart density for visible detail, especially face, hands,
feet and important attachments. Check UV degeneracy, unintended overlap, stretch, seams, padding, material
assignments and checker appearance. Intentional mirrored/stacked charts need a documented texture-compatible
reason; unique asymmetric features cannot share them indiscriminately.

Ensure UVs remain attached during deformation and use the correct UV set. Retain actual geometry, normals,
weights, bind and animation identity when a UV operation alone was requested. UV/export seam vertex splitting
may change raw counts/order; compare per-corner correspondence and deformed surfaces instead of treating that
normal export representation change as a geometry defect.

## Meshy texturing handoff

Deliver a static body-only upload in the selected rest pose; exclude rig, actions and separate effects/remains.
Keep the full animated master, UVs, effect tracks and source hashes. Use generated HD masters and the selected
playbook's route: current humanoid body retexture uses `enable_original_uv: false` and transfers Meshy's UVs back
by verified face correspondence; head-only or suitable locally unwrapped uploads use `true`. Check returned UV
density, overlaps, seams and correspondence. Later topology corrections can use verified UV carry-over without
automatically paying for another texture. Choose texture resolution by the shared texture guide, record the
upload hash/master revision and apply maps to the animated master. For a user-operated handoff, give the exact
upload, settings and required GLB/maps return.

In FULL, perform this handoff directly through [the job helper](full-production.md), including separate prop
texturing where needed. Submit selected static uploads, download originals/maps and continue through texture return
and final delivery review. Do not stop at upload preparation or replace the animated master with the static return.

Preparation finishes a handoff-only substep; FULL continues automatically. Do not submit a paid service job under
an audit or handoff-only request.
If the user operates Meshy, mark the texture return as awaiting external input and keep other authorized work
moving. Follow the root skill's authorization rules for any agent-operated service job.

## Texture return and delivered-asset review

Compare the returned geometry, UVs, orientation and material slots with the static upload. Do not replace the
animated master with a static texture return. Apply verified maps/materials to the retained master using actual
UV/surface correspondence. If the return changed geometry/UVs, diagnose the discrepancy before transfer;
do not silently remesh, rerig or accept a mismatched texture. Preserve original downloads/maps.

Inspect materials under neutral lighting in front, profile, rear and detail views, including moving/extreme poses:
face/eyes/jaw, digit/claw roots, straps, seams, normal-map orientation and detail coherence. Skin, leather and
bone should not acquire unintended metallic response. Correct confirmed pigmentation/material assignment
locally with reviewed region masks; do not hide anatomy or deformation defects with painted detail.
Check base-color/roughness/metallic/normal conventions in the intended export, not only Blender preview lighting.

Colour tiers and palette variants (user decision 2026-10-08, all 3D models): deliver one shared texture set, an RGBA
region mask (one channel per recolourable region) and per-variant colour ramps from each tier's native palette,
following `level_generation/creatures/CLAUDE_CREATURE_TEXTURES.md`. Do not bake separate base-colour textures per
tier unless a tier differs in pattern or detail, not only colour. Faces seen up close get a separate 1K head set.

Export only the intended objects and synchronized skeletal/morph/effect/visibility tracks. Deliver a normal
editable `.blend` with needed textures packed or correctly linked, a portable model and a clear clip manifest.
Verify action slots, clip names, durations, fractional keys, loop/endpoints and state resets. Reimport the actual
export in isolation and compare appearance and representative evaluated motion across all clips; revisit dense
samples for changed deformation. Record the resulting files/hashes after final tuning, not before it.
Unsupported effect/material behavior in the target consumer remains an explicit limitation or separate task.

Stage 3 production completion needs current visual/motion/material and exported-asset evidence for the requested
deliverable. Report user artistic acceptance separately; do not infer it from technical completion. Give selected
files, covered actions, remaining differences and any concrete review still needed. Complete creature production
continues through [runtime delivery and review](full-production.md#runtime-delivery-and-review): LODs, attachments,
head-material merge, world binding, regression and indoor/outdoor comparisons. Asset-only and narrower requests
stop at their stated boundary; missing runtime review remains explicit when it belongs to the requested scope.

The demon's [complete animation package](../../../../level_generation/creatures/mm6_demon_meshy_reference_20261004/animations_native_v7_20261005/README.md),
[UV handoff](../../../../level_generation/creatures/mm6_demon_meshy_reference_20261004/texturing_upload_20261005/README.md)
and [texture return](../../../../level_generation/creatures/mm6_demon_meshy_reference_20261004/texturing_fixed_20261005/README.md)
show the separation between animated master and static upload. Its faceted death effect, nine clips, large tail
blade and retained extra finger are case-specific outcomes/limitations, not defaults or automatic acceptance.
