# Complete game font replacement: inventory and production workflow

Audited against the current workspace on 2026-09-08; SPELL integration updated on 2026-09-09.
This is an implementation guide and backlog; it does not start an asset-generation run. The inventory covers active game UI layouts and C++ font consumers, including MM9,
character creation, Arcomage, and the victory screen. It is based on the mounted development assets, rather than
assuming every font in the original games is currently used.

## What we need to create

**Ten outline faces cover the currently referenced game fonts. All nine active MM6–MM8 FNT faces are integrated: Arrus, Create, Lucida, SMALLNUM, Comic, Book2, AUTONOTE, ENDGAME and SPELL. One MM9 face remains.**

The active [faithful Arrus specimen](arrus_faithful/README.md) preserves the native design more closely, with
71 refined masters and 56 shared accented forms after the 2026-09-28 review; remaining symbols and ligatures
retain native traces. The previous AI reconstruction remains available
for comparison.
There are eleven active lookup names: nine FNT names and two MM9 PCX names. The two PCX names share one glyph design.
For MM6–MM8 presentation alone, all nine active FNT faces now have TTF replacements. Arrus still has a refinement backlog; reserved bitmap slots and the FNT metric dependency remain separate work.

Create one independently reviewed face per row. A shared family name is fine for packaging, but do not assume that
similar names or native heights mean that two assets should use the same outlines.

