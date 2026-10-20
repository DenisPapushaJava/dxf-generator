"""Offline release gates: real Git archives, PE fixtures and no GitHub writes."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from package_release import package_release, verify_bundle
from release_common import changelog, config_hash, qt_config
from release_tools import create_draft, ensure_new_release, preflight, verify_uploaded_draft
from test_pe_audit import fixture


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root / "build"
        self.build.mkdir()
        self.qt = self.build / "qt.tar.gz"
        self.qt.write_bytes(b"complete pinned Qt archive fixture")
        config = qt_config()
        config["source_sha256"] = hashlib.sha256(self.qt.read_bytes()).hexdigest()
        files = {
            ".gitignore": "/build/\n",
            "CMakeLists.txt": "project(DxfGenerator VERSION 0.3.0 LANGUAGES CXX)\n",
            "CHANGELOG.md": "## Не выпущено\nLater\n## 0.3.0 — today\nOur changes\n## 0.2.0\nOld\n",
            "cmake/qt-static.json": json.dumps(config),
            "LICENSE": "Application MIT fixture\n",
            "third_party/qxlsx/LICENSE": "QXlsx MIT fixture\n",
            "resources/licenses/THIRD-PARTY-NOTICES.txt": "Full notices fixture\n",
            "docs/rebuilding.md": "Rebuilding instructions fixture\n",
            "examples/Детали.csv": "Имя;Тип;Размер 1;Размер 2;Толщина;Количество\n;К;100;;5;1\n",
        }
        for name, text in files.items():
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8")
        self.git("init", "--quiet")
        self.git("add", ".")
        self.git("-c", "user.name=Release test", "-c", "user.email=test@example.invalid",
                 "commit", "--quiet", "-m", "Fixture")
        self.commit = self.git("rev-parse", "HEAD").strip()
        self.exe = self.build / "checked.exe"
        self.exe.write_bytes(fixture("KERNEL32.dll"))
        self.info = {
            "application_commit": self.commit, "version": "0.3.0",
            "exe_sha256": hashlib.sha256(self.exe.read_bytes()).hexdigest(),
            "exe_size": self.exe.stat().st_size,
            "qt_version": config["version"], "qt_commit": config["commit"],
            "qt_config_sha256": config_hash(config),
            "workflow_url": "https://github.com/DenisPapushaJava/dxf-generator/actions/runs/123",
            "checks": ["ctest", "pe_audit", "winhttp_live_check"],
        }
        self.record = self.build / "build-info.json"
        self.output = self.build / "release"
        self.write_record()

    def git(self, *args):
        return subprocess.check_output(["git", *args], cwd=self.root, text=True)

    def write_record(self):
        self.record.write_text(json.dumps(self.info), encoding="utf-8")

    def package(self):
        return package_release(self.exe, self.record, self.qt, self.output, root=self.root)

    def test_complete_bundle_has_one_commit_checksums_examples_and_full_sources(self):
        self.package()
        info = verify_bundle(self.output)
        self.assertEqual(info["application_commit"], self.commit)
        with zipfile.ZipFile(self.output / "dxf-generator-v0.3.0-sources.zip") as archive:
            self.assertEqual(archive.testzip(), None)
            self.assertEqual(json.loads(archive.read("source-manifest.json"))["application_commit"], self.commit)
            self.assertEqual(archive.read("qtbase-6.10.3-7ddbc87.tar.gz"), self.qt.read_bytes())
            self.assertIn("application/LICENSE", archive.namelist())
            self.assertIn("application/docs/rebuilding.md", archive.namelist())
        with zipfile.ZipFile(self.output / "dxf-generator-v0.3.0-examples.zip") as archive:
            self.assertIn("Детали.csv", archive.namelist())
        notes = (self.output / "release-notes.md").read_text(encoding="utf-8")
        self.assertIn("Our changes", notes)
        self.assertNotIn("Old", notes)
        self.assertNotIn("Later", notes)

    def test_foreign_commit_version_qt_or_missing_checks_rejected_before_writes(self):
        for field, value in (("application_commit", "0" * 40), ("version", "0.2.0"),
                             ("qt_config_sha256", "0" * 64), ("checks", [])):
            with self.subTest(field=field):
                old = self.info[field]
                self.info[field] = value
                self.write_record()
                with self.assertRaises(ValueError):
                    self.package()
                self.assertFalse(self.output.exists())
                self.info[field] = old

    def test_modified_exe_rejected(self):
        self.exe.write_bytes(self.exe.read_bytes() + b"changed")
        with self.assertRaises(ValueError):
            self.package()
        self.assertFalse(self.output.exists())

    def test_wrong_qt_archive_rejected(self):
        self.qt.write_bytes(b"wrong archive")
        with self.assertRaises(ValueError):
            self.package()
        self.assertFalse(self.output.exists())

    def test_nonportable_exe_rejected_even_with_matching_hash(self):
        self.exe.write_bytes(fixture("Qt6Core.dll"))
        self.info["exe_sha256"] = hashlib.sha256(self.exe.read_bytes()).hexdigest()
        self.info["exe_size"] = self.exe.stat().st_size
        self.write_record()
        with self.assertRaisesRegex(ValueError, "non-system"):
            self.package()

    def test_uncommitted_sources_rejected(self):
        (self.root / "LICENSE").write_text("uncommitted change")
        with self.assertRaisesRegex(ValueError, "clean"):
            self.package()
        self.assertFalse(self.output.exists())

    def test_existing_destination_is_never_overwritten(self):
        self.output.mkdir()
        sentinel = self.output / "keep.txt"
        sentinel.write_text("keep")
        with self.assertRaises(FileExistsError):
            self.package()
        self.assertEqual(sentinel.read_text(), "keep")

    def test_missing_version_notes_rejected(self):
        with self.assertRaises(ValueError):
            changelog("9.9.9", self.commit, self.root)

    def test_tampered_bundle_rejected_before_github_call(self):
        self.package()
        (self.output / "dxf-generator-windows-x64.exe").write_bytes(b"bad")
        with patch("release_tools.subprocess.run") as run:
            with self.assertRaises(ValueError):
                create_draft(self.output)
            run.assert_not_called()

    def test_publication_command_is_always_a_new_draft_for_exact_commit(self):
        self.package()
        with patch("release_tools.checked_revision", return_value=self.commit), \
                patch("release_tools.project_version", return_value="0.3.0"), \
                patch("release_tools.ensure_new_release"), \
                patch("release_tools.all_releases", return_value=[{"tag_name": "v0.3.0"}]), \
                patch("release_tools.verify_uploaded_draft"), \
                patch("release_tools.subprocess.run") as run:
            create_draft(self.output)
        command = run.call_args.args[0]
        self.assertEqual(command[:4], ["gh", "release", "create", "v0.3.0"])
        self.assertIn("--draft", command)
        self.assertEqual(command[command.index("--target") + 1], self.commit)
        self.assertNotIn("--clobber", command)
        self.assertNotIn("--latest", command)

    def test_existing_release_or_draft_cannot_be_changed(self):
        for draft in (False, True):
            with self.subTest(draft=draft), patch("release_tools.subprocess.check_output",
                    return_value=json.dumps([[{"tag_name": "v0.3.0", "draft": draft}]])), \
                    patch("release_tools.subprocess.run") as run:
                with self.assertRaises(ValueError):
                    ensure_new_release("0.3.0", self.commit, self.root)
                run.assert_not_called()

    def test_existing_tag_must_match_the_build_commit(self):
        wrong = subprocess.CompletedProcess([], 0, "0" * 40 + "\trefs/tags/v0.3.0\n", "")
        with patch("release_tools.subprocess.check_output", return_value="[[]]"), \
                patch("release_tools.subprocess.run", return_value=wrong):
            with self.assertRaisesRegex(ValueError, "different commit"):
                ensure_new_release("0.3.0", self.commit, self.root)

    def test_completed_ci_can_be_reused_without_github_write(self):
        with patch.dict(os.environ, {"GITHUB_SHA": self.commit}), \
                patch("release_tools.gh_json", return_value={"workflow_runs": [
                {"head_sha": self.commit, "conclusion": "success", "event": "push"}]}):
            result = preflight(reuse_ci=True, root=self.root)
        self.assertEqual(result["linux_required"], "false")
        self.assertEqual(result["commit"], self.commit)
        for runs in ([], [{"head_sha": "0" * 40, "conclusion": "success", "event": "push"}],
                     [{"head_sha": self.commit, "conclusion": "success", "event": "pull_request"}]):
            with self.subTest(runs=runs), patch.dict(os.environ, {"GITHUB_SHA": self.commit}), \
                    patch("release_tools.gh_json", return_value={"workflow_runs": runs}):
                self.assertEqual(preflight(reuse_ci=True, root=self.root)["linux_required"], "true")

    def test_uploaded_assets_need_matching_github_sha256_and_complete_materials(self):
        self.package()
        info = verify_bundle(self.output)
        release = {"draft": True, "target_commitish": self.commit, "tag_name": "v0.3.0",
                   "assets": [{"name": name, "state": "uploaded",
                               "size": (self.output / name).stat().st_size,
                               "digest": "sha256:" + hashlib.sha256((self.output / name).read_bytes()).hexdigest()}
                              for name in info["assets"] + ["SHA256SUMS.txt"]]}
        verify_uploaded_draft(release, self.output, info)
        release["assets"][0]["digest"] = "sha256:" + "0" * 64
        with self.assertRaises(ValueError):
            verify_uploaded_draft(release, self.output, info)
        release["assets"].pop()
        with self.assertRaises(ValueError):
            verify_uploaded_draft(release, self.output, info)


if __name__ == "__main__":
    unittest.main()
