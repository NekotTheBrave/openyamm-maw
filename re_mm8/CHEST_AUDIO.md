# Original MM8 chest opening and closing sounds

Investigated 2026-10-05 with Rizin disassembly and the rz-ghidra decompiler.

**A successful chest opening requests sound ID 141 (`0x8d`), named `openchest0101`.**
The corresponding mounted OpenYAMM asset is
[`openchest0101.wav`](../assets_dev/engine/audio/openchest0101.wav).
The request occurs when the chest UI is entered, after the trap checks have accepted the opening.
A trap explosion instead requests ID 8 (`fireBall`) and returns without entering that UI.

**Normal chest closing is silent:** neither the close button/Escape callback nor the chest-screen exit
requests a closing effect or generic click sound. The user-requested removal of OpenYAMM's extra sound 439
(`chest close`) makes both pointer and Escape closing paths silent as well.

## Binary evidence

Target: `mm8_orig/MM8-Rel.exe`, the executable previously verified against the supplied GOG installer.
Preferred image base: `0x400000`.

```text
SHA-256: 5326d56459ca74126447bcfd07a8cc85a419663e2281500f0f043f13ffe77a0b
```

Addresses below belong to this executable, not the patched `mm8.exe`.
Names are research labels, not recovered symbols. See the
[annotations](mm8_rel_5326d564_chest_audio.rz).

| VA | Observation |
| --- | --- |
| `0x44619b` / `0x44619f` | EVT opcode 7 reads the chest ID into ECX and calls the opening routine at `0x420093`. |
| `0x420093` | Validates chest index, prepares contents, checks the active character and handles traps. |
| `0x420526` | Successful paths converge on chest-screen creation. |
| `0x420545` | Constructs the chest screen through `0x4d3339`. |
| `0x4d3345` | The constructor assigns vtable `0x4ec658`. |
| `0x42055b` | Calls the screen manager at `0x4d49ab`, passing the new screen and chest ID. |
| `0x4d49e6` | The manager invokes the screen's vtable slot `+0xd8`; `0x4ec730` contains `0x4d33eb`. |
| `0x4d33eb` | Chest-screen entry: pauses the game timer, selects screen type 10 and stops existing sounds. |
| `0x4d342b` | Pushes the hardcoded sound ID `0x8d` (141). |
| `0x4d3432` | Calls the common audio routine `0x4aa8b7`. |

The decisive instruction sequence is:

```text
0x4d3421  xor  ebx, ebx
0x4d3423  push ebx
0x4d3424  push ebx
0x4d3425  push ebx
0x4d3426  push ebx
0x4d3427  push -1
0x4d3429  push ebx
0x4d342a  push ebx
0x4d342b  push 0x8d
0x4d3430  mov  ecx, edi       ; audio object, 0xfeb258
0x4d3432  call 0x4aa8b7
```

Ghidra independently expresses that request as
`fcn.004aa8b7(0x8d, 0, 0, 0xffffffff, 0, 0, 0, 0)`.
The assembly establishes the arguments despite the decompiler's imperfect recovered types.
Object/source argument zero means this is a non-positional UI request; no chest position is passed.
The ID is fixed in the screen-entry routine, with no chest-picture/material selection.
Every accepted opening that enters this screen reaches it, including reopening a chest.

`0x4aa8f8` reads the first argument as the requested ID. The lookup at `0x4aa914`–`0x4aa924`
compares it with the 32-bit ID at record offset `0x40`, advancing by `0x98` (152) bytes.
Thus 141 is a sound ID, not a sound-table row number.

## Trap distinction

At `0x4204a0`, the failed-disarm path pushes ID 8; `0x4204a7` calls the same audio routine.
It then applies the explosion, clears the trap flag, optionally schedules the character's trap reaction,
and jumps at `0x42050b` to the failure return at `0x42059e`. It never enters the chest screen on that attempt.
Successful disarming clears the trap flag and joins the normal screen-opening path;
the character's disarm reaction and the chest-opening effect are separate requests.

## Closing: no sound request

Follow-up inspection of the same original executable on 2026-10-05 traced the close control, its Escape
binding, the shared action dispatcher, screen removal and chest-specific cleanup. The conclusion is based
on those paths, rather than merely finding no sound call in one destructor.

| VA | Observation |
| --- | --- |
| `0x4d3455`–`0x4d345c` | Constructs the close button with action 11 and zero parameters. |
| `0x4d3470`–`0x4d3494` | Assigns its `c_close_up`, `c_close_dn` and `c_close_ht` images. |
| `0x4d349a`–`0x4d34b7` | Binds Escape (key 27) to this button. |
| `0x4c4c41`–`0x4c4c67` | Button constructor stores action 11 at `+0xc8` and zero parameters at `+0xcc`/`+0xd0`. |
| `0x4c3ae6` | Button mouse-release path invokes vtable slot `+0x48`, targeting `0x4c4f36`. |
| `0x4c3be9` / `0x4c3c59` | Inherited keyboard handler checks the binding and invokes button activation at vtable slot `+0x30`. |
| `0x4c4bdc` / `0x4c4bf2` | Keyboard activation draws the pressed state and invokes the same `+0x48` action callback, without audio. |
| `0x4c4f36`–`0x4c4f8e` | Queues the button action and clears its pressed state, without an audio call. |
| `0x43073f`–`0x430749` | Dispatcher action 11 tests screen type 10 (chest), then jumps to screen removal. |
| `0x43074f`–`0x43075e` | In chest-inventory screen type 15, action 11 returns to type 10 without closing the chest. |
| `0x42fc38` | Calls screen removal at `0x4d4a46`, with no intervening audio request. |
| `0x4d4a5b` / `0x4d4a66` | Invokes the screen's `+0xe0` and `+0xe8` methods. |
| `0x4ec738` / `0x4ec740` | Chest vtable targets those methods at `0x4d3b27` and `0x4d3564`. |
| `0x4d3b27` / `0x4d48e7` | Common screen-child cleanup, without an audio request. |
| `0x4d3564`–`0x4d359f` | Chest exit resumes the timer, detaches the shared panel, releases textures, selects gameplay screen 0 and marks redraw. No audio call. |

