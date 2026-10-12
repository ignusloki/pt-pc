import importlib.util
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("macos_publish", Path(__file__).resolve().parents[1] / "tools/macos/publish_artifacts.py")
publish = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publish)


class PublishTests(unittest.TestCase):
    def create(self, root, setup=False):
        for arch in ("arm64", "x64"):
            folder = root / arch
            folder.mkdir()
            for ext in ("zip", "dmg"):
                (folder / f"P.T.PC.Port-macOS-{arch}.{ext}").write_bytes(b"fixture")
            if setup:
                (folder / f"P.T.PC.Port.Setup-macOS-{arch}.zip").write_bytes(b"fixture")

    def test_resolves_annotated_tags_to_commits(self):
        with patch.object(publish, "gh_json", side_effect=[{"conclusion": "success", "head_sha": "commit-sha"}, {"sha": "commit-sha"}]) as request:
            self.assertEqual(publish.verify_source("owner/repo", 123, "v1.0.6"), "commit-sha")
            self.assertEqual(request.call_args_list[-1].args[0], "repos/owner/repo/commits/v1.0.6")

    def test_rejects_failed_run(self):
        with patch.object(publish, "gh_json", return_value={"conclusion": "failure"}):
            with self.assertRaises(RuntimeError): publish.verify_source("owner/repo", 123, "v1")

    def test_rejects_different_source(self):
        with patch.object(publish, "gh_json", side_effect=[{"conclusion": "success", "head_sha": "a"}, {"sha": "b"}]):
            with self.assertRaises(RuntimeError): publish.verify_source("owner/repo", 123, "v1")

    def test_upstream_portable_pair(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.create(root)
            self.assertEqual(len(publish.collect_assets(root)), 4)

    def test_fork_includes_both_installers(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.create(root, setup=True)
            self.assertEqual(len(publish.collect_assets(root, require_setup=True)), 6)

    def test_fork_rejects_missing_installers(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.create(root)
            with self.assertRaises(RuntimeError): publish.collect_assets(root, require_setup=True)

    def test_rejects_one_installer(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.create(root)
            (root / "P.T.PC.Port.Setup-macOS-arm64.zip").write_bytes(b"fixture")
            with self.assertRaises(RuntimeError): publish.collect_assets(root)

    def test_rejects_duplicate_asset(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.create(root)
            (root / "P.T.PC.Port-macOS-arm64.zip").write_bytes(b"fixture")
            with self.assertRaises(RuntimeError): publish.collect_assets(root)


if __name__ == "__main__": unittest.main()
