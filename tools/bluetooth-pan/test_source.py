"""Offline regression tests for reconstructing a complete, pinned IDF source tree."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from source import blob_hash, hydrate, verify_submodules


class SourceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "idf"
        self.cache = Path(self.temp.name) / "cache"
        self.root.mkdir()
        (self.cache / "blobs").mkdir(parents=True)

    def manifest(self, entries):
        (self.cache / "tree.json").write_text(json.dumps({"tree": entries}))

    def file(self, name, data, mode="100644"):
        sha = blob_hash(data)
        (self.cache / "blobs" / sha).write_bytes(data)
        return dict(path=name, type="blob", mode=mode, sha=sha)

    def hydrate(self):
        # No test may silently fall back to an online download.
        with patch("source.fetch", side_effect=AssertionError("Unexpected download")), \
             patch("source.shutil.which", return_value=None):
            return hydrate(self.root, self.cache, "test/idf", "pinned")

    def test_replaces_mixed_revision_and_creates_empty_files(self):
        (self.root / "startup.c").write_text("old startup")
        self.manifest([self.file("startup.c", b"correct startup"), self.file("empty", b"")])
        self.hydrate()
        self.assertEqual((self.root / "startup.c").read_bytes(), b"correct startup")
        self.assertTrue((self.root / "empty").is_file())
        self.hydrate()  # Repeated verification must be offline and idempotent.

    def test_removes_stale_sources_but_preserves_package_and_submodules(self):
        for name in ("stale.c", "package.json", ".piopm", "version.txt", "module/library.a"):
            target = self.root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(b"original")
        self.manifest([dict(path="module", mode="160000", type="commit", sha="pinned")])
        self.hydrate()
        self.assertFalse((self.root / "stale.c").exists())
        for name in ("package.json", ".piopm", "version.txt", "module/library.a"):
            self.assertEqual((self.root / name).read_bytes(), b"original")

    def test_restores_modes_and_symlinks_even_when_content_matches(self):
        (self.root / "script").write_bytes(b"execute")
        (self.root / "link").write_bytes(b"script")
        self.manifest([self.file("script", b"execute", "100755"), self.file("link", b"script", "120000")])
        self.hydrate()
        self.assertEqual((self.root / "script").stat().st_mode & 0o777, 0o755)
        self.assertTrue((self.root / "link").is_symlink())
        self.assertEqual((self.root / "link").read_bytes(), b"execute")

    def test_rejects_incomplete_manifest_before_removing_files(self):
        (self.root / "keep").touch()
        (self.cache / "tree.json").write_text('{"truncated":true,"tree":[]}')
        with self.assertRaisesRegex(RuntimeError, "Incomplete"):
            self.hydrate()
        self.assertTrue((self.root / "keep").exists())

    def test_rejects_corrupt_download(self):
        entry = self.file("startup.c", b"expected")
        (self.cache / "blobs" / entry["sha"]).unlink()
        self.manifest([entry])
        with patch("source.fetch", side_effect=lambda url, path: path.write_bytes(b"corrupt")), \
             patch("source.shutil.which", return_value=None):
            with self.assertRaisesRegex(RuntimeError, "hash mismatch"):
                hydrate(self.root, self.cache, "test/idf", "pinned")
        self.assertFalse((self.root / "startup.c").exists())

    def test_submodules_must_match_the_pinned_idf_revision(self):
        tree = {"tree": [dict(path="controller", mode="160000", sha="correct")]}
        verify_submodules(tree, [dict(path="controller", sha="correct")])
        for path, sha in (("controller", "wrong"), ("unknown", "correct")):
            with self.assertRaisesRegex(RuntimeError, "Submodule does not match"):
                verify_submodules(tree, [dict(path=path, sha=sha)])


if __name__ == "__main__":
    unittest.main()