The screen manager restores its previous screen after removal. Its removal/restore logic also contains
no audio request. Ghidra decompilation of `0x4d3564` agrees with the disassembly.
The button constructor's cached argument names are misleading; raw `[esp+0x10]`, `[esp+0x14]` and
`[esp+0x18]` accesses, the two saved registers and the callee's stack cleanup establish the stored values.
Ghidra's labels for the dispatcher's compressed switch are also not action IDs; disassembly identifies
the action-11 case explicitly. Neither analysis artifact changes the observed silent close path.

OpenYAMM previously requested `SoundId::ChestClose = 439` from the shared Escape path in
`game/gameplay/GameplayInputController.cpp` and the pointer/keyboard close branches in
`game/gameplay/GameplayOverlayInputController.cpp`. Those three requests were removed on 2026-10-05
at the user's direction. Chest closing now follows the original silent behavior.

## Sound-name corroboration and limits

The local `mm8_orig/Data/EnglishT.lod` contains `dsounds.bin` at archive offset `0x11c6d`.
Its resource has a 96-byte header followed by zlib data. Decompression produces 524,100 bytes:
4 bytes containing the record count (3448), followed by 152-byte records.
Record 98 begins at decompressed offset `0x3a34` and contains:

```text
Name: openchest0101
ID:   141
Type: 2 (swap)
Bits: 0
```

This decoded table is byte-for-byte identical to `mm8_orig/DataFiles/dsounds.bin`.
Its SHA-256 is `7e872b31191fe3d3d2fd1c8570b7d4e3507805dd2e592cec34220d8c7f0fb6ae`.
The archive SHA-256 is `cf7ed373a990bac65d7f11c27f1d3f34a18461ff47e2c8138581da68aa486c38`.
The local text tables and `assets_dev/engine/data_tables/sounds.txt` agree on this mapping.

The executable's GOG provenance is established; these local data files were not independently compared with
the installer in this investigation. The installation contains modified content, so the hardcoded ID conclusion
rests on the verified executable, while the asset-name mapping is separately corroborated by the local tables.
No original-game listening session, live patch-hook tracing or runtime asset comparison was performed.

For comparison only, the local MM7 OpenEnroth reference names its chest-opening sound
`SOUND_openchest0101 = 208` in `src/Media/Audio/SoundEnums.h:101`, used by
`src/Engine/Objects/Chest.cpp:145`. That MM7 number must not replace MM8's verified 141.

## Reproduce

Tools used: Rizin 0.10.0, source commit `c329b50b1ce87e32168f46e8a29d019b747c4117`;
rz-ghidra commit `26b6130f310e0fa8027bdfacba65079dbd448e63`.
The installed commands were absent from PATH in this session, so the existing build-tree executables/plugin
were used. A disposable Sleigh directory under `work/chest_audio/sleigh/` links the source x86 language
definitions, processor/compiler specifications and existing compiled `x86.sla`/`x86-64.sla`.
No tool rebuild or executable modification was necessary.

From the repository root, the critical assembly can be checked without a saved analysis project:

```bash
sha256sum re_mm8/mm8_orig/MM8-Rel.exe
third_party/rizin/build/binrz/rizin/rizin -N -q -e scr.color=0 \
  -i re_mm8/mm8_rel_5326d564_chest_audio.rz \
  -c 'pd 10 @ 0x44619b;pd 14 @ 0x4d3421;pxw 4 @ 0x4ec730;pd 10 @ 0x4d49d9;pd 12 @ 0x420498' \
  re_mm8/mm8_orig/MM8-Rel.exe
```

For Ghidra with an installed plugin, analyze `0x4d33eb` with `aF`, then run `pdgo` there.
For the build-tree plugin used here, first run `L third_party/rz-ghidra/build/core_ghidra.so`
and set `ghidra.sleighhome` to the absolute disposable Sleigh-directory path.
Raw disassembly/decompiler exports are retained in ignored `work/chest_audio/`.
The three executable/DLL hashes from the guide and the inspected sound-data hashes remained unchanged.

Closing can be reproduced with the same executable and annotation script using:

```text
pd 26 @ 0x43073f
pd 5 @ 0x42fc34
pxw 12 @ 0x4ec738
aF @ 0x4d3564
pdf @ 0x4d3564
pdgo @ 0x4d3564
```

The original opening investigation changed no runtime or assets. In the subsequent user-requested fix,
the incorrect Invisibility gate was removed from `OutdoorWorldRuntime::activateChestView`, so accepted outdoor
openings request 141 just like indoor openings. The development build and 24 focused chest tests passed.
The closing investigation changed research notes/annotations only. The subsequent user-requested closing fix
removed all three playback requests from the shared input controllers. The development build and 27 focused
chest/corpse-loot tests passed, and no `SoundId::ChestClose` playback references remain in `game/` or `tests/`.
