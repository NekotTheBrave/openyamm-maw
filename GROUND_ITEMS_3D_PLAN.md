# Items on the ground in 3D: plan and status

Status (2026-10-16, evening): **rolled back for now at the user's request.** Items on the ground draw their sprites
again; the 911 installed models, their textures and `engine/models/items.yml` moved to
`assets_legacy/rollback_3d_20261016/items/` (restore steps there). The engine code stays (without bindings it draws
nothing), and so do the generators, designs and work exports in `level_generation/props/items_claude/`. When resumed:
reliefs for flat items, the [3D ground item playbook](level_generation/props/items_claude/GROUND_ITEM_3D_PLAYBOOK.md)
for volumetric ones (Codex HD master, Meshy Smart Topology, Codex-painted views baked into PBR). MM9 is out of scope.

Items lying on the ground are the last gameplay billboards outside the MM7/MM8 monsters: projectiles, impacts and
decorations are already 3D models or effects ([All projectiles](PROJECTILE_VFX_ALL_PROPOSAL.md),
[MM6 2D retirement](level_generation/MM6_2D_RETIREMENT.md)).

## 1. Audit (MM6-MM8 merged item table)

`assets_dev/engine/data_tables/items.txt` has 2198 rows; 304 are placeholders without an icon (`Pic File` null:
unused artifacts, `_potion/reagent` slots, "quest item N"), leaving **1894 real items**. On the ground an item shows
its object's sprite (`Sprite Index` -> `object_list.txt` ObjectId -> sprite): **367 distinct ground sprites**, small
side views drawn once per game (MM6 `3*` 107, MM7 `7obj*` 112, MM8 `obj*` 145; MM8 reuses much of MM7's art). The
inventory icons are far richer: **911 distinct icons**, every one restored at x2 (`icons_x2`, median 89 x 150 px).

| Family | Items | Ground sprites | Icons |
|---|---|---|---|
| Scrolls | 514 | 7 | 18 |
| Books | 315 | 3 | 13 |
| Misc and quest items | 231 | 149 | 189 |
| Potions (bottles) | 105 | 31 | 31 |
| Wands | 75 | 3 | 15 |
| Weapons (sword 68, spear 45, axe 38, bow 36, mace 35, dagger 31, staff 15, club 8, blaster 5) | 281 | 57 | 281 |
| Rings, amulets, gems, ore, reagents, herbs, gold | 160 | 92 | 157 |
| Helms, shields, cloaks, boots, belts, gauntlets, body armour | 195 | 38 | 193 |

Example: the 68 swords share 12 ground sprites (about 4 shapes, once per game) but have 68 distinct icons.

Placed items per map (`scene.yml` `sprite_objects`): median 0; the busiest maps are MM7 `7d12` (135), MM8 `out03`
(132), MM7 `7out04` (109), MM6 `cd2` (72); runtime drops add a few dozen.

## 2. Decision

Two options were weighed: one model per ground-sprite family (about 150-250 after merging the games' duplicates) or
one model per item (911 icons). Per item wins because the icons are the better source and the models are generated,
not hand-built, so the work is in a few templates, not per item:

- **Per item, generated from its icon** (about 800 icons): weapons, wands and tools; shields, cloaks, armour, belts,
  scrolls, books and amulets (flat reliefs lying on the ground, the way such items lie); potions (turned bottles);
  rings, gems, ore and coins; most quest items.
- **Per family for bulky items** (helms, boots, gauntlets: 77 icons, 16 ground sprites): one front icon does not show
  enough of a helmet; one model per family, tinted per item where cheap.
- **Meshy later and selectively** for bulky items worth their own shape (artifact helms), about 30 credits each; a few
  important quest items by hand.

## 3. Performance

Not a deciding factor. Worst case about 150 items at 300-1200 triangles: under 200k triangles, about 100 draws for
100 distinct models (static stand-in instances, instanced per asset), icon-sized textures (BC7 128 x 512 is 64 KB)
for about 3 MB of video memory. A model is loaded the first time its item appears on a map (a few milliseconds for a
small GLB). Disk: 30-50 MB for about 900 models.

## 4. Engine design

- **Binding** `assets_dev/engine/models/items.yml` (schema `openyamm.itemModels.v1`): `{icon, model, length}` by the
  item's icon (`Pic File`), so items sharing an icon share a model. `length` is the world length of the model's
  longest horizontal side: the icon's opaque height in logical pixels x 0.9 (1.25 x the ground sprites they replace,
  for readability: longsword about 165 units, claymore 195, cutlass 116).
- **Presentation**: `IGameplayWorldRuntime::collectWorldItemPresentationStates` (outdoor `WorldItemState`s, indoor
  map-delta sprite objects carrying an item); `WorldFxSystem` keeps one static stand-in model per bound item lying
  flat on its position, turned by a per-item yaw; outdoors on the drawn terrain
  (`OutdoorTerrainMaterials::renderedHeight`, which includes the beach shaping) and tilted to its slope; indoors the models follow the portal-revealed sectors. The renderers
  skip the item's billboard; picking keeps the sprite-sized area; the hover outline goes to the model.
- **Model contract**: the resting pose is baked in (swords: hilt end raised 9 degrees, rolled 7), longest side along
  the model's horizontal Y, origin at the centre of its footprint with the lowest point at height 0.

## 5. Generation: icon relief (pilot: swords)

`level_generation/props/items_claude/` (README there): a Blender script turns an icon into a relief mesh: the
outline traced from the icon's alpha (marching squares, simplified), a constrained Delaunay triangulation with
interior points, a thickness from the distance to the outline (thin edges, a ridge along a blade, round grips),
front and back meeting at the outline; UVs straight onto the icon, which is the texture (colours dilated outward).
Templates differ in thickness and profile per family.

Pilot results (2026-10-16): 68 sword models, 93-478 triangles (median 218), 644 KB cooked, all installed and bound;
checked in New Sorpigal (beach and grass) and Agar's Laboratory (`review/swords_ingame_*.png` there). Each sword is
distinct and faithful to its icon where the sprites showed about four shapes. Lying flat at the sprites' size they
read less at a distance than the upright sprites and grass hid part of them, so at the user's request they are now
1.25 x larger and propped (hilt end raised on the guard).

## 6. Next waves

1. Swords (pilot, 68 items): done 2026-10-16.
2. Other weapons, wands, staves, bows (228 icons): done 2026-10-16 with the same relief, a prop per type (axes and
   maces lie on their heads, spears on theirs) and a family scale for wands.
3. Flat items: shields, armour (shell `dome`); cloaks and belts as solids projected from their oblique icons
   (`build_oblique.py` pile and loop) after the user's review; done 2026-10-16.
4. Everything else, done 2026-10-16: potions (lathe), rings, books, helms, boots, gold heaps (piles), scrolls,
   amulets, gauntlets, gems, ore and reagents, quest and misc items (reliefs). Helms, boots and gauntlets came from
   their own icons too (no per-family Meshy models needed so far).
5. Review on the test map; fixes per family; then retire the ground sprites (keep the native fallback).
