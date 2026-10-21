#!/usr/bin/env python3
"""Check the content pack split (tools/runtime_packs.py): determinism, membership, stable buckets and identity."""

import json
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile

from runtime_packs import (PackConfig, assign_packs, build_packs, content_id, group_key, write_index, write_zip)

CONFIG = {
    "schema": 1, "target_mib": 64, "max_mib": 128, "core_max_mib": 16, "worlds": ["mm6"],
    "packages": {
        "engine": {"core": ["data_tables"], "domains": {"icons_x2": {"buckets": 4},
                                                        "models": {"buckets": 4, "group": "model"},
                                                        "sprites_new": {"buckets": 3, "group": "first_dir"}}},
        "world": {"core": ["events"], "domains": {
            "maps": {"buckets": 2, "split": {"lighting": {"extensions": [".lighting"], "buckets": 3}}},
            "textures_x4": {"buckets": {"desktop": 3, "android": 1}}}},
    },
}


def glb(uris):
    document = json.dumps({"asset": {"version": "2.0"}, "images": [{"uri": uri} for uri in uris]}).encode()
    document += b" " * (-len(document) % 4)
    return struct.pack("<4sII", b"glTF", 2, 20 + len(document)) + struct.pack("<II", len(document), 0x4E4F534A) + document


class RuntimePackTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="openyamm-packs-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.config = PackConfig(CONFIG)

    def engine_entries(self):
        model = self.root / "demon.glb"
        model.write_bytes(glb(["textures/aaaa.oytex", "textures/bbbb.oytex"]))
        entries = {"icon_packages.txt": b"engine x2\n", "data_tables/items.txt": b"items",
                   "models/demon.glb": model, "models/textures/aaaa.oytex": b"tex a",
                   "models/textures/bbbb.oytex": b"tex b", "models/actors.yml": b"actors"}
        for index in range(40):
            entries[f"icons_x2/icon{index}.png"] = f"icon {index}".encode()
        for family in ("goblin", "rat"):
            for frame in range(3):
                entries[f"sprites_new/{family}/page{frame}.oyatlas"] = f"{family} {frame}".encode()
        return entries

    def world_entries(self):
        return {"world.yml": b"id: mm6", "events/oute3.lua": b"lua",
                "maps/oute3.odm": b"map", "maps/oute3.scene.yml": b"scene", "maps/oute3.scene.1.yml": b"scene 1",
                "maps/oute3.bake.json": b"{}", "maps/oute3.lighting": b"bake", "maps/6d01.blv": b"dungeon",
                "textures_x4/stone.oytex": b"stone"}

    def test_every_file_lands_in_exactly_one_pack_at_its_virtual_path(self):
        packs = assign_packs("engine", self.engine_entries(), self.config, "desktop")
        virtual = [pack["mount"] + "/" + name for pack in packs.values() for name in pack["entries"]]
        self.assertEqual(len(virtual), len(set(virtual)))
        self.assertEqual(sorted(virtual), sorted("/engine/" + name for name in self.engine_entries()))
        self.assertEqual(packs["engine.core"]["mount"], "/engine")
        self.assertIn("data_tables/items.txt", packs["engine.core"]["entries"])

    def test_group_rules_keep_related_files_together(self):
        self.assertEqual(group_key("oute3.scene.1.yml", "stem", "", {}), "oute3")
        self.assertEqual(group_key("Houses/sample.ogv", "stem", "", {}), "Houses/sample")
        self.assertEqual(group_key("goblin/page0.oyatlas", "first_dir", "", {}), "goblin")
        world = assign_packs("mm6", self.world_entries(), self.config, "desktop")
        oute3 = {pack_id for pack_id, pack in world.items() for name in pack["entries"]
                 if name.startswith("oute3.") and not name.endswith(".lighting")}
        self.assertEqual(len(oute3), 1)
        lighting = [pack_id for pack_id, pack in world.items() if "oute3.lighting" in pack["entries"]]
        self.assertTrue(lighting[0].startswith("mm6.maps.lighting."))
        self.assertTrue(all(pack["mount"] == "/worlds/mm6/maps" for pack_id, pack in world.items()
                            if pack_id.startswith("mm6.maps")))
        engine = assign_packs("engine", self.engine_entries(), self.config, "desktop")
        model = [pack_id for pack_id, pack in engine.items() if "demon.glb" in pack["entries"]][0]
        self.assertIn("textures/aaaa.oytex", engine[model]["entries"])
        self.assertIn("textures/bbbb.oytex", engine[model]["entries"])
        goblin = {pack_id for pack_id, pack in engine.items() for name in pack["entries"] if name.startswith("goblin/")}
        self.assertEqual(len(goblin), 1)

    def test_adding_a_file_changes_exactly_one_pack(self):
        before = {pack_id: content_id(pack["entries"])
                  for pack_id, pack in assign_packs("engine", self.engine_entries(), self.config, "desktop").items()}
        entries = self.engine_entries()
        entries["icons_x2/new_icon.png"] = b"new"
        after = {pack_id: content_id(pack["entries"])
                 for pack_id, pack in assign_packs("engine", entries, self.config, "desktop").items()}
        changed = {pack_id for pack_id in before.keys() | after.keys() if before.get(pack_id) != after.get(pack_id)}
        self.assertEqual(len(changed), 1)

    def test_unlisted_domains_fail(self):
        with self.assertRaisesRegex(ValueError, "lists engine domain 'secret' nowhere"):
            assign_packs("engine", {"secret/file.txt": b"x"}, self.config, "desktop")

    def test_profile_bucket_counts(self):
        entries = {f"textures_x4/t{index}.oytex": bytes([index]) for index in range(30)}
        self.assertEqual(len(assign_packs("mm6", entries, self.config, "desktop")), 3)
        self.assertEqual(set(assign_packs("mm6", entries, self.config, "android")), {"mm6.textures_x4"})

    def test_packing_is_deterministic_and_identity_ignores_compressor_bytes(self):
        packages = {"engine": self.engine_entries(), "mm6": self.world_entries()}
        first = build_packs(packages, self.config, "desktop", self.root / "one")
        second = build_packs(packages, self.config, "desktop", self.root / "two")
        self.assertEqual([(r["file"], r["sha256"]) for r in first], [(r["file"], r["sha256"]) for r in second])
        # The same entries with different compression bytes keep their content_id.
        entries = {"a.txt": b"aaaa" * 1000}
        write_zip(entries, self.root / "deflated.zip")
        with zipfile.ZipFile(self.root / "stored.zip", "w", zipfile.ZIP_STORED) as archive:
            archive.writestr("a.txt", entries["a.txt"])
        def identity(path):
            with zipfile.ZipFile(path) as archive:
                return content_id({name: archive.read(name) for name in archive.namelist()})
        self.assertNotEqual((self.root / "deflated.zip").read_bytes(), (self.root / "stored.zip").read_bytes())
        self.assertEqual(identity(self.root / "deflated.zip"), identity(self.root / "stored.zip"))

    def test_published_bytes_are_reused_and_index_records_them(self):
        packages = {"engine": self.engine_entries()}
        first = build_packs(packages, self.config, "desktop", self.root / "published")
        core = next(record for record in first if record["id"] == "engine.core")
        published = self.root / "published-core.zip"
        # A runner whose zlib differs would write other bytes; the published file is used instead.
        with zipfile.ZipFile(published, "w", zipfile.ZIP_STORED) as archive:
            for name in sorted(self.engine_entries()):
                if name.startswith("data_tables/") or "/" not in name:
                    archive.writestr(name, self.engine_entries()[name])
        second = build_packs(packages, self.config, "desktop", self.root / "next", {core["content_id"]: published})
        reused = next(record for record in second if record["id"] == "engine.core")
        self.assertEqual(reused["content_id"], core["content_id"])
        self.assertEqual((self.root / "next/packs" / reused["file"]).read_bytes(), published.read_bytes())
        index = json.loads(write_index(self.root / "next", second, profile="desktop", release="0.8", build="0.8",
                                       content_format=1).read_text())
        self.assertEqual(index["store"], "packs")
        self.assertEqual({pack["id"] for pack in index["packs"]}, {record["id"] for record in second})

    def test_size_limits_fail_packaging(self):
        config = PackConfig({**CONFIG, "max_mib": 1})
        big = {f"icons_x2/big{index}.png": bytes(range(256)) * 4096 * 2 for index in range(6)}
        with self.assertRaisesRegex(ValueError, "over its 1 MiB limit"):
            build_packs({"engine": big}, config, "desktop", self.root / "limits")


if __name__ == "__main__":
    unittest.main()
