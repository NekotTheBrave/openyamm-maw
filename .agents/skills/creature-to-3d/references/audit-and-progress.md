# Audit, progress and resume

Identify what exists, what its evidence supports and what should happen next. Audit does not automatically
repair findings, generate images, upload to services or initialize a progress file.

## Establish selected artifacts

Start in the named creature project. Read its existing README/manifests, source/acceptance records and explicit
selected-version pointers. Reconcile later corrections and user decisions before selecting files. Modification
time, a high version number, an `acceptance.json` filename or a file named `final` does not establish selection
or user approval. If no selection is explicit, report a candidate with its rationale and uncertainty; inspect
plausible candidates as needed without overwriting the record.

Identify the native inventory, selected appearance/anatomy/motion specifications, static model, rig, animation
master, UV master, static upload, returned textures and final exports. Check their actual presence, relevant
hashes and revision relationships. Read the real artifact metadata when needed; do not repeat stale README
counts as measurements. Preserve privacy of unrelated files; Downloads or archives are relevant only when a
project record names them as sources.

Treat reports bound to a previous hash/revision as historical. Determine affected scope: a material-factor edit
may leave geometry/motion evidence applicable while invalidating material review and delivery hashes. A changed
ear topology can invalidate vertex-index masks; a changed foot can invalidate contact checks. Do not mark every
unrelated stage stale, and do not claim an earlier report certifies the changed output without preservation evidence.

## Read-only audit procedure

1. Establish selected/candidate files and stage/substep coverage from existing evidence.
2. Inspect representative native/reference/model comparisons and current artifacts for the audit's scope.
   Separate directly inspected observations from historical reports. Use isolated/background Blender loading
   with auto-execution disabled; do not save over the WIP or replace a live authoring scene.
3. Run only applicable existing checks after inspecting their side effects. Scripts named `validate` may save,
   normalize, render or export: do not run those in place during a read-only audit. If fresh checks/renders are
   needed, direct all writes to a disposable `/tmp` copy/output and leave repository artifacts untouched.
4. Classify findings as confirmed defect, unverified criterion, accepted deviation or external input needed.
   Identify the earliest unresolved prerequisite as well as the furthest produced artifact.
5. Give the next useful action and its owner, exact input/output paths, affected rechecks and any decision needed.
   A broad audit should cover all stages; a named head/rig/texture audit stays within that scope.

Do not require full animation rerenders to answer a stage/status question. State unverified review coverage
honestly. Missing evidence is not proof of a defect. A visible defect is not cleared by a passing export check.
Technical check results, agent visual review and explicit user artistic acceptance are independent records.

Use a concise report with selected files followed by a stage table:

| Stage/substep | Status | Evidence and gaps | Next action/owner |
| --- | --- | --- | --- |
| References and motion | ... | ... | ... |
| Model and rig | ... | ... | ... |
| Animation, UVs, materials/export | ... | ... | ... |

Then list consequential findings with artifact/clip/time/view/region, required correction or verification and
affected stages. Rank prerequisites before cosmetic refinements. Say which comparisons/playback/checks were
actually performed. Report findings in chat; save an audit only when requested, reusing the canonical record.

## One production progress record

Reuse a project's canonical manifest if it can hold progress and decisions without changing unrelated schemas.
Otherwise create `creature_3d_progress.json` in the creature project when production begins. Use JSON or the
project's established structured format; no database, workflow engine or custom status tool is needed.
Keep detailed source inventories, motion plans and verification outputs in their existing files and link them.

Record only the state needed to resume:

- Creature/source identity, requested deliverable/scope and last update time.
- Selected artifact roles with the established manifest-relative path convention, revision/hash and related master.
- Required body/prop components, native/design evidence, selected artifacts, attachment/grip and assembly review.
- For agent-operated Meshy, `meshy.credit_limit` and `meshy.jobs`: request settings, uploaded paths/hashes,
  submission/reservation/task ids, consumption, receipts and downloaded paths/hashes. Helpers preserve other fields.
- Stage and meaningful substeps, evidence links, and current findings that prevent the requested outcome.
- For complete production, installed model/binding revisions plus viewer, regression and indoor/outdoor comparison
  evidence; for narrower requests, record runtime installation/review as outside scope rather than missing work.
- Technical/agent visual evidence with artifact identity, covered clips/views/times and actual review method.
- User acceptance/deviations with the recorded statement or decision reference and applicable artifact/scope.
- A short correction ledger: finding, cause, changed region/clip and affected recheck result.
- Next action, owner (`codex` or `user`), required input, and concrete external handoff instructions if relevant.

Stage/substep statuses are `not_started`, `in_progress`, `awaiting_external`, `needs_revision`, `unverified`
and `complete`. Use `needs_revision` only for confirmed faults; `unverified` for insufficient current evidence.
Completion means the requested substep's output and review criteria are satisfied. A plan can be complete
while image generation has not started, and a texture handoff can be complete while its return is pending.
Track user acceptance independently as `not_recorded`, `accepted` or `rejected`, including explicitly limited
acceptance. Do not infer an approval from silence, subsequent tasks or an agent-authored acceptance filename.

Update once at a useful production milestone or handoff. Do not create another ledger per pass, rewrite history
as though old findings were current, or initialize progress files for every historical creature during skill work.
Audit/status stays read-only unless recording was requested. User-provided acceptance later supersedes a stale
pending note; production resume can reconcile that decision in the canonical record.

## Resume and invalidation

Resume from the earliest unresolved criterion relevant to the authorized work. Reuse verified checkpoints,
selected references and existing user choices. Do not start imagegen, remesh or rerig solely because a new agent
arrived or a newer procedure exists. For a scoped texture repair, report an unrelated anatomy issue without
silently expanding the task. For full production, fix confirmed prerequisite faults before dependent work.

| Changed input | Revisit affected evidence |
| --- | --- |
| Native inventory, timing or selected design | Relevant specification, likeness or clip fitting |
| Geometry/topology or bind/hierarchy | Affected anatomy, masks/weights, deformation, UV correspondence and export |
| Weights or animation curves | Affected posed surfaces, contacts/volume, interpolation, motion and export |
| UVs or triangulation | Charts, texture correspondence, tangent-normal/material appearance and export |
| Maps or material factors | Material/detail/seam review and exported appearance; retain proven unchanged motion |
| Export settings/file | Actual delivered structure, timing, appearance and appropriate motion comparisons |

Use comparison/preservation evidence to keep unaffected results valid. A confirmed earlier-stage defect can
move the next action backward even when a later artifact exists. If a user-operated Meshy return is missing,
give the exact upload/return instructions and mark awaiting external input; elapsed time supplies neither a
return nor approval. Continue other independent authorized work rather than polling indefinitely.
