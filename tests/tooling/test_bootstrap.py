# SPDX-License-Identifier: GPL-3.0-only
import contextlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import bootstrap


class BootstrapTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="kirby bootstrap ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.project = self.root / "project"
        self.project.mkdir()
        config = self.root / "global.gitconfig"
        config.write_text("[core]\n\tautocrlf = true\n[user]\n\tname = Fixture\n"
                          "\temail = fixture@example.invalid\n[commit]\n\tgpgsign = false\n"
                          "[init]\n\tdefaultBranch = main\n"
                          "[protocol \"file\"]\n\tallow = always\n", encoding="utf-8")
        environment = patch.dict(os.environ, GIT_CONFIG_GLOBAL=str(config), GIT_CONFIG_NOSYSTEM="1",
                                 GIT_ALLOW_PROTOCOL="file")
        environment.start()
        self.addCleanup(environment.stop)
        location = patch.object(bootstrap, "ROOT", self.project)
        location.start()
        self.addCleanup(location.stop)
        self.source = self.repository("source")

    def git(self, *args):
        return subprocess.check_output(["git", *map(str, args)], stderr=subprocess.PIPE, text=True).strip()

    def repository(self, name):
        path = self.root / name
        self.git("init", path)
        self.git("-C", path, "config", "core.autocrlf", "false")
        (path / "payload.txt").write_bytes(b"first line\nsecond line\n")
        self.git("-C", path, "add", "payload.txt")
        self.git("-C", path, "commit", "-m", "Fixture")
        return path

    def pins(self, submodule=False):
        pin = {"url": str(self.source), "commit": self.git("-C", self.source, "rev-parse", "HEAD")}
        if submodule:
            pin["submodules"] = {"nested": self.git("-C", self.source / "nested", "rev-parse", "HEAD")}
        (self.project / "dependencies.json").write_text(json.dumps({"fixture": pin}), encoding="utf-8")

    def add_submodule(self):
        nested = self.repository("submodule-source")
        self.git("-C", self.source, "submodule", "add", str(nested), "nested")
        self.git("-C", self.source, "commit", "-am", "Add nested fixture")
        self.pins(submodule=True)

    def run_bootstrap(self):
        with contextlib.redirect_stdout(io.StringIO()):
            bootstrap.main()

    @property
    def checkout(self):
        return self.project / "vendor/fixture"

    def test_new_checkout_uses_lf_despite_global_autocrlf(self):
        self.pins()
        self.run_bootstrap()
        self.assertEqual((self.checkout / "payload.txt").read_bytes(), b"first line\nsecond line\n")
        self.assertEqual(self.git("-C", self.checkout, "config", "--local", "core.autocrlf"), "false")
        self.assertEqual(self.git("-C", self.checkout, "config", "--local", "core.eol"), "lf")

    def test_submodule_initial_checkout_uses_lf(self):
        self.add_submodule()
        self.run_bootstrap()
        self.assertEqual((self.checkout / "nested/payload.txt").read_bytes(), b"first line\nsecond line\n")

    def test_existing_clean_crlf_checkout_is_not_reconfigured_or_rewritten(self):
        self.pins()
        self.git("clone", str(self.source), self.checkout)
        payload = self.checkout / "payload.txt"
        self.assertEqual(payload.read_bytes(), b"first line\r\nsecond line\r\n")
        config = (self.checkout / ".git/config").read_bytes()
        self.run_bootstrap()
        self.assertEqual(payload.read_bytes(), b"first line\r\nsecond line\r\n")
        self.assertEqual((self.checkout / ".git/config").read_bytes(), config)

    def test_existing_modified_checkout_is_refused_without_mutation(self):
        self.pins()
        self.git("clone", str(self.source), self.checkout)
        payload = self.checkout / "payload.txt"
        payload.write_bytes(b"user's changes\r\n")
        config = (self.checkout / ".git/config").read_bytes()
        with self.assertRaisesRegex(SystemExit, "local changes; left untouched"):
            self.run_bootstrap()
        self.assertEqual(payload.read_bytes(), b"user's changes\r\n")
        self.assertEqual((self.checkout / ".git/config").read_bytes(), config)

    def test_existing_clean_submodule_keeps_crlf_and_configuration(self):
        self.add_submodule()
        self.git("clone", "--recurse-submodules", str(self.source), self.checkout)
        payload = self.checkout / "nested/payload.txt"
        self.assertEqual(payload.read_bytes(), b"first line\r\nsecond line\r\n")
        config = (self.checkout / ".git/config").read_bytes()
        nested_config = (self.checkout / ".git/modules/nested/config").read_bytes()
        self.run_bootstrap()
        self.assertEqual(payload.read_bytes(), b"first line\r\nsecond line\r\n")
        self.assertEqual((self.checkout / ".git/config").read_bytes(), config)
        self.assertEqual((self.checkout / ".git/modules/nested/config").read_bytes(), nested_config)

    def test_modified_submodule_is_refused_even_when_git_ignore_is_configured(self):
        self.add_submodule()
        self.git("clone", "--recurse-submodules", str(self.source), self.checkout)
        self.git("-C", self.checkout, "config", "submodule.nested.ignore", "all")
        payload = self.checkout / "nested/payload.txt"
        payload.write_bytes(b"user's nested changes\r\n")
        config = (self.checkout / ".git/config").read_bytes()
        nested_config = (self.checkout / ".git/modules/nested/config").read_bytes()
        with self.assertRaisesRegex(SystemExit, "local changes; left untouched"):
            self.run_bootstrap()
        self.assertEqual(payload.read_bytes(), b"user's nested changes\r\n")
        self.assertEqual((self.checkout / ".git/config").read_bytes(), config)
        self.assertEqual((self.checkout / ".git/modules/nested/config").read_bytes(), nested_config)


if __name__ == "__main__":
    unittest.main()
