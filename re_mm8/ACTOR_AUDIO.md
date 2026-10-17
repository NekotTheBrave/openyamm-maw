# MM8 actor sounds and OpenYAMM implementation

Investigated and implemented 2026-09-29. **The five main audio fixes are implemented**, together with
verified Shrink pitch, resurrection's death-slot request and the special pursuit hit-slot request.
The original findings and pre-change comparison remain below; see the implementation and validation record
for the selected playback policy.

This covers the four vocalization slots of map actors, including friendly NPC actors. House greetings,
party speech, footsteps, and spell/projectile impact effects have separate triggers and are outside this audit.

## Evidence and scope

- Primary evidence: Rizin 0.10.0 disassembly of the GOG-verified `mm8_orig/MM8-Rel.exe`.
- SHA-256: `5326d56459ca74126447bcfd07a8cc85a419663e2281500f0f043f13ffe77a0b`.
- Local reference: OpenEnroth checkout `d98839ba8b9afde4df987150de7bcc9c32d9ecee`.
- OpenYAMM: the current working tree, including existing uncommitted changes, inspected on the date above.
- [Rizin annotations](mm8_rel_5326d564_actor_audio.rz) preserve the original-executable addresses.
  Rz-ghidra pseudocode helped navigate; branch conditions, constants and sound-call arguments were checked
  against disassembly. Function names in the annotations are research labels.
- This is static analysis. No original-MM8 listening session or live GrayFace/MMerge hook inspection was done.
  The Miles 3D provider's internal attenuation implementation was not reverse engineered. These findings
  describe this executable's disk code, not every patched installation.

No OpenEnroth code was copied. The implementation uses shared OpenYAMM gameplay/audio systems.

## Original MM8: what requests a sound

The common actor helper is `0x402e43`. Actors begin at `0x61c458`, have stride `0x3cc`, and store four
16-bit sound IDs at offset `0xd4`. The helper skips a zero sound ID and sends the selected sound to
`0x4aa8b7` with the actor identity `(actorIndex << 3) | 3`.

| Event | Slot | Original trigger and restrictions | Evidence |
| --- | ---: | --- | --- |
| Attack | 0 | Once when entering melee, ranged-1, ranged-2, spell-1 or spell-2 attack state. It does not wait for impact or require a successful hit. The starters check LOS to their actual target; failed LOS leads to pursuit instead. The target can be another actor. | Starters `0x403f4e`, `0x40373b`, `0x403953`, `0x403b2c`, `0x403d3d`; sound calls `0x4040d4`, `0x4038d3`, `0x403aad`, `0x403c86`, `0x403e97` |
| Got hit / wince | 2 | On entering the stunned/hit-reaction state. Unless explicitly forced, an actor already stunned or in any of the five attack states suppresses both the reaction and its sound. A damage attempt is not itself a sound trigger. | `0x40332f`; state guards `0x40339d` onward; sound call `0x403433` |
| Death | 1 | On entering Dying, alongside setting HP to zero and starting the dying animation. The later Dying-to-Dead transition is silent. | `0x402ec9`, sound call `0x402f44` |
| Resurrection | 1 | The resurrection routine also requests the death slot while using the dying animation. This is an original oddity, not an inferred new sound type. | `0x4031a6`, sound call `0x4031f9` |
| Bored / fidget | 3 | After accepting a front-facing fidget action, one `rand() % 100 < 5` roll. Failure of the facing test chooses Stand instead, without a sound roll. | `0x403207`; facing test `0x4032ca`; sound call `0x40331c` |
| Ordinary wandering | 3 | A separate `rand() % 100 < 2` roll after committing a walking action. Early returns that only stand do not reach it. Does not require the fidget animation or its facing test. | `0x403537`, sound call `0x403728` |
| Special pursuit, analogous to OE `AI_Pursue3` | 2 | A separate 2% roll after committing the pursuit action, using the **hit** slot. This is another verified original oddity. | `0x4027d6`, sound call `0x402957` |

