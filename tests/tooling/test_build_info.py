# SPDX-License-Identifier: GPL-3.0-only
"""Build provenance uses synthetic source files, never disc/generated content."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import re
import tomllib
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import build_info


class BuildInfoTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="kirby's build sources ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / "source"
        self.root.mkdir()
        self.git("init", "-q")
        (self.root / ".gitignore").write_text("/local/\n/disc/\n/generated/\n/build/\n/vendor/\n")
        (self.root / "main.cpp").write_text("// synthetic project source\n")
        (self.root / "dependencies.json").write_text(json.dumps({
            "wiicompiled": {"commit": "f" * 40, "submodules": {"aurora-main": "a" * 40}}
        }))
        self.git("add", ".")
        self.git("-c", "user.name=Build test", "-c", "user.email=build@example.invalid",
                 "commit", "-qm", "Synthetic source fixture")

    def git(self, *args):
        return subprocess.check_output(["git", "-C", str(self.root), *args], stderr=subprocess.DEVNULL)

    def collect(self, **options):
        return build_info.collect(self.root, compiler="Clang test", build_type="Release", **options)

    def test_records_revision_dependency_pins_and_deterministic_source_hash(self):
        info = self.collect()
        self.assertEqual(info, self.collect())
        self.assertEqual(info["source_revision"], self.git("rev-parse", "HEAD").decode().strip())
        self.assertFalse(info["source_modified"])
        self.assertRegex(info["source_sha256"], r"^[a-f0-9]{64}$")
        self.assertEqual(info["translator_revision"], "f" * 40)
        self.assertEqual(info["aurora_revision"], "a" * 40)

    def test_ignored_disc_generated_and_build_outputs_do_not_change_identity(self):
        before = self.collect()
        for folder in ("disc", "generated", "build", "local", "vendor"):
            path = self.root / folder / "synthetic-private-input"
            path.parent.mkdir()
            path.write_bytes(b"not authored project source")
        self.assertEqual(before, self.collect())

    def test_modified_untracked_deleted_and_executable_sources_change_the_fingerprint(self):
        before = self.collect()
        source = self.root / "main.cpp"
        source.write_text("// changed source\n")
        self.assertNotEqual(before["source_sha256"], self.collect()["source_sha256"])
        self.assertTrue(self.collect()["source_modified"])
        source.write_text("// synthetic project source\n")
        source.chmod(source.stat().st_mode | 0o100)
        self.assertNotEqual(before["source_sha256"], self.collect()["source_sha256"])
        source.chmod(source.stat().st_mode & ~0o111)
        extra = self.root / "extra.cpp"
        extra.write_text("// untracked input\n")
        self.assertNotEqual(before["source_sha256"], self.collect()["source_sha256"])
        extra.unlink()
        source.unlink()
        self.assertNotEqual(before["source_sha256"], self.collect()["source_sha256"])

    def test_symlink_targets_are_not_read(self):
        outside = Path(self.temporary.name) / "outside"
        outside.write_text("private content")
        (self.root / "source-link").symlink_to(outside)
        before = self.collect()
        outside.write_text("different private content")
        self.assertEqual(before, self.collect())

    def test_toolchain_options_are_recorded(self):
        info = self.collect(compiler_options='-O2 -DNAME="spaces and quotes"')
        self.assertEqual(info["compiler_options"], '-O2 -DNAME="spaces and quotes"')

    def test_non_checkout_is_rejected_instead_of_using_parent_revision(self):
        nested = self.root / "nested"
        nested.mkdir()
        with self.assertRaisesRegex(ValueError, "checkout root"):
            build_info.collect(nested, compiler="test", build_type="Release")

    def test_unchanged_header_keeps_timestamp_and_rejects_symlink_output(self):
        output = Path(self.temporary.name) / "BuildInfo.h"
        build_info.write_header(output, self.collect())
        before = output.stat().st_mtime_ns
        build_info.write_header(output, self.collect())
        self.assertEqual(before, output.stat().st_mtime_ns)
        link = Path(self.temporary.name) / "linked.h"
        link.symlink_to(output)
        content = output.read_bytes()
        with self.assertRaisesRegex(ValueError, "symlink"):
            build_info.write_header(link, self.collect())
        self.assertEqual(content, output.read_bytes())

    def test_header_round_trips_unicode_quotes_and_a_raw_string_terminator(self):
        record = self.collect(compiler_options=")KBI0")
        record["compiler"] = 'Clang é\n"quoted"'
        output = Path(self.temporary.name) / "BuildInfo.h"
        build_info.write_header(output, record)
        match = re.search(r'R"(\w+)\((.*?)\)\1";', output.read_text(), re.S)
        self.assertIsNotNone(match)
        self.assertNotEqual(match[1], "KBI0")
        self.assertEqual(tomllib.loads(match[2])["build"], record)

    def test_oversized_records_are_rejected_without_replacing_the_previous_header(self):
        output = Path(self.temporary.name) / "BuildInfo.h"
        build_info.write_header(output, self.collect())
        before = output.read_bytes()
        with self.assertRaisesRegex(ValueError, "16 KiB"):
            build_info.write_header(output, self.collect(compiler_options="x" * 20000))
        self.assertEqual(before, output.read_bytes())
