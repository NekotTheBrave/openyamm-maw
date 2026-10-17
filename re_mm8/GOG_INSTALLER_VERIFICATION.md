# MM8 GOG installer verification

Checked on 2026-09-10 with innoextract 1.8 and the installed Rizin 0.10.0.

**The existing `mm8_orig/MM8-Rel.exe` is byte-for-byte identical to the game executable in the supplied GOG
installer. Use it as the primary GOG baseline for new engine-behavior research.** Keep the patched `mm8_orig/mm8.exe`
for comparison. This confirms the supplied GOG distribution, not an independently authenticated retail-disc build.

## Source and comparison

Installer path supplied by the user:

```text
/media/pjasicek/3A5CF65E5CF6147B/Users/pjasicek/Downloads/Might and Magic 8 GOG/setup_mm8_2.0.0.13.exe
```

Installer SHA-256:

```text
f2fbd8faf43451b7080639db78318559308df6c76c35038f3fa79efc15f7ec4d
```

The installer identifies its content as Might and Magic VIII - Day of the Destroyer and uses Inno Setup data
version 5.5.0 (Unicode). The executables were extracted without running the installer or game.

| Installer member | Bytes | SHA-256 | Comparison with workspace |
| --- | ---: | --- | --- |
| `app/MM8-Rel.exe` | 1,163,264 | `5326d56459ca74126447bcfd07a8cc85a419663e2281500f0f043f13ffe77a0b` | Exact match to `mm8_orig/MM8-Rel.exe`; verified by SHA-256, `cmp` and direct byte comparison. |
| `app/MM8.exe` | 12,288 | `c5ebbdcfbd39870d36186d3d0ee667d3ee6503905144352b250106fc0673dee2` | Different from the 1,155,072-byte patched `mm8_orig/mm8.exe`. |

The installer file listing contains no `MM8patch.dll`. Its small `MM8.exe` is a launcher: at VA `0x401020` it pushes
the `MM8-Rel.exe` command-line string at `0x402000`, then calls the `CreateProcessA` thunk at `0x401027` before
exiting. The actual game executable `MM8-Rel.exe` has PE32 x86 preferred base `0x400000` and entry point `0x4df52b`.

By comparison, the workspace's patched `mm8.exe` has entry point `0x4e7d9c` and explicitly loads `MM8patch.dll`.
Thus the presence of both filenames in `mm8_orig/` does not mean they are two interchangeable game builds: the
installer originally supplies a launcher under one name and the game under the other, while the patched directory
contains a replacement game executable under `mm8.exe`.

## Reproduce the extraction and comparison

Run from the repository root. The destination is temporary and the source installation remains untouched.

```bash
mm8Installer='/media/pjasicek/3A5CF65E5CF6147B/Users/pjasicek/Downloads/Might and Magic 8 GOG/setup_mm8_2.0.0.13.exe'
sha256sum "$mm8Installer"
innoextract --list "$mm8Installer"
innoextract --extract --include app/MM8-Rel.exe --include app/MM8.exe \
  --output-dir /tmp/mm8-gog-installer-2.0.0.13 "$mm8Installer"
sha256sum /tmp/mm8-gog-installer-2.0.0.13/app/MM8-Rel.exe re_mm8/mm8_orig/MM8-Rel.exe
cmp /tmp/mm8-gog-installer-2.0.0.13/app/MM8-Rel.exe re_mm8/mm8_orig/MM8-Rel.exe
rizin -N -q -e scr.color=0 -c 'pd 18 @ entry0' /tmp/mm8-gog-installer-2.0.0.13/app/MM8.exe
```

No replacement of workspace binaries is necessary. Start a separate project for `MM8-Rel.exe`, for example:

```bash
mkdir -p re_mm8/work
rizin -N -q -e scr.color=0 -c 'aaa;Ps re_mm8/work/mm8-rel-5326d564-base.rzdb' re_mm8/mm8_orig/MM8-Rel.exe
```

That project command is a next-session recipe; the installer comparison itself did not perform a full analysis
or migrate the patched executable's annotations. Re-establish function addresses and structure references before
transferring findings from [the Rizin guide](RIZIN_GUIDE.md). For future runtime checks, use an isolated extraction
of the GOG install rather than assuming the DLLs/configuration in the patched directory reproduce its environment.