These percentages are **per action decision**, not per frame, second, or animation frame. The sprite's
animation duration controls the fidget/hit/death action timer. There is no independent global periodic
"make all nearby monsters speak" timer in the actor sound helper.

### Fidget orientation and scheduling

MM8's bored routine first assigns the requested target yaw at `0x40325c`, then compares that yaw with
the actor-to-camera bearing. In its 2048-unit angle system, the test is:

```text
(actorYaw + 1024 + 128 - cameraToActorAngle) & 0x700 == 0
```

This selects the front viewing octant, approximately a 45-degree sector. It is a test of the actor's facing
toward the camera, not a test that the party is looking at the actor. It neither tests the view frustum nor
casts an audio occlusion ray. Camera XY is read from `0x519380/0x519384`.

`StandOrBored` at `0x40424b` makes a 50/50 stand/bored choice; the bored branch then applies its own facing
and 5% sound tests. Background actors also use this route when their action expires (`0x401e9d`). The
background loop skips dead, removed, disabled and full-AI actors; petrification/paralysis suspend ordinary
action advancement. Turn-based idle paths call the same bored helper (`0x407164`, `0x407b8e`) after a
50/50 decision, retaining its facing and sound rules.

Friendly/nonhostile actors use the same slots and playback helper. The helper has no "hostile to party"
requirement. Attack eligibility and the actor's current state determine whether an attack is started;
they do not make a separate friendly-versus-hostile hearing rule.

## Original MM8: when the requested sound is audible

Sound generation, AI activation and audio playback are separate decisions. A nonzero sound slot is not
enough: the audio subsystem must be enabled and initialized, the sound record/data must exist, the source
must satisfy the selected mixer path, and a suitable channel must be available.

### Ordinary Miles sample path

`0x4abc78` computes distance from **actor feet to party feet**, including Z. It calls the integer
approximation at `0x46155d`, not Euclidean length. For sorted absolute coordinate differences `a >= b >= c`:

```text
d = a + floor(11*b/32) + floor(c/4)
volume = 114 - trunc(100*d/8192), if d <= 8192
volume = 0,                    if d > 8192
```

Thus **8192 is still audible**, at volume 14; the cutoff is strictly beyond it. The formula is the
positional channel volume before other mixer/output gains. The binary's constants are `100.0` at
`0x4ea968` and `1/8192` at `0x4ea970`.

Actor playback selects channels **0 through 3**, a four-voice actor budget (`0x4ab023`). An existing
playing sound on the same actor is handled before allocating another channel:

- Same actor and same sound: suppress the duplicate; do not restart it.
- Same actor and a different sound: stop the old sound, then try the new one.
- Different actors may play the same sound simultaneously, subject to the channel budget.
- If no actor channel is free, a quieter existing actor channel can be replaced. If none has a strictly
  smaller volume, the new sound is dropped. There is no combat/ambient priority classification here.

`0x4abc33` pans relative to the **party yaw**. The periodic audio updater at `0x4ab58b` resets its counter
to 32 timer ticks, fetches the actor's current position again, updates pan/volume and stops out-of-range
sounds. Source position is not permanently frozen at the attack/fidget event position.

There is **no additional audio LOS, wall/sector occlusion, frustum, party-facing or hostility check** in
this path. Attack LOS and fidget orientation can prevent the event from being generated, but a sound
already requested is not independently tested for visibility through walls.

### Optional Miles 3D provider path

`0x4aa8b7` uses the 3D branch only when a 3D provider is active and the sound record has flag `0x2`.
Do not assume that every installation or sound takes this branch.

- Source coordinates come from the actor's feet, transformed relative to the view, then scaled by
  `100/8192`. Indoor transformation is `0x4ab462`; the outdoor helper is `0x4348db`.
- The executable calls `AIL_set_3D_sample_distances` with maximum 100 and minimum 20. In world scale
  these correspond nominally to **8192** and **1638.4** units.
