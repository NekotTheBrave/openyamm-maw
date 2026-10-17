# Original MM8: friendly wandering in confined spaces

Investigated with Rizin and rz-ghidra on 2026-09-14. Target `mm8_orig/MM8-Rel.exe`, freshly verified SHA-256
`5326d56459ca74126447bcfd07a8cc85a419663e2281500f0f043f13ffe77a0b`. Addresses are preferred virtual addresses
in this original executable. No patched binary or original-game runtime session was used.

The shared wandering routine and the physical collision response explain the behavior. No balcony-size test,
free-space classification, geometry-aware random destination selection, or generic wall-stuck timeout was found
in the traced chain. A specific balcony may have authored collision faces or stationary actors; no particular
balcony's map data was identified by this request.

| Location | Verified purpose |
| --- | --- |
| `0x403537` | Choose a timed, home-biased wandering action |
| `0x403674–0x403687` | Random pause branch: `rand() % 100 < 25` |
| `0x404153` | Stand awhile: zero velocity, reset elapsed time, duration 128–255 ticks |
| `0x4036a0–0x4036cb` | Large heading-change test can choose standing instead |
| `0x4036d2–0x403709` | Store heading, walking duration and AI state 1 |
| `0x4020b2` | Advance action elapsed time using the timer delta |
| `0x402187–0x402194` | Keep the current action when its duration has not expired, on the applicable AI path |
| `0x402696–0x402725` | Movement-category dispatch into wandering or standing |
| `0x4711d7–0x47132a` | Outdoor solid-face collision response changes velocity and heading |
| `0x471449–0x471490` | Outdoor actor-contact response distinguishes hostility and crowding |
| `0x404323` | Stop and face an object, with a possible bored-animation branch |
| `0x47025f–0x47029f` | Indoor grounded-walker drop guard |

Wandering uses the initial/home XY fields at actor offsets `+0xaa/+0xac`, current XY at `+0x96/+0x98`, heading
at `+0xa2`, speed at `+0x94`, duration at `+0xa8`, elapsed time at `+0xc0`, and AI state at `+0xb8`.
The routine computes a cheap approximate distance to home:

`D = max(abs(homeX - x), abs(homeY - y)) + min(abs(homeX - x), abs(homeY - y)) / 2`

It adds `R * (rand() & 15) / 16`, where R is selected by the movement category. The real-time caller uses 1024,
2560, 5120 and 10240 for category values 0, 1, 2 and 4, respectively. The corresponding structural names in the
local reference are Short, Medium, Long and Free. Category 5 selects standing. These values are wandering
parameters, not measured balcony dimensions or a hard maximum allowed displacement from home.

The heading is toward home plus a random offset of -128 through +127 angle units, about ±22.5 degrees on the
2048-unit circle. Before accepting a walk, a 25% branch chooses standing for 128–255 ticks, zeroing all three
velocity components and resetting elapsed time. Another branch stands for 256 ticks when the raw absolute
heading difference exceeds 256 units and actor attribute bit `0x200000` is clear. This comparison is not an
angular-wrap-aware shortest-distance calculation. The routine also has special monster-category and movement-
category checks, so the pause probability is not a universal percentage of all actor updates.

On the accepted walk branch, duration is integer `32 * adjustedDistance / moveSpeed` ticks, or zero for zero speed.
It resets elapsed time, stores the heading, selects AI state 1 (the reference names it Tethered), and updates the
animation. It does not save a collision-validated destination that must eventually be reached. Duration is based
on the initial calculation, not accumulated successful displacement.

The AI dispatcher increments elapsed time independently of physical progress. For ordinary noncombat action
continuation, it skips choosing another action while elapsed time is below duration. Therefore failure to make
physical progress does not itself keep extending the same wander action. Once it expires, the actor can choose
another walk or a pause; consecutive accepted walks need not have an idle interval between them. Combat and
other state transitions can interrupt the ordinary path.

For a lone friendly NPC on a small outdoor balcony, distinguish the actual obstacle:

| Situation | Behavior established by the traced code |
| --- | --- |
| Solid wall or collidable railing | Shorten travel to the hit, adjust velocity against the face normal, apply the relevant positional push-out, realign heading, damp velocity and retry remaining movement. No wall-specific standing-state assignment occurs in this response. |
| Corner or space too small for the actor | Repeated collision response can consume the movement allowance or retry limit. The actor can make little progress while the walking action remains active. There is no guaranteed escape. |
| Another nonhostile actor | In the ordinary real-time single-contact branch, stop and face the other actor; a small random branch chooses the bored behavior instead. |
| Multiple counted actor collisions | A separate stand-or-bored response is available. This counts actor collision helper results in the current collision pass, not time spent stuck against geometry. |
| Open outdoor edge with lower terrain below | No indoor-style 100-unit drop guard is present. Once support is lost, the nonflying actor can fall. Friendliness does not add an outdoor edge-stop check here. |
| End of the wander action's time | Re-enter action selection; another wander or an idle action can follow, regardless of whether the intended motion succeeded. |

For solid-face contact, the outdoor velocity correction is approximately
`velocity += normal * max(abs(dot(velocity, normal)), speed / 8)`, with fixed-point arithmetic. For the applicable
non-slope response, heading is recomputed from the corrected horizontal velocity at `0x471325–0x47132a`.
The walking animation normally reconstructs movement from that stored heading at the next update. This allows
continued movement along or away from a wall; it also explains why an actor may remain walking while blocked.
The generic face response does not require hostility to be zero and does not contain a progress-based action cancel.

The friendly-contact branch is more explicit. The moving actor's hostility byte is checked at `0x471449`; when
there is no crowd response, the other actor's hostility byte is checked at `0x471480`. If both are zero, the call
at `0x471490` goes to `0x404323`. Its usual branch faces the hit object, clears XYZ velocity and elapsed time,
sets duration 256 ticks, and assigns AI state 10. It has a `rand() % 100 < 5` bored-animation alternative.
The crowd boolean is computed from more than one successful actor-collision helper result at `0x470fa1–0x470fa8`.
These collision reactions have turn-based-stage exceptions; they are not unconditional in every game mode.

Indoors, the same wandering policy runs over a different movement solver. A grounded, nonflying walking actor
rejects a candidate floor more than 100 units below its feet. It changes heading by +100 or -100 angle units,
selected by the low bit of its X position, and takes the retry path without committing that candidate position.
This branch does not switch the actor to standing or reset its action timer. Indoor solid walls use the same
general normal-response principle. The sky-face branch has additional stand/bored behavior; it should not be
confused with either the general drop guard or ordinary wall contact.

Thus an observed villager that stops naturally in a confined location may be taking a timed random pause,
meeting another friendly actor, or using an authored stationary movement category. Collision can redirect a
walking villager, but the engine does not guarantee that every confined wander ends in a clean stop instead
of walking in place. Proving the cause for one original balcony requires its actor records, face flags and a
runtime reproduction.

The local OpenEnroth `Actor::AI_RandomMove` was used to locate the structural counterpart; the constants and
branches above were checked in MM8 instructions. No reference code was copied. Fresh scratch exports are
`work/creature_movement/wandering.txt` and `wander_dispatch.txt`; broader geometry evidence remains in
[CREATURE_MOVEMENT.md](CREATURE_MOVEMENT.md). Reusable labels are in
[mm8_rel_5326d564_creature_movement.rz](mm8_rel_5326d564_creature_movement.rz).
