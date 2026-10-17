# MM7 and MM8 surface textures at x2

These packs use the same export method as the accepted MM6 x2 pack: reuse the saved
4x ESRGAN output, resize to exactly twice the original dimensions with Pillow Lanczos,
preserve RGBA, and apply no further sharpening.

| World | Runtime textures | Saved x4 input | Runtime output |
| --- | ---: | --- | --- |
| MM7 | 1,085 | `out_prod_bmodel_mm7/` | `out_prod_bmodel_mm7_x2/` |
| MM8 | 1,230 | `out_prod_bmodel_mm8/` | `out_prod_bmodel_mm8_x2/` |

The scope matches MM6: the shared surface-texture pool for outdoor bmodels and indoor
geometry. Registered terrain, sky names and HD water animation names are excluded.
These packs do not replace the separately restored terrain.

Four additional MM7 source filenames containing spaces are excluded: `hem1er w.bmp`,
`hhm3el2 a.bmp`, `hhm3el2 b.bmp`, and `hwp1b wall.bmp`. None occurs in the decoded face
texture references across all 76 MM7 maps or in the current asset text/config/event files.
The normal filenames are included; the distinct `hem1er w` image is not silently replaced
by `hem1erw`.

## Rebuild and provenance

```sh
python3 tools/upscaling/export_mm6_models_x2.py --world mm7 mm8
```

The historical script name and its default MM6 invocation are retained. Each output
`manifest.json` records original/x4/x2 hashes, dimensions, source directories, scanned-map
hashes, referenced surface names and exclusions. Each output also includes `review.jpg`.

The saved recipe in `upscale_model.sh` uses `realesrgan-x4plus`, 4x inference,
`SHARPEN=0x0.7+0.5+0.02`, and `ENABLE_COLORKEY=0`. Original batch invocation logs are
absent, so historical environment overrides cannot be verified. These exports reuse the
actual saved artwork rather than claiming a fresh reconstruction of those settings.

## Runtime packs

- [MM7 ZIP](mm7_surface_textures_x2.zip)
- [MM8 ZIP](mm8_surface_textures_x2.zip)
- [Export validation](mm7_mm8_x2_validation.json)

Each ZIP contains `worlds/mm7/textures_x2/` or `worlds/mm8/textures_x2/`. Copy that
`worlds` directory into the intended staged `assets_dev` and set `[video_quality] texture=x2`.
Only manifest-listed PNGs belong in the runtime texture directory, not the review JPEG.
The exporter does not install the packs or change game settings, maps, sprites or terrain.

## Installed development assets

The packs are installed alongside the originals in `assets_dev/worlds/mm7/textures_x2/`
and `assets_dev/worlds/mm8/textures_x2/`. All 2,315 PNG hashes match the export manifests;
all original texture files and game settings are unchanged. See
[installation audit](mm7_mm8_x2_assets_dev_installation.json). Select texture quality `x2` to use them.