- It submits two transformed coordinates with the third provider coordinate fixed at zero. Its own
  range cleanup uses approximate length of truncated transformed coordinates, with a `> 100` test.
  This is not an exact 8192-unit Euclidean sphere; pitch and quantization matter.
- Actor channels are 0 through 3 with at least 16 provider channels. With fewer channels, the actor
  range is 0 through `floor(3 * channelCount / 16)`, inclusive.
- Same-actor duplicate/replacement rules remain. Under channel pressure it compares source distances
  and can replace a farther source for a closer newcomer. Ongoing playback tracks moving actors.
- No audio geometry/occlusion test was found in this branch either. The provider's precise gain curve
  is outside the evidence gathered here; do not apply the ordinary-sample volume formula to it.

### AI ranges are not hearing radii

The outdoor full-AI candidate range is **5632** units (`0x40151a`), using approximate distance minus
actor radius, clamped at zero. The indoor distance candidate pass uses **10240** (`0x40172e`), then
detection/cached flags and sector-based additions affect selection. Both final full-AI lists cap at
**30 actors**. These are scheduler rules, not a universal sound condition.

In particular, the map-wide background action loop can still request bored sounds outside the outdoor
full-AI radius. Conversely, an indoor actor can be eligible for full AI while outside the ordinary
mixer's audible range. Do not gate all sounds using the full-AI list, nor rewrite AI ranges to fix audio.

### Indoor sectors do not have to be visited for background voices

For the full portal-rendering, minimap discovery and actor-selection investigation, see
[Indoor exploration and actor activation](INDOOR_EXPLORATION_AND_ACTIVATION.md).

Follow-up verification: ordinary real-time background fidgets have **no visited-sector or activated-sector
prerequisite**. Indoor full-AI selection checks actor detection/cached flags (`0x40194c`–`0x40198b`) and
adds actors in the party's current sector (`0x4019b7`–`0x4019e9`). These checks select the full-AI list;
they do not suppress the separate background loop.

After indoor selection returns, `0x401c9a`–`0x401eb9` traverses the entire actor array. Its entry guards
skip Dead, Removed, Disabled and actors selected for full AI (`0x401ccb`–`0x401cfc`). It expires buffs,
pauses ordinary action advancement for paralysis/petrification (`0x401de3`–`0x401e18`), and calls
StandOrBored on eligible action expiry (`0x401e94`–`0x401e9d`). This path does not read sector visitation,
sector visibility or actor detection flags, and does not perform an LOS check.

Consequently, an eligible actor in an unexplored room can produce an audible fidget through walls,
subject to the fidget decision, facing/sound roll, mixer distance and available actor channels. A map
script can still keep an actor explicitly disabled/removed until an event enables it; that is a separate
actor-state restriction. Turn-based mode uses its own scheduler rather than this real-time loop.

The current OpenYAMM indoor AI collector likewise sends actors outside the full-AI selection through
shared background AI without consulting its sector-activation mask. The remaining uses of that mask
in collision and area-spell queries are outside this audio finding.

## Comparison with local OpenEnroth

Behavioral reference: [Actor.cpp](../reference/OpenEnroth-git/src/Engine/Objects/Actor.cpp),
[AudioPlayer.cpp](../reference/OpenEnroth-git/src/Media/Audio/AudioPlayer.cpp),
[AudioSamplePool.cpp](../reference/OpenEnroth-git/src/Media/Audio/AudioSamplePool.cpp), and
[OpenALSoundProvider.h](../reference/OpenEnroth-git/src/Media/Audio/OpenALSoundProvider.h).

