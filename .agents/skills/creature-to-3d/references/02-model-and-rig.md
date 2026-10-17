# Stage 2: model ingestion, likeness and rig examination

Turn the supplied Meshy output into a model that matches the selected references and has usable deformation
controls. Inspection-only requests produce findings; cleanup/ingestion requests authorize relevant local edits.
Keep the original download and work from a named revision with reproducible input provenance.

## Default rig route

Follow the applicable shared playbook from [the skill](../SKILL.md#default-production-workflow-codex-and-claude).
Humanoids use the source-frame rig: compare Meshy joint estimates with skin-ring measurements, fit hand frames
and finger chains to the mesh, clone source frames with the measured rest swing, and copy world rotations at the
source's 30 fps. Meshy's rig supplies estimates and weights, not an automatically accepted final rig. Apply the
playbook's G2–G5 checks, connectivity-based finger weights, twist rules, accessory ownership and solved prop grip;
reduce after weight transfer while protecting face/hands. Use the selected source extremes from stage 1.
Non-humanoids use the playbook's local rig and appropriate weighting; do not buy humanoid rig jobs for unsupported
anatomy. Existing selected rigs are preserved on scoped repair/resume unless a relevant defect requires change.

## Inspect the static model

Check actual objects/components, counts, coordinate conventions, scale, rest pose, normals, materials and any
UVs, rig or clips already present. Preserve the current selected model and unrelated Blender work.
The usual initial Meshy request is Smart Topology at approximately 15,000 triangles. This is a starting budget;
check the actual result and any hard delivery cap. Refinement may change the count. Avoid automatic reduction
that destroys identity or joint surfaces. Fine scales/grain can belong in textures/normal maps, but silhouette,
negative spaces, digit separation and characteristic anatomy need geometry.

Compare clay front, profile, rear and three-quarter views at a fixed scale against the anatomy specification
and selected detailed images. Inspect head/ears, hands/digits, feet, attachments and extra structures closely.
Use consistent cameras for before/after comparisons. A successful topology checker is not a likeness review.
Create an explicit discrepancy list and distinguish accepted user deviations from faults.

## Correct structure and anatomy before rigging

Screen for misplaced vertices/long bridges, degenerate or duplicate faces, inappropriate disconnected/internal
geometry, damaged boundaries, nonmanifold joins, inverted normals and insufficient joint surface structure.
Interpret findings anatomically: separate teeth/eyes/armor and intentional open cloth surfaces are legitimate.
Do not weld the whole character or close every opening merely to reach a zero-boundary statistic.

Repair confirmed causes locally where possible. Preserve good topology/coordinates and compare affected views.
Use local retopology or rebuild a specific form when its structure cannot support the required shape/deformation;
do not hide it with whole-body smoothing, heavy subdivision, dramatic lighting or a texture patch.
Adapt wrong head/ear forms, digit count, palm/foot shape, proportions, tail depth and equipment attachments now.
Do not postpone clear anatomy differences into animation fitting or remove digits through pose foreshortening.

Validate the changed structure and actual exported static model, including neutral/clay likeness. The outcome
is a selected base model with known unresolved differences. Honor existing acceptance; require further user
input only for a material unresolved design choice or a requested review, not each routine reversible repair.

## Meshy rigging handoff and return

In FULL, use the owned project and durable job helper from [FULL production](full-production.md) to submit,
poll, download and ingest directly. Missing required equipment is an independent component prerequisite;
generate/reuse it and verify assembly rather than accepting a complete-looking body as the whole creature.

Prepare the selected model in the correct orientation/units/rest pose, with the service-required materials or
provisional texture if applicable. State the exact upload file, verified settings and requested return files.
Keep final UV/texture production in stage 3; a provisional rigging requirement does not freeze final materials.
Use current official documentation for the selected web/API route. Do not assume humanoid auto-rigging supports
quadrupeds, wings, extra limbs, ghosts or every MM6–MM8 creature. For unsupported anatomy, choose a scoped Blender
rig/control adaptation or report the required alternative; do not distort the creature to fit a humanoid tool.

On return, compare the rigged mesh with the selected static input. Detect unexpected geometry/proportion/UV
changes before accepting it. Inspect hierarchy, bind matrices, pivots, actual joint centers, bone axes, weights,
influence limits and manual action selection. Bone labels and displayed endpoints are not anatomical proof.
The demon's forearm endpoint was short of its actual wrist; fitting must use the child's wrist joint center.

## Deformation and control probes

Stock Walking/Running clips are useful probes, not native-motion reconstructions. Check normal/slow playback,
contacts and full-body/profile views, then stress the important controls in isolation and in combined source
extremes. Choose probes from the creature's motion study:

- Shoulder/clavicle raise and cross-body reach; attachments follow their actual anatomical owner.
- Elbow bend, forearm pronation and wrist extremes; distribute twist without winding or collapsing the forearm.
- Hand open/curl/fan and thumb motion; inspect every actual digit, rigid tips, palm orientation and surface gaps.
- Jaw opening with head fixed; upper skull/muzzle/upper teeth stay put while the lower jaw hinges correctly.
- Hip/knee/ankle flex and planted/swing feet; rigid talons do not inherit inappropriate humanoid toe curls.
- Head turn, tail/wing/extra-limb ranges and prop grip; maintain attachments, support and clearance.

Diagnose ugly deformation before changing weights. Wrong geometry, hierarchy, pivots, local-space conversion or
orientation targets need their own repair. A continuous-looking endpoint can hide a long twist between keys.
Check palm normals from real hand geometry and continuous rotation paths; do not classify mirrored palms from
the sign of a nearly zero component. Weight rigid structures appropriately and blend soft attachment roots.
Add twist/helper controls only where an observed deformation needs them, preserving existing motion as agreed.

Inspect the evaluated mesh, not just bones: joint/core volume, hard-tip rigidity, collapsed/inverted faces,
ground contact, body/prop clearance and visible creases. Numerical screens complement close visual review.
Use tolerances proportional to model scale and intended flexion; a limited collision/radius test is not a proof
that every surface is safe. Sample densely near fast rotations/contact transitions where integer frames miss faults.

## Outcome and handoff

Stage 2 is ready for pilot animation when the selected anatomy is reviewed, needed controls operate correctly
and representative extreme poses deform convincingly. Deliver the corrected rig/master, tested poses/clips,
rig mapping and remaining findings. Record stock clips as presets and retain their provenance.
If awaiting the rig, report stage 2's geometry substep as complete and rig examination as awaiting external input.
For FULL, poll the recorded job and continue through rig review into pilot/full animations. Ingesting a rig is not
permission to produce all animations when the request only covered that handoff.

Worked evidence: [demon geometry cleanup](../../../../level_generation/creatures/mm6_demon_meshy_reference_20261004/meshy_cleanup/README.md),
[stock-rig correction](../../../../level_generation/creatures/mm6_demon_meshy_reference_20261004/meshy_rig_review/README.md),
and [attack orientation correction](../../../../level_generation/creatures/mm6_demon_meshy_reference_20261004/attack_animation_v2/README.md).
The last repaired palm/forearm targets without further reweighting. These are failure examples, not a universal
repair algorithm, fixed bone count, required watertight mesh or acceptable finger mismatch.