| Order | Runtime name / INI entry | Source under `assets_dev/` | Native height | Active uses and evidence | Status |
| --- | --- | --- | ---: | --- | --- |
| Refine | `arrus` | `engine/fonts/icons/ARRUS.FNT` | 19 | Main [dialogue body](../../assets_dev/engine/ui/gameplay/dialogue.yml), quick reference, inspection headings, character details, targeting prompts, Arcomage hints | [Faithful TTF active; 71 masters, 56 accented forms](arrus_faithful/README.md) |
| Done | `create` | `engine/fonts/icons/create.fnt` | 18 | Dialogue topics **and long-dialogue body fallback**; rest, save/load, menus, character creation, inspection titles, combat text | [Faithful TTF integrated; native/new UI comparisons](create_faithful/README.md) |
| Done | `lucida` | `engine/fonts/icons/Lucida.fnt` | 17 | [Character labels and skills](../../assets_dev/engine/ui/gameplay/character.yml), HUD labels, dialogue service rows, spellbook, mobile HUD | [Faithful TTF integrated; native/new UI comparisons](lucida_faithful/README.md) |
| Done | `smallnum` | `engine/fonts/icons/SMALLNUM.FNT` | 14 | HUD counters, chest/dialogue counters, actor/item/spell inspection bodies, character creation inspection | [Faithful TTF integrated; native/new UI comparisons](smallnum_faithful/README.md) |
| Done | `comic` | `engine/fonts/icons/COMIC.FNT` | 19 | [Item type, statistics, special effects and value](../../assets_dev/engine/ui/gameplay/item_inspect.yml), [Arcomage names and statistics](../../game/ui/screens/ArcomageScreen.cpp) | [Faithful TTF integrated; native/new UI comparisons](comic_faithful/README.md) |
| Done | `book2` | `engine/fonts/icons/Book2.FNT` | 30 | [Journal headings](../../assets_dev/engine/ui/gameplay/journal.yml), [Town Portal](../../assets_dev/engine/ui/gameplay/town_portal.yml), [Lloyd's Beacon](../../assets_dev/engine/ui/gameplay/lloyds_beacon.yml) | [Faithful TTF integrated; native/new UI comparisons](book2_faithful/README.md) |
| Done | `autonote` | `engine/fonts/icons/AUTONOTE.FNT` | 18 | Journal quest/story/note body via `JournalTextViewport` | [Faithful TTF integrated; black-ink presentation and journal comparisons](autonote_faithful/README.md) |
| Done | `endgame` | `engine/fonts/icons/ENDGAME.FNT` | 20 | [Victory certificate](../../game/ui/screens/WinGameScreen.cpp) | [Faithful TTF integrated; native/new certificates](endgame_faithful/README.md) |
| Done | `spell` | `engine/fonts/icons/SPELL.FNT` | 16 | Lloyd's Beacon available slots, saved location names and remaining durations in [GameplayPartyOverlayRenderer](../../game/ui/GameplayPartyOverlayRenderer.cpp) | [Faithful TTF integrated; shadow-free black beacon labels and native/new UI comparisons](spell_faithful/README.md) |
| 9 | `rudeblack`, `rudered` → one proposed `OpenYAMM Rude` face | `worlds/mm9/ui/dialogue/RUDEBLACK.pcx`, `RUDERED.pcx` | 15 | [MM9 RUDE dialogue](../../game/ui/GameplayDialogueRenderer.cpp): normal title/body/topics and hovered topics | Create one face; extend PCX replacement support |

The order prioritizes broad coverage. Create completes both body-font choices in normal
MM6–MM8 dialogue; replacing Arrus alone does not cover conversations that switch to Create when they are too long.
SMALLNUM is not a digits-only font: it contains upper/lowercase letters and is used for prose.

The Book2 screen review found an additional active font: `SPELL` is explicitly used for Lloyd's Beacon slot labels,
location names and durations. The earlier inventory incorrectly excluded it. Its pixels are 255 foreground / 0
transparent with no native shadow; its TTF now uses `presentation: tintable` after black-on-paper review in empty and occupied slots.

### Assets that do not require another face for the active UI

The FNT directories `engine/fonts/icons` and `engine/fonts/english_text` each contain 14 font names. Thirteen
same-name pairs are byte-identical; **LEGAL is the exception**. All nine active FNT names have identical copies in
both directories. The loaders search icons before EnglishT, so the icons copy is the reference for this inventory.

| Available FNT name | Icons height | Current audit result |
| --- | ---: | --- |
| `book` | 25 | No active font consumer found; different from the actively used `book2` |
| `calig` | 27 | No active font consumer found |
| `cchar` | 29 | No active font consumer found; current character creation uses Create/SMALLNUM |
| `legal` | 15 | No active font consumer found; EnglishT also has a different 14-pixel LEGAL font |
| `quick` | 20 | No active font consumer found; current quick reference uses Arrus |

Keep these five names in a later archival-coverage backlog. Recheck references before excluding them from a future
release with new screens or mods. If the goal becomes converting every stored source variant, audit both LEGAL
versions independently rather than overwriting one with the other.

MM9 `RUDEFONT.pcx` and `RUDEWHITE.png` are additional presentation assets, but the active renderer references
RUDEBLACK and RUDERED. The three PCXs have identical 835×15 dimensions and identical non-teal pixel masks; their
palette colors differ. They do not justify three independently generated typefaces. Preserve color behavior when
consolidating them.

The developer console and editor use ImGui's own font atlas. They are outside the legacy font switch and do not need
AI reconstruction to complete the game-font inventory. Text already painted into button/background textures is
also separate artwork: replacing these ten faces does not replace that baked lettering.

## Findings that affect the workflow

### AUTONOTE is a black-ink font, not an empty font

Its pixel stream contains only `0` and `1`. The visible letter shapes are the `1` pixels. Applying Arrus's
`pixel > 1` foreground extraction would produce a blank sheet. Use an explicit source policy (`1` is ink here),
record that policy with the font, and inspect a native specimen before generation.

The existing bitmap atlas places those pixels in its black shadow layer, whereas the journal requests black text.
A TTF replacement needs to render the actual letter mask in black with the intended offset and shadow behavior.
The normal TTF presentation synthesizes a shifted shadow; do not enable that extra layer for AUTONOTE.
AUTONOTE now uses baseline 15 and `presentation: black-ink`: the shared loader writes coverage to the existing
black layer at native coordinates, with no synthetic shadow. Journal comparisons cover quests, notes and paginated
MM8 story text. This preserves HUD and menu caller offsets without a font-name exception.

### MM9 needs a loader extension, not just an INI name

The [HUD loader](../../game/ui/GameplayHudCommon.cpp) currently handles column-separated PCX fonts on a separate path
that returns before FNT-backed TTF replacement. [FontAsset](../../engine/FontAsset.cpp) requires FNT bytes and a
`windows-1252` descriptor. Therefore adding `rudeblack,rudered` to today's INI does not convert them to TTF.

Export the exact PCX-derived layout metrics: 94 printable glyphs (`!` through `~`), one logical pixel of right
spacing, and the existing computed space width based on `E` (currently three pixels). Both strips contain 95
non-teal column runs; the current loader maps the first 94 to `!` through `~`. Review the remaining run explicitly
before assigning any additional character, and preserve the existing mapping during replacement.
Extend the shared font asset path to accept those authoritative metrics and the supported encoding without
requiring a fabricated FNT. Keep the metric model shared; do not add a second TTF rasterizer to MM9 dialogue.

Use one outline face and explicit normal/hover colors, retaining the public lookup names if needed by existing
consumers. MM9 currently draws the original colored texture directly, so merely supplying a white TTF atlas would
lose its black/red behavior. Keep MM9 descriptors/assets world-scoped and load them only when requested.

### Replacing typography is not yet removal of all bitmap glyph data

Current TTF rendering still reads the FNT for layout metrics, and reserved CP1252/control slots keep their native
bitmap images. For complete outline coverage, inventory any visible reserved-slot symbols and trace them into the
same face, with explicit internal glyph mappings (for example private-use Unicode mappings). Preserve real text
control semantics, including newlines, instead of converting every control byte into a printable glyph.

Eliminating the FNT dependency altogether is a separate migration: first export and validate authoritative metrics
in content metadata, then change the loader to consume them. It is not necessary to replace ordinary visible text.

## Recommended production process

### 1. Freeze each source and its metrics

Create a work directory per face, such as `tools/font_upscale/create_ai/`. Record the source path, full SHA-256,
first/last byte, encoding, palette/alpha semantics, and exact per-glyph left bearing, width, right bearing, advance,
ink bounds, and shadow bounds. Classify spaces, visible symbols, undefined bytes, and control codes explicitly.

Derive the baseline, cap height, x-height, and descender depth from representative glyphs and actual UI alignment.
Native cell height is recorded above; it is **not** enough to infer the baseline. Arrus's baseline 14 and 19-pixel
cell are specific to Arrus. Preserve source copies and fail a rebuild if the source hash changes unexpectedly.

Use the existing `parse_font` implementation in [unpack_xbrz_font.py](unpack_xbrz_font.py) as the starting point for
FNT extraction. Validate foreground policies independently for each face. Export nearest-neighbor source proofs
and real strings on their intended backgrounds before attempting restoration.

### 2. Make the existing builder reusable before processing the remaining FNTs

[prepare_dialogue_reference.py](prepare_dialogue_reference.py) and [build_dialogue_ttf.py](build_dialogue_ttf.py)
are working **Arrus-specific** tools. Their source/output paths, baseline, em size, sheet layout, and component
choices are hardcoded. Do not change their constants in place for each next font or run them expecting a generic
font-name argument.

Extract their shared extraction, tracing, font construction, and validation operations into a small configurable
pipeline. Give each face a manifest with source hash, ink policy, byte-to-Unicode map, measured metrics, sheet cell
maps, approved glyph inputs, component recipes, outline fitting rules, and output names. Keep Arrus as a reproducible
regression fixture. Implement PCX extraction as another source of the same metrics and masks.

The FNT path is now available as `build_native_font.py <manifest.json>`, backed by `font_pipeline.py`.
Create's manifest is the first configurable native restoration. Both historical Arrus builds use the extracted
assembly/validation operations and retain their original TTF hashes. AUTONOTE explicitly supports `pixel 1 is ink, pixel 0 transparent`; other profiles retain their original masks
and TTF hashes. PCX extraction remains future work; unsupported policies fail explicitly. Avoid implementing a large font editor or
multiple copied builders. A manifest and a few deterministic scripts are sufficient.

### 3. Establish the design with a small specimen

Work **per face**, using several manageable sheets within that face. Do not put unrelated families on one sheet.
Do not generate all 218 printable CP1252 characters independently: independent generations drift in weight,
proportions, and serif design.

Start with a 16–24-glyph pilot containing distinct structures: `H O A V M N S R a e n o s g i l 0 1 2 8 ? &`.
Use the original foreground-only sheet as the identity reference. Include a few actual UI words for visual review,
but keep specimen text out of the glyph-extraction cells. Review stroke weight, counters, joins, slant, serifs,
single-/double-storey letters, and legibility at native size before expanding the alphabet.

AI should supply a cleaner interpretation where pixelated outlines are ambiguous. For already clear geometric
strokes, simple punctuation, and repeated components, deterministic vector construction or careful tracing can be
more faithful than a new generation. Do not substitute an installed font merely because its name resembles an
asset name such as Lucida or Comic.

### 4. Generate a controlled set of sheets

Use the built-in imagegen tool by default, with one request per sheet or targeted correction. Inspect local
reference images before passing them to the tool. The installed imagegen skill governs tool usage; retain prompts
and selected generated outputs in the repository. Copy project inputs/results out of the tool's default generated
image directory so rebuilds do not depend on an agent's home directory. CLI/API generation is an explicit opt-in,
not an automatic fallback or a requirement for batching sheets.

A useful production split is a core alphabet/digit sheet, punctuation/component sheet, and special-glyph sheet.
Use fewer cells when the pilot shows that a denser sheet loses stroke detail. Supply both the original references
and approved same-face design references to later sheets. Give each cell an exact externally stored identity;
never infer Unicode from visual order or OCR alone. Blank unused cells must remain blank.

Prompt template (replace every placeholder with the actual face and exact cell map):

```text
Use case: infographic-diagram
Asset type: source glyph sheet for an outline game font
Primary request: Restore the supplied <face> glyph silhouettes into clean, readable lettering.
Input image 1: original glyph identity and proportions reference.
Input image 2: approved same-face style reference, if available.
Composition: orthographic <columns> by <rows> grid, one isolated glyph per cell;
             generous clear space around every glyph, including accents and descenders.
Text, row by row, verbatim: <explicit row strings corresponding to the saved cell map>.
Style: retain the reference's slant, serif structure, stroke contrast and letter construction.
Colors: flat solid black glyphs on uniform white; no texture, bevel, lighting, shadow or glow.
Constraints: preserve glyph identity and counter openings; do not substitute similar letters;
             no labels, grid lines, sample words, decorative marks or extra glyphs in cells.
Output intent: high-detail silhouettes for reviewed vector tracing, not a finished font file.
```

Numeric metrics come from extraction, not the model. Re-render just an incorrect glyph or small group when a sheet
fails review; preserve accepted glyphs. The Arrus run needed a targeted thorn correction because Þ/þ initially
resembled P/p. Explicitly check that failure pattern in every new face.

### 5. Build outlines and consistent components

Segment using the saved cell map. Inspect each cell before tracing; retain disconnected dots and accents, and do
not use a blanket small-component filter that deletes punctuation. Remove confirmed generation debris explicitly.
Trace with the existing Potrace/fontTools path, fit smooth curves with restrained simplification, and convert cubic
curves to valid TrueType quadratics. Check contour direction, counters, overlaps, degeneracies, and curve extrema.

Use shared reviewed bases to construct accented letters, with face-specific anchor placement. Remove the dot from
i when composing the corresponding accented forms. Treat ligatures, thorn, eth, sharp s, euro, fractions and other
non-decomposing characters explicitly. Keep straight and curly quotes distinct. Use ordinary space and nonbreaking
space with the source advances and no visible outlines.

Restore a coherent face while fitting the native layout contract. Preserve every character advance exactly. Maintain
common baseline and x-height; do not stretch each accent-bearing composite independently just to fill a bounding
box. Allow reviewed italic overhangs and slight curve overshoot, with sufficient texture padding. Arrus taught us
that rounding at accent extrema and reconstructed Ž can exceed the original bitmap ink rectangle.

### 6. Build a usable TTF, then validate its real raster output

Choose consistent em units for each face so `native_height` rasterizes to its measured native layout. Set family,
style, unique identifiers, Unicode cmap, horizontal metrics, ascent/descent, clipping bounds and glyph order
correctly. Keep provenance and version metadata. Do not add kerning or automatic ligature substitutions that alter
legacy wrapping unless the layout contract is deliberately changed in separate work.

Treat the existing printable Windows-1252 coverage as the compatibility target for FNT fonts. Do not equate
Latin-1 with Windows-1252: smart punctuation, euro and additional letters need the explicit mappings. MM9 currently
uses printable ASCII; do not invent legacy source metrics for characters absent from its strip. Further language
coverage needs its own content and text-encoding plan.

Validate all mapped glyphs through both fontTools and FreeType. Check missing/empty visible glyphs, valid contours,
exact advances, combining components, clipping, and raster bounds including shadow padding. Test at native height,
intermediate sizes, 2× and 4×, on dark HUD and light paper backgrounds. Inspect `Il1`, `O0`, `rn/m`, punctuation,
accents, numbers, and actual long sentences. Native-size legibility is an acceptance criterion, not just a large
specimen's appearance. Evaluate small-size hinting only against the real runtime rasterizer: it currently requests
unhinted grayscale outlines, so adding TTF hints alone will not change in-game rendering.

Save the selected sheets, editable outlines/component recipes, TTF, cell map, source hashes, per-glyph provenance,
metrics/coverage report, full glyph proof, and native/TTF sentence comparisons. Rebuilding must not invoke imagegen.

### 7. Integrate one face at a time

For ordinary FNT-backed faces, place the TTF and descriptor in `assets_dev/engine/fonts/truetype/`.
A descriptor uses the existing fields demonstrated by [arrus.yml](../../assets_dev/engine/fonts/truetype/arrus.yml):

```yaml
file: openyamm_dialogue_italic.ttf
logical_height: 19
baseline: 14
encoding: windows-1252
```

Those values are the working Arrus example, not defaults for the next face. Use a separate descriptor named after
each runtime font, its measured height/baseline, and its actual TTF filename. AUTONOTE uses the implemented `presentation: black-ink` descriptor field; omitted presentation retains
`tintable-with-shadow`, and unknown values fail explicitly. ENDGAME uses `presentation: tintable` to omit the
synthetic shadow while retaining caller-controlled ink color. SPELL uses the same policy after its own source and
Lloyd’s Beacon UI review. MM9 still needs PCX replacement support.

Since 2026-10-15 every legacy face is drawn from its replacement: there is no font setting (`[fonts] prefer_ttf`,
`ttf_fonts` and the `[video_quality] fonts` tier are gone) and no bitmap rendering path. `engine/fonts/english_text` keeps
only the FNTs of the faces with a descriptor (arrus, create, lucida, smallnum, comic, book2, autonote, endgame,
spell): each supplies its face's layout metrics and reserved symbol glyphs. A face referenced by the UI must have a
descriptor; a missing or broken one reports an error. The MM9 RUDE dialogue fonts stay PCX bitmaps until their
replacement is implemented and checked.

### 8. Verify layout, appearance and cost in the actual game

Use the same save/state, viewport and UI scale for before/after captures. Compare word wrapping, panel height,
truncation, text alignment, cursor placement, hover hitboxes, colors, shadow offsets and accents. Cover each row:

| Face | Minimum visual checks |
| --- | --- |
| Arrus + Create | Short dialogue and long dialogue that switches to Create; service topics; spell targeting |
| Create | Character creation, rest, load/save and a dense inspection popup |
| Lucida | Character stats/skills, spellbook, desktop and mobile HUD |
| SMALLNUM | Counters and paragraph-length item/spell/actor descriptions at the smallest supported UI size |
| Comic | Item description and Arcomage names, resource numbers and statistics |
| Book2 | Journal title and both travel spell screens |
| AUTONOTE | Multiline quests, story and notes on paper; black ink and scrolling/page boundaries |
| Endgame | Victory certificate, including long names and score/date lines |
| Spell | Lloyd's Beacon empty/occupied slots, location names and durations; black ink without added shadow |
| RUDE | MM9 normal/hovered dialogue, wrapping and topic clicking; MM6–MM8 with MM9 absent |

Build and run focused checks when implementation or font assets change:

```sh
cmake --build build --target openyamm -j25
cmake --build build --target openyamm_unit_tests -j25
./build/tests/openyamm_unit_tests --test-case='font *'
```

Extend [FontAssetTests.cpp](../../tests/FontAssetTests.cpp) to cover each new font's metrics, coverage and special
source policy. Add meaningful renderer/PCX/color tests when implementing those branches. A headless scenario alone
cannot prove font appearance because it may skip GPU rendering. Use actual graphical captures; the existing
[TTF](dialogue_ai/output/runtime_ttf.png) and [bitmap](dialogue_ai/output/runtime_bitmap.png) Arrus captures show the
required comparison. For Android validation, use a release build.

Measure cold font raster/upload time, atlas dimensions, CPU/GPU bytes including tinted copies, and warm rendering
cost. The current TTF path rasterizes at 4×, has five logical pixels of padding, and caches textures. Allocation
therefore scales with `(cell_width + 10) × (height + 10) × 16 × 16 × 4² × 4` bytes per BGRA atlas, before shadow and
tinted copies. Enabling every face can materially increase memory; do not assume a small TTF file means a small
runtime atlas. Keep rasterization off the per-frame path. Tighten packing or cache policy only when measured needs
justify it, preserving filtering gutters and glyph extrema.

### 9. Package and finish

Include each TTF and descriptor in the corresponding runtime content package as well as development assets. Check
packaged lookup, case-sensitive names and the original bitmap option. Keep MM9 content world-scoped. Record the
validated settings, source/version hashes and screenshots in each face's README.

A face is complete when coverage, metrics, outline validity, native-size readability, actual UI behavior, cache
behavior and packaged loading all pass. For complete removal of visible bitmap font glyphs, also finish the reserved
symbol mappings and PCX integration; do not call the task complete merely because every ASCII letter looks smooth.

## Inventory maintenance

When UI layouts or screens change, repeat the audit over `assets_dev/engine/ui`, mounted world UI directories, and
all game font consumers. Check YAML `text.font`, literal and constant C++ font names, fallback fonts, MM9 paths and
non-game overlays separately. Resolve names case-insensitively, then compare source bytes and extracted masks before
deduplicating. Assets present on disk are not evidence that they are used.

The key implementation references are [FontAsset](../../engine/FontAsset.cpp),
[GameplayHudCommon](../../game/ui/GameplayHudCommon.cpp), [MenuScreenBase](../../game/ui/MenuScreenBase.cpp),
[GameplayDialogueRenderer](../../game/ui/GameplayDialogueRenderer.cpp), and the per-screen layouts linked above.
