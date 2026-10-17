# MM6 surface textures at x2

The existing MM6 ESRGAN batch is `out_prod_bmodel/`: 1,700 PNGs at exactly four times the native
width and height. The two native textures without saved results are `sky20` and `sky21`.
No existing x2 surface-texture pack was found during the 2026-09-08 audit.

`upscale_model.sh` is the saved building-texture recipe:

- `MODEL=realesrgan-x4plus`, `SCALE=4`
- `SHARPEN=0x0.7+0.5+0.02`
- `ENABLE_COLORKEY=0` (default; existing alpha is retained)

No original batch invocation log was found, so these are the saved script defaults, not proof that
there were no historical environment overrides. The x2 export reuses the actual saved results rather
than trying to reconstruct their inference settings. No new inference was necessary.

Run from the repository root:

```sh
python3 tools/upscaling/export_mm6_models_x2.py
```

Output is `tools/upscaling/out_prod_bmodel_mm6_x2/`. Each PNG is Lanczos-downsampled from its saved
x4 result to exactly twice the native dimensions, with no additional sharpening. RGBA is preserved.
`manifest.json` records dimensions, original/x4/x2 hashes, exclusions and provenance limitations.
`review.jpg` compares six surface samples against original and saved x4 artwork.

This exports the shared MM6 surface-texture pool used for outdoor bmodels and indoor geometry;
it is not limited to New Sorpigal or to textures referenced only by outdoor buildings.
Registered terrain, sky names and HD water animation names are excluded. The separately restored
terrain pack remains authoritative for the ground.

For an isolated runtime, copy only the manifest-listed PNGs into `assets_dev/worlds/mm6/textures_x2/`
and select `video_quality.texture=x2`. No runtime assets or settings are changed by the exporter.

The same exporter now supports `--world mm7 mm8`; see [MM7/MM8 packs](MM7_MM8_MODELS_X2.md). The default invocation remains MM6.