| Area | Local OE versus original MM8 |
| --- | --- |
| Attack, hit, death triggers | Same broad state-entry behavior, including suppression of ordinary hit reactions during attacks/already-stunned state. |
| Bored and wandering | Preserves 5% bored, 2% wandering, and 50/50 StandOrBored. Uses separate visual/game RNGs. |
| Fidget facing order | OE tests the previous yaw, then assigns target yaw in the successful bored branch. MM8 assigns target yaw before testing. This can change whether the same actor fidgets. |
| Pursuit/resurrection quirks | Preserves pursuit-3's 2% hit-slot call and resurrection's death-slot call. |
| Range/attenuation | OpenAL parameters: reference distance 300, maximum distance 60000, rolloff 1.5. No explicit MM8-style 8192 rejection in the actor playback path. The maximum-distance parameter alone should not be described as a hard mute radius. |
| Overlap | Actor sounds use `playUniqueSoundId`: an already-playing sound ID suppresses another use of that ID across actors. This differs from MM8's per-actor duplicate policy. |
| Direction/source updates | Listener yaw/pitch and position update. Actor position is supplied at sound creation; pool updates only remove stopped samples, so ordinary actor one-shots do not follow the moving actor. |
| Shrink | The actor helper has an explicit TODO; all Shrink branches play at ordinary pitch. |

OE is a useful behavioral reference, not an exact MM8 audio specification. Its source comments containing
legacy function addresses are not addresses in the selected MM8 executable.

## OpenYAMM before this implementation

This section records the pre-change problems, all addressed by the implementation below.

Relevant implementation: [shared actor AI](../game/gameplay/GameplayActorAiSystem.cpp),
[indoor runtime](../game/indoor/IndoorWorldRuntime.cpp),
[outdoor runtime](../game/outdoor/OutdoorWorldRuntime.cpp),
[game audio](../game/audio/GameAudioSystem.cpp), and [engine mixer](../engine/AudioSystem.cpp).

### Confirmed correctness problems

1. **Actor-versus-actor hit/death audio is predicted before combat resolution.** The
   `ActorMeleeImpact` branch in shared AI chooses Hit or Death by comparing raw attack damage with target HP.
   The actual indoor/outdoor attack resolver subsequently performs hit and resistance checks; the indoor
   path also applies damage event hooks. Neither corrects that prediction. The outdoor path explicitly
   passes `emitAudio = false` when applying the resolved damage. A miss can therefore produce a hit/death
   sound, and a target surviving reduced damage can produce a death sound.
2. **Hit audio and hit-reaction eligibility are disconnected.** Several damage paths guard the animation
   with `canEnterHitReaction`/`beginMapActorHitReaction` but request `winceSoundId` afterward regardless of
   whether the reaction began. Attacking/already-stunned targets can make sounds the original suppresses.
3. **Stereo direction ignores listener orientation.** `AudioSystem::calculateStereoGains` uses
   `clamp((sourceX - listenerX) / 2048, -1, 1)`. Listener state contains XYZ but no facing. Turning the party
   does not rotate the sound field.

### Missing or different original behavior

- `AI_Bored` contains a 5% sound request, but ordinary `resolveIdleBehavior` initializes `bored = false`
  and never sets it true. Its `AI_RandomMove` bored branch is consequently not reached by those decisions.
  Crowd-standing, background presentation and turn-based waiting can select a Bored animation directly
  without requesting the sound. Adding another percentage only inside `AI_Bored` would not fix this.
- The two walking starters have no separate 2% bored-sound roll. The original front-octant check is also
  absent from `AI_Bored`. Current idle timing/selection is not the original StandOrBored behavior.
- Normal actor sounds use the generic Euclidean attenuation `1 / (1 + (distance / 768)^2)`, with silence
  at `distance >= 12000`. This drops much faster nearby than MM8's ordinary sample curve while allowing
  a farther tail. Actor source Z is usually feet plus half height, unlike MM8's foot position.
- World sounds create independent mixer instances. There is no actor ownership, per-actor duplicate
  suppression, replacement of an actor's old voice, or four-channel actor budget. Actor identity does
  not reach the ordinary one-shot playback API. Unrelated world effects share this generic path.
