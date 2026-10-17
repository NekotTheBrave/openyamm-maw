#!/usr/bin/env python3
"""Run the actual package-metadata shell block without building an APK."""

import os
from pathlib import Path
import subprocess
import tempfile
import textwrap
import unittest


repo_root = Path(__file__).resolve().parents[1]
workflow = (repo_root / ".github/workflows/nightly.yml").read_text()
metadata_step = workflow.split("      - name: Validate release tag and resolve names\n", 1)[1]
metadata_script = textwrap.dedent(metadata_step.split("        run: |\n", 1)[1].split("\n  android:", 1)[0])


class AndroidVersionTests(unittest.TestCase):
    def resolve(self, ref="refs/heads/main", event="schedule", run=1, attempt=1, commit_time=1792100575,
                succeeds=True):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "output"
            environment = dict(os.environ, GITHUB_REF=ref, GITHUB_REF_NAME=ref.rsplit("/", 1)[-1],
                               GITHUB_EVENT_NAME=event, GITHUB_SHA="1234567", GITHUB_OUTPUT=str(output),
                               GITHUB_RUN_NUMBER=str(run), GITHUB_RUN_ATTEMPT=str(attempt),
                               OPENYAMM_COMMIT_TIME=str(commit_time))
            result = subprocess.run(["bash", "-euo", "pipefail", "-c", metadata_script], env=environment,
                                    cwd=repo_root, capture_output=True, text=True, timeout=10)
            if not succeeds:
                self.assertNotEqual(result.returncode, 0)
                return
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return dict(line.split("=", 1) for line in output.read_text().splitlines())

    def test_newer_commits_update_older_packages(self):
        nightly = self.resolve(run=40, commit_time=1792100575)
        release = self.resolve(ref="refs/tags/1.1", event="push", run=41, commit_time=1792100600)
        following_nightly = self.resolve(run=42, commit_time=1792187000)
        codes = [1000000] + [int(values["android_version_code"]) for values in (nightly, release, following_nightly)]
        self.assertTrue(all(older < newer for older, newer in zip(codes, codes[1:])))
        self.assertEqual(nightly["android_version_name"], "nightly-40.1")
        self.assertEqual(release["android_version_name"], "1.1")
        self.assertEqual(release["android_asset_name"], "OpenYAMM-1.1-android-arm64.apk")

    def test_same_commit_shares_a_code(self):
        nightly = self.resolve(run=40)
        rerun = self.resolve(run=40, attempt=2)
        release = self.resolve(ref="refs/tags/1.1", event="push", run=41)
        self.assertEqual(nightly["android_version_code"], rerun["android_version_code"])
        self.assertEqual(nightly["android_version_code"], release["android_version_code"])

    def test_all_build_types_have_explicit_versions(self):
        for event in ("push", "schedule", "workflow_dispatch"):
            with self.subTest(event=event):
                values = self.resolve(event=event)
                self.assertGreater(int(values["android_version_code"]), 1000000)
                self.assertEqual(values["android_version_name"], "nightly-1.1")
                self.assertEqual(values["android_asset_name"], "OpenYAMM-nightly-android-arm64.apk")
                self.assertEqual(values["is_tag"], "false")

    def test_android_code_limit(self):
        self.assertEqual(int(self.resolve(commit_time=1767225600 + 2100000000)["android_version_code"]), 2100000000)
        self.resolve(commit_time=1767225600 + 2100000001, succeeds=False)
        self.resolve(commit_time=1767225600 + 1000000, succeeds=False)
        self.resolve(commit_time="yesterday", succeeds=False)

    def test_tag_validation_is_preserved(self):
        for tag in ("1.1", "0.14", "2.0"):
            self.assertEqual(self.resolve(ref="refs/tags/" + tag, event="push")["android_version_name"], tag)
        for tag in ("0.0", "v1.1", "1.1.0", "01.1", "1.100"):
            self.resolve(ref="refs/tags/" + tag, event="push", succeeds=False)

if __name__ == "__main__":
    unittest.main()
