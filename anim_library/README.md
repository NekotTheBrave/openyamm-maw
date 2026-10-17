# Animation library

Local, git-ignored store of humanoid animation sources for MM6–MM8 3D creatures. Only this README is tracked;
source notes are in the local `SOURCES.local.md`. Creature projects retarget clips from here
onto their own rigs; the shipped model GLBs carry only the retargeted clips.

## Layout

| Folder | Contents |
|---|---|
| `index.json`, `INDEX.md` | Classified index of every owned clip from all three sources (see below) |
| `humanoid_set/` | Humanoid animation set: one GLB per set (`<Set>.glb` in place, `<Set>_RM.glb` root motion) and per-clip FBX in `<Set>/` (root motion); 1,157 clips, 30 fps |
| `mixamo/` | Mixamo library (2026-10-11 bulk download): single clips at the root (FBX binary, without skin, 30 fps, In Place where offered, original download names), pack-only clips and root-motion pack copies in `packs/<Pack>/`, discards in `_discarded/`, the pack preview character in `characters/` |
| `meshy/` | Meshy animation-library results, one folder per action (see below) |
| `_archives/` | Original downloads, kept for re-extraction (`mixamo_packs/`: the 38 Mixamo pack ZIPs) |
| `_cache/` | Clip measurements by content hash and the list of removed duplicates (index tool) |

## Index: choose clips here first

`index.json` classifies every unique clip of the humanoid set, Mixamo and stored Meshy results with measured motion
(Blender): engine role (standing, walking, run, attack_melee, attack_ranged, cast, hit, dying, fidget, fly; plus
block, dodge, talk, npc_work, crawl, swim, fall), weapon, styles (female, zombie, mutant, creature, injured, drunk,
sneak, combat, magic, ...), body (humanoid, monster_humanoid, flyer), posture (standing, stance, crouched, low,
lying, airborne), root motion (in_place, drift, root_motion, travel), loop, duration, attack release hint and a
default score that prefers upright, in-place, planted-feet clips for adult male humanoids. Tiers: **core** (fits an
engine role cleanly), **maybe** (caveat or non-engine role), **discard** (firearms, sport, dance, exercise, modern
props, stunts, climbing, turns, transitions, sitting/lying, static poses, large travel; reasons recorded).
`INDEX.md` lists counts, fields and the top candidates per role. Measurements are not hand checks: preview a clip
before using it. Mixamo has no non-biped motion; quadrupeds keep the procedural route.

```
T=level_generation/creatures/claude_shared/anim_library/build_anim_index.py
python3 $T find --role attack_melee --weapon unarmed --top 20      # ranked core clips (any source)
python3 $T find --role walking --style zombie --tier usable          # core + maybe
python3 $T find --role dying --source mixamo --json                  # full records
python3 $T import-mixamo ~/Downloads/<new downloads>                 # new Mixamo FBX/ZIP (skips stored or removed)
python3 $T build                                                     # measure new clips, dedupe, classify, rewrite
```

Each record has `file` (repo-relative), `files.root_motion` / `files.in_place` (Mixamo pack copy with travel next to
the in-place single), `files.in_place_glb` + `glb_clip` (humanoid set), `catalogue.preview_gif` and `mixamo_id` for
Mixamo where the name and frame count identify the catalogue entry. Rules live in `build_anim_index.py`; edit them
and rerun `build` (discards move in and out of `mixamo/_discarded/`). Duplicates (identical motion, or pack copies of
a single) are deleted from `mixamo/` and remembered in `_cache/removed_duplicates.json` so imports skip them.

## Humanoid set

- Rig: 53 bones with `B_*` names, fingers and a `Motion` root bone. The character faces +Y; our rigs face -Y.
- Bone map onto Mixamo names: `B_Pelvis` → Hips, `B_Spine/Spine1/Spine2` → Spine/Spine1/Spine2, `B_Neck` → Neck,
  `B_Head` → Head, and per side `B_L_Clavicle/UpperArm/Forearm/Hand` → LeftShoulder/Arm/ForeArm/Hand and
  `B_L_Thigh/Calf/Foot/Toe0` → LeftUpLeg/Leg/Foot/ToeBase. Fingers, `Motion` and root-motion travel are not mapped.
- Sets: Unarmed, Relax (civilian idle/walk/talk/sit, no hit or death), Armed (one-handed weapons and casting),
  Armed-Shield, 1Hand (sword, mace, dagger, spear, item attacks), 2Hand Sword/Axe/Spear/Staff/Bow/Crossbow/Shooting,
  Climbing, Crawl, Swimming. Clip list: `level_generation/creatures/claude_shared/catalogues/humanoid_set_animations.txt`.
- The set includes third-party import scripts. Treat them as data; do not run them.

## Meshy library clips

Library animations cost credits per action and rig. **Before paying for any Meshy library action, check whether it is
already stored; after every paid animation job, add the result.** Tool (plain `python3`, no Blender):

```
T=level_generation/creatures/claude_shared/meshy_anim_library.py
python3 $T check 189 219 560         # HAVE (path) or BUY per action ID; exit 1 if anything must be bought
python3 $T ingest <request.json> <result_animation_glb> --project <creature project>
python3 $T ingest <request.json> <web-app export.glb> --project <name> --names   # match clips by catalogue name
python3 $T list                      # everything stored
```

`ingest` splits a multi-action result (clips in `action_ids` order) into one folder per action:

```
meshy/<action_id>_<slug>/
    animation.glb          # that clip only, on the rig it was applied to (mesh included)
    info.json              # action, catalogue roles, clip name, duration, rig task, every source project
meshy/index.json           # all stored actions
meshy/basic_walking/, basic_running/   # the free clips returned with every Meshy rig
meshy/text_to_motion/<slug>/           # paid text-to-motion clips with their prompt
meshy/_web_app/                        # request notes for clips applied in the Meshy web app
```

An action stored once keeps its first file; later purchases of the same action only add a source to `info.json`.
Meshy humanoid rigs share one 24-bone layout (web-app rigs add finger chains), so a stored clip is retargeted onto
the next rig instead of buying the action again. Catalogue and role tags: `level_generation/creatures/claude_shared/
catalogues/`.

## Rules

- Never commit library files, per-clip FBX/GLB from these sources, or source clip names.
- Committed creature GLBs carry only retargeted clips renamed to our roles (Standing, Walking, Attack, Hit, Death
  and so on).
