# Lifedrain damage and healing

Checked on 2026-10-08 with Rizin against the original GOG `mm8_orig/MM8-Rel.exe`, SHA-256
`5326d56459ca74126447bcfd07a8cc85a419663e2281500f0f043f13ffe77a0b`.
The executable was opened read-only. Addresses below belong to this executable, not the patched `mm8.exe`.

The spell-damage routine at `0x43962c` dispatches spell 111 to `0x43974f`. For skill level S, it rolls:

| Mastery | Damage |
| --- | --- |
| Normal / Expert | 3 + S rolls of 1–3 |
| Master | 5 + S rolls of 1–5 |
| Grandmaster | 7 + S rolls of 1–7 |

The party-to-actor damage routine tests the impact object's spell id against 111 at `0x437fc0`.
At `0x437fc6`–`0x437fee`, healing is the smaller of the target's remaining HP divided by three and the
resolved damage divided by three. Nonzero damage grants at least one HP. The caster's health is incremented
and capped at maximum HP at `0x437ff1`–`0x43800f`; monster damage follows at `0x438011`.
There is no missing-caster-health gate in this damage/healing path. Full health only prevents further healing.

Read-only reproduction, after verifying the executable hash:

```bash
third_party/rizin/build/binrz/rizin/rizin -N -q -e scr.color=0 \
  -c 'pd 30 @ 0x43974f;pd 38 @ 0x437fbc' re_mm8/mm8_orig/MM8-Rel.exe
```

OpenYAMM's shared actor handler omitted Lifedrain, so both indoor and outdoor casts failed to apply damage.
The fix registers it as direct Dark damage, applies the mastery dice, and heals from the target's actual HP
loss after resistance and overkill. Focused regression coverage includes full health, wounded casters,
all four masteries, resistance, immunity and targets with one or two HP remaining.
