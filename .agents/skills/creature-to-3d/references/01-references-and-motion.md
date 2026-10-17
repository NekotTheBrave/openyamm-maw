# Stage 1: references and motion study

Produce a specification another agent can use to correct anatomy and construct, then verify, every requested
animation. This stage may be requested without generation. Reuse adequate selected references and existing
studies, repairing demonstrated omissions rather than automatically rebuilding them. The shared playbooks in
[the skill](../SKILL.md#default-production-workflow-codex-and-claude) govern the production route.

## Select motion before modelling

For humanoids, read the [animation library](../../../../anim_library/README.md), choose clips by their actual
motion, preview them on the source mannequin and extract joint extremes (G1). Use these poses to test the fitted
rig. Record the native action's intent, equipment, gameplay timing and required roles, then the selected source
clips and planned adaptations. Do not require frame-by-frame reconstruction before a library-based production
run. Use the detailed extrapolation section below for explicitly requested native reconstruction or custom actions
whose gesture cannot be supplied by the selected clips. Non-humanoids use their playbook's anatomy-specific motion.

## Native inventory

Resolve the actual creature descriptors, frame tables, original bitmaps and native palettes. Restored runtime
images may have replaced original sprites; find preserved native sources before using them as evidence.
Inventory every descriptor-referenced frame/action, its ordering and hold duration, looping/endpoint behavior,
aliases, stored directions, mirrors and single-image direction reuse. Do not infer playback from filenames.
Include creature-specific actions, held standing/corpse states and effects where present. A suggested action
list is a discovery prompt, not evidence of an absent native action. Reviewed 3D idle/run clips can be selected
for game presentation as the playbook allows; record their provenance rather than calling them native frames.

Keep one native logical canvas, offsets, pivot and comparison scale throughout each family. Build compact
contacts of all unique observed frames/views and inspect enlarged details. Record source provenance and hashes.
An unused bitmap is not another playback frame; mirrors/reused frontal images supply no new depth evidence.
The source canvas bottom is not necessarily a visible ground plane. Preserve uncertainty about hidden support.

## Anatomy and appearance specification

Describe identity before choosing the generated design. Use annotated native views and proportion measurements
where legible, with uncertainty or ranges for ambiguous features. Cover the whole creature:

| Region | Evidence and targets to retain |
| --- | --- |
| Whole body | Head/body ratio, body width/depth, limb lengths, posture, silhouette and negative spaces |
| Head | Skull profile, muzzle, brow, eye position, pupils, ears, horns, teeth, mouth cavity and jaw hinge |
| Hands | Digit count/identity, thumb opposition, palm width/thickness, finger segments, claw shape and reach |
| Feet | Toe count, forefoot/heel proportions, sole shape, spurs, talons and contact surfaces |
| Torso and equipment | Surface/material boundaries, coverage, strap routing, armor, fastenings and prop grip |
| Extra structures | Tail root/curve/tip and depth, wings, additional limbs, spikes and their attachments |
| Materials | Native palette variants, protected details, skin/cloth/leather/bone/metal identity and surface character |

Inventory required props separately from body anatomy. An unarmed modeling pose can improve body/digit generation,
but native weapons, shields and equipment still belong in the assembled deliverable. For FULL, record their source,
current artifacts and missing prerequisites using [FULL production](full-production.md); reuse or generate
separate Meshy props when appropriate.

Distinguish observed features, inferred construction and explicit user choices. Specify which structures are
rigid and which should flex. A written prompt alone does not verify a digit count or ear construction: inspect
the actual selected images. Resolve conflicting views or flag the exact unresolved decision.

## Consistent AI modeling references

Follow [HD master references](../../../../level_generation/creatures/CLAUDE_HD_MASTER_REFERENCES.md) for all five
views, head turnarounds, review gates and generation records before any Meshy job. Never send native sprites,
restored x2 frames or enlargements to Meshy. For authorized generation, inspect local inputs and use the imagegen
skill. Start prompts with explicit context
that this is a faithful reconstruction of an existing MM6/MM7/MM8 game creature. Assign each input its role:
native anatomy/palette, selected appearance, neutral pose or detail evidence. Preserve the native identity and
user-selected detail; do not let an attractive AI reinterpretation redefine proportions or add equipment.

The normal deliverable is one clear full-body three-quarter Meshy input plus separate front, back, left and
right references of the same creature in the same neutral pose. A comparison sheet may collect them, but do
not upload a collage as one creature image. Use a suitable A-pose for humanoids, with open separated digits and
visible limb gaps; choose a defensible exposed rest pose for other anatomy. Keep tail/wings/props visible.
Use consistent orthographic-style scale, restrained lighting and unclipped framing. Match asymmetry and material
boundaries between views. Mark generated unseen sides as inferences; do not call mirrored source views genuine.

Generate/edit from the selected identity rather than independently redesigning each view. Check all returned
views against native evidence and one another, including front/profile anatomy and close details. Repair a
specific mismatch while preserving selected work. Record actual generated dimensions separately from padding
or export enlargement; never count resized pixels as generated detail. Keep required alpha and clean backgrounds.

The selected service route determines the upload: single-image Smart Topology may use the three-quarter input
while the other views guide Blender correction. Multi-image generation is a separate route; verify current
compatibility rather than assuming it has identical Smart Topology options. Retain requested settings and any
service receipt, so triangle count alone is not later treated as proof of a particular generation mode.

## Detailed animation extrapolation (native reconstruction or custom actions)

Use native frames for motion evidence even if enhanced action art is available. Analyze every unique observed
pose, not only the action's two extremes. Use all genuine views at the corresponding times. Record the following
in an existing motion-specification format, or one compact structured specification with a readable review:

| Field | Required useful content |
| --- | --- |
| Time and phase | Native frame/id, hold interval, action phase, proposed key times and end/loop rule |
| Evidence | Annotated landmarks/silhouette, visible overlaps, uncertainty, observed versus inferred targets |
| Main driver | Initiating body segment, action direction, line of action and how movement travels down the chain |
| Main arm/limb | Shoulder/clavicle, elbow direction and bend, wrist path, reach/depth and segment relationships |
| Hand orientation | Finger direction AND palm normal, forearm roll, wrist bend, proximal curl/fan and thumb motion |
| Secondary limb | Guard, counterbalance, lag, retraction, collision clearance and relation to the main movement |
| Support | Planted/lifted contacts, pivots, knee/hip flex, toe-off/landing and restrained inferred weight transfer |
| Body/head | Pelvis/spine yaw, lean/bob, shoulder response, head turn/tilt, gaze, jaw and facial action |
| Extra structures | Tail/wing/clothing lag, attachment behavior, rigid tips and effects/visibility as separate tracks |
| Transition | Motion arc, acceleration/fastest interval/deceleration, overshoot or hold where supported |
| Verification | Features to compare, required views/times, continuity/contact/clearance tests and known ambiguity |

Specify semantic anatomical targets before a rig exists. Projected joint angles are not recovered 3D angles;
hidden depth and palm roll can remain uncertain. Use annotated 2D observations plus constrained 3D fitting
seeds/ranges, and label their spaces/sign conventions. Map these to actual bind matrices during stage 2/3.
Do not prescribe executable bone-local Euler channels from pixel coordinates.

Describe how an action acquires weight and force: proximal initiation, distal follow-through, opposing balance,
support and timing. This is visual choreography, not recovered physical forces. Do not add a large windup,
step, impact or body turn absent from the source. Use source timing with suitable spacing and tangents; uniform
smoothstep/easing that stops at every source anchor can destroy a continuous strike. Inspect transition arcs,
not just interpolated screen positions. Fixed-length limbs and continuous orientation must constrain fitting.

For a claw strike, record the lift/opening, elbow bend, forearm rotation, wrist/palm roll, finger fan, crossing
clearance, free-arm response, torso/head contribution, supporting-foot pivot and recovery. For walking, record
each foot's contact cycle and near/far identity across views, unequal phase durations, opposing arms, bob and
loop seam. For death, separate visible joint evidence from occluded motion and effect/disappearance/remains.
Apply equivalent attention to all other discovered actions; do not force every creature into these examples.

## Outcome and handoff

Stage 1 is ready for model production when the selected references are internally coherent and the anatomy and
motion plan cover the required roles, equipment and timing, with selected clips/ROM for the default humanoid
route or detailed extrapolation for the relevant custom/native actions, and remaining inferences identified.
Planning alone
can finish a plan deliverable while reference generation remains not started; report those substeps separately.
Provide source inventory/timing, selected image paths, anatomy targets, motion specification and review evidence.
Record unresolved decisions and exact Meshy inputs/settings. A preparation-only request ends here; FULL continues
into authorized Meshy generation and subsequent stages without waiting for a user upload.

The demon's [attack study](../../../../level_generation/creatures/mm6_demon_meshy_reference_20261004/attack_study/ATTACK_MOTION_SPEC.md)
and [complete motion study](../../../../level_generation/creatures/mm6_demon_meshy_reference_20261004/animation_study/README.md)
show useful evidence/specification formats. Its
[later foot study](../../../../level_generation/creatures/mm6_demon_meshy_reference_20261004/attack_study/feet_review_20261004/README.md)
supersedes the initial locked-foot interpretation. Carry such corrections into the selected specification;
do not replay historical assumptions or copy the demon's angles, tolerances, frame rate or anatomy.