- One-shot source coordinates are captured at creation; they do not follow moving actors.
- Resurrection does not request the death slot. The special pursuit hit-slot roll is absent.
- No Shrink-dependent actor pitch is applied. MM8's helper contains explicit playback rates **33075**,
  **44100**, and **55125** for powers 2, 3, and 4. Its other-power path and alternate playback arguments
  are irregular; those quirks need separate verification before claiming complete Shrink parity.

## Implemented behavior and deliberate choices

| Area | Implemented behavior |
| --- | --- |
| Resolved hit/death | Shared melee intent no longer predicts victim sounds. Indoor/outdoor damage resolvers emit from accepted hit/death transitions after hit checks, resistance and damage hooks. Ordinary zero-damage attempts are silent; attacking/already-stunned actors suppress ordinary reactions. Dying-to-Dead is silent. Action-entry requests are queued before resolved damage voices, so a same-frame attack cannot replace death audio. Explicit forced reactions can bypass only the stun/attack guard, never revive an unavailable actor. |
| Stereo | The engine listener carries yaw. Positional pan uses horizontal bearing relative to the listener's right vector; turning rotates all positional effects. Front/back sources remain centered. |
| Fidgets | Ordinary, nearby friendly, background, crowd-standing and turn-based idle paths use shared facing/voice rules. StandOrBored has a separate 50% decision; accepted front-octant fidgets get a 5% voice roll and their sprite animation duration. Requested yaw is tested after being assigned. Holding an action does not roll again. |
| Wandering | A newly committed ordinary or home-return walk gets an independent 2% bored-slot roll. Continuing a walk or taking a stand-only branch does not. |
| Ownership | GameAudioSystem owns four actor voices. Same actor/same sound is suppressed without restarting; another sound replaces that actor's voice. Different actors may share a sound. A full budget admits a newcomer only by replacing a strictly quieter voice, choosing the quietest eligible voice. |
| Movement and lifetime | Each gameplay frame refreshes playing voices from actor feet and party feet, including frames with no new requests. Missing/removed actors, live actors made invisible and sources outside range stop playing. A dead actor that turns invisible (no corpse, or a looted one) keeps its death voice to the end. Scene teardown, map sound preload and stop-all clear actor ownership. |
| Distance | Actor voices use the ordinary MM8 integer distance/volume rule above, normalized by Miles' maximum channel volume 127 before OpenYAMM's effects/master gains. Distance 8192 remains audible at 14/127; greater integer distance is silent. The generic engine attenuation is bypassed for these voices. No AI activation, LOS or frustum test is added. |
| Shrink | Active powers 2/3/4 use pitch ratios 1.5/2/2.5. Other powers retain ordinary pitch. Ratios preserve the verified native 22050 Hz behavior after mixer resampling and also give predictable relative pitch for replacement assets. |
| Original quirks | Successful resurrection requests the death slot. Only the special OffsetWide/Pursue3 starter gets the independent 2% hit-slot request; ordinary pursuit does not. |

The playback policy is deliberately the **ordinary sample path**. OpenYAMM does not emulate the optional
Miles 3D provider or its undocumented gain curve. Panning is smooth listener-relative bearing rather than
legacy angle quantization, and source tracking runs each gameplay frame rather than every 32 legacy ticks.
Unrelated positional effects retain their existing attenuation policies.

Fidget/wander/pursuit rolls use distinct deterministic hashes of actor identity and action decision count.
This preserves reproducibility and independent probabilities without perturbing combat RNG or claiming the
same random sequence as MM8. Background StandOrBored faces the party on each new decision, as verified
at `0x401e5e`–`0x401e9d`; it tests that requested yaw rather than the previous facing. AI activation ranges
are unchanged.

