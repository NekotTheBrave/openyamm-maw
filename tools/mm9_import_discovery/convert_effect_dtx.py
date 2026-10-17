#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path

import yaml

from convert_abc_model import decode_dtx_mips


def convert_effect_texture(
    source: Path,
    output: Path,
    import_directory: Path,
    color_space: str,
    alpha_usage: str,
    min_filter: str,
    mag_filter: str,
    wrap_u: str,
    wrap_v: str,
    runtime_texture_path: str | None = None,
) -> dict:
    images, metadata = decode_dtx_mips(source, repair_opaque_alpha=False)
    output.parent.mkdir(parents=True, exist_ok=True)
    images[0].save(output, format="PNG", optimize=False)

    mip_directory = import_directory / "mips"
    mip_directory.mkdir(parents=True, exist_ok=True)
    for mip_index, image in enumerate(images):
        mip_path = mip_directory / f"{mip_index:02d}.png"
        image.save(mip_path, format="PNG", optimize=False)
        metadata["mips"][mip_index]["artifact"] = mip_path.relative_to(import_directory).as_posix()

    report = {
        "schema": "openyamm.effectTextureImport.v1",
        "source": {
            "path": str(source),
            "format": "LithTech DTX",
            "sha256": metadata.pop("sourceSha256"),
        },
        "runtime": {
            "texture": runtime_texture_path or output.as_posix(),
            "format": "PNG RGBA8",
            "colorSpace": color_space,
            "alphaUsage": alpha_usage,
            "premultipliedAlpha": False,
            "mipPolicy": "generate-at-runtime",
            "minFilter": min_filter,
            "magFilter": mag_filter,
            "wrapU": wrap_u,
            "wrapV": wrap_v,
        },
        "conversion": {
            "decoder": "tools/mm9_import_discovery/convert_abc_model.py:decode_dtx_mips",
            "decodedRgbChanged": False,
            "decodedAlphaChanged": False,
            "opaqueAlphaRepair": False,
            "zeroAlphaRgbPreserved": True,
            "sourceMipArtifacts": "mips/*.png",
        },
        "dtx": metadata,
    }
    import_directory.mkdir(parents=True, exist_ok=True)
    (import_directory / "texture_import.yml").write_text(
        yaml.safe_dump(report, sort_keys=False),
        encoding="utf-8",
    )
    return report


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Convert an MM9 effect DTX to a pixel-preserving PNG and retained decoded mip artifacts."
    )
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument(
        "--import-directory",
        type=Path,
        help="Directory for texture_import.yml and decoded source mips; defaults beside the output.",
    )
    parser.add_argument("--color-space", choices=("srgb", "linear"), default="srgb")
    parser.add_argument("--alpha-usage", choices=("opaque", "mask", "blend"), default="blend")
    parser.add_argument("--min-filter", choices=("nearest", "linear"), default="linear")
    parser.add_argument("--mag-filter", choices=("nearest", "linear"), default="linear")
    parser.add_argument("--wrap-u", choices=("repeat", "clamp", "mirror"), default="clamp")
    parser.add_argument("--wrap-v", choices=("repeat", "clamp", "mirror"), default="clamp")
    parser.add_argument(
        "--runtime-texture-path",
        help="Package-relative texture path recorded in provenance; defaults to the output argument.",
    )
    args = parser.parse_args()

    import_directory = args.import_directory or args.output.parent / "import" / args.output.stem
    convert_effect_texture(
        source=args.source,
        output=args.output,
        import_directory=import_directory,
        color_space=args.color_space,
        alpha_usage=args.alpha_usage,
        min_filter=args.min_filter,
        mag_filter=args.mag_filter,
        wrap_u=args.wrap_u,
        wrap_v=args.wrap_v,
        runtime_texture_path=args.runtime_texture_path,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