The shared rules live in [ActorAudioRules.h](../game/audio/ActorAudioRules.h),
[ActorVocalizationRules.h](../game/gameplay/ActorVocalizationRules.h), and
[AudioSpatialization.h](../engine/AudioSpatialization.h). World runtimes supply actor identity, position,
Shrink state and resolved combat transitions through their existing audio queues.

### Shrink verification

The verified executable's actor helper selects rates 33075/44100/55125 at `0x402e87`, `0x402e8e` and
`0x402e95`. The ordinary mixer passes the supplied rate to `AIL_set_sample_playback_rate` at
`0x4ab433`–`0x4ab43e`. The optional 3D branch does not use this rate; it reuses the argument's stack slot
as scratch at `0x4aac3d`. An unsupported Shrink power can read an uninitialized local at `0x402e9c`,
and the Shrink branch also changes an ancillary playback argument from -1 to 0. Neither irregularity
is reproduced.

A RIFF-header scan of 467 mounted WAV files with attack/wince/fidget/death-style names found 22050 Hz
throughout, including `pirate_warrior Attack.wav`, `pirate_warrior Death.wav`, `01archera_attack.wav`
and `zomdie_wince.wav`. Thus the implemented pitch ratios match the verified rates for native actor clips.

### Validation

- Normal `openyamm` and `openyamm_unit_tests` targets build; no Vulkan build is involved.
- Focused actor AI/service, fidget, distance, panning, pitch and ownership tests: **57 cases, 432 assertions pass**.
- All audio unit regressions: **26 cases, 28475 assertions pass** (overlaps the focused selection).
- Headless indoor/outdoor `actor_audio` cases verify repeated-hit suppression, suppression during attacks,
  one death request, feet placement and resurrection. The indoor case verifies damage hooks changing
  a lethal attempt to zero or one damage before audio selection. Both world hooks exercise turn-based
  fidget entry, and the outdoor case checks a background actor at 6000 units without repeated per-frame rolls.
- Older wandering/combat fixtures placed actors 6000 units from the party despite the existing 5632-unit
  full-AI range; affected fixtures now use 3000. Idle checks permit the newly restored Bored animation.
  All eight selected friendly-actor cases and the hostile actor-pair combat case pass. The inactive-death
  check now rejects AI-directed walking while allowing the already implemented corpse knockback/gravity;
  it also passes. In total, the selected headless checks pass all **12 cases**.

Reproduce the focused checks with:

```bash
cmake --build build --target openyamm openyamm_unit_tests -j25
./build/tests/openyamm_unit_tests --test-case='*actor audio*,*actor fidget*,*positional audio*,*shared actor AI*,*shared actor service*'
./build/tests/openyamm_unit_tests --source-file='*AudioRegressionTests.cpp'
OPENYAMM_REGRESSION_FILTER=actor_audio ./build/game/openyamm --headless-run-regression-suite indoor
OPENYAMM_REGRESSION_FILTER=actor_audio ./build/game/openyamm --headless-run-regression-suite audio
```

No subjective listening comparison was performed. Volume balance and the chosen stereo/pitch presentation
still warrant listening on the user's normal audio setup; automated checks establish the event and mixer rules.

## Reproduce the binary checks

The current host has the repository-built Rizin binary rather than the guide's former `~/.local` install:

```bash
sha256sum re_mm8/mm8_orig/MM8-Rel.exe
third_party/rizin/build/binrz/rizin/rizin -N -q -e scr.color=0 \
  -p re_mm8/work/mm8-rel-5326d564-base.rzdb \
  -i re_mm8/mm8_rel_5326d564_actor_audio.rz \
  -c 'pdf @ 0x402e43;pdf @ 0x403207;pdf @ 0x40332f;pdf @ 0x4abc78;pdf @ 0x4aa8b7'
```

Create the cached project with the [Rizin guide](RIZIN_GUIDE.md) if absent. Opening without `-w` keeps the
executable read-only. Large scratch exports live in ignored `re_mm8/work/actor_audio/`; the evidence
addresses and conclusions above do not depend on retaining those exports.
