"""Data merge safety regressions. Run: python extras/test_prepare_data.py"""
import argparse
import hashlib
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "prepare", Path(__file__).resolve().parents[1] / "prepare_gunbros_data_folder.py")
prepare = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = prepare
spec.loader.exec_module(prepare)


class PreparationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.args = argparse.Namespace(original_dir=root / "original",
            patched_dir=root / "patched", logos_dir=root / "logos",
            output_dir=root / "output", quality=5, dry_run=True)
        self.original = self.args.original_dir / "gunbros_free"
        self.library = b"test native library"
        self.hash_patch = patch.object(prepare, "SUPPORTED_SO_SHA256",
                                      hashlib.sha256(self.library).hexdigest())
        self.hash_patch.start()
        self.addCleanup(self.hash_patch.stop)
        self.write(self.original / "files/apk/lib/armeabi/libandroidplatformjni.so", self.library)
        self.write(self.args.patched_dir / "libandroidplatformjni.so", self.library)
        self.write(self.original / "files/apk/assets/gunbros.big", b"original")
        for name in ["pack0_core_wvga.big", "packTOC_wvga.dat"] + [
                f"pack{i}_wvga.big" for i in range(1, 13)] + [
                f"{track}.mp3" for track in prepare.REQUIRED_MUSIC_TRACKS]:
            self.write(self.original / "files" / name, b"fixture")
        for name in prepare.LOGOS:
            self.write(self.args.logos_dir / name, b"\x89PNG\r\n\x1a\nfixture")

    def write(self, path, data):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def test_normalizes_and_overlays_without_android_or_saves(self):
        self.write(self.args.patched_dir / "assets/gunbros.big", b"patched")
        self.write(self.args.patched_dir / "files/save.dat", b"private")
        plan, report = prepare.preparation_plan(self.args)
        self.assertEqual(plan["gunbros_free/assets/gunbros.big"].read_bytes(), b"patched")
        self.assertTrue(report["files"]["gunbros_free/assets/gunbros.big"]["changed_by_overlay"])
        self.assertNotIn("gunbros_free/files/save.dat", plan)
        self.assertFalse(any("/apk/" in name for name in plan))
        self.assertFalse(self.args.output_dir.exists())

    def test_mismatched_binary_rejected_before_output(self):
        self.write(self.args.patched_dir / "libandroidplatformjni.so", b"wrong version")
        with self.assertRaisesRegex(ValueError, "Unsupported library"):
            prepare.preparation_plan(self.args)
        self.assertFalse(self.args.output_dir.exists())

    def test_missing_original_pack_rejected(self):
        (self.original / "files/pack12_wvga.big").unlink()
        with self.assertRaisesRegex(ValueError, "Original data is incomplete"):
            prepare.preparation_plan(self.args)

    def test_existing_output_preserved(self):
        self.write(self.args.output_dir / "keep", b"keep")
        with self.assertRaisesRegex(ValueError, "already exists"):
            prepare.preparation_plan(self.args)
        self.assertEqual((self.args.output_dir / "keep").read_bytes(), b"keep")

    def test_overlapping_output_rejected(self):
        self.args.output_dir = self.original / "new"
        with self.assertRaisesRegex(ValueError, "separate"):
            prepare.preparation_plan(self.args)

    def test_dry_run_does_not_create_output(self):
        with patch.object(prepare, "parse_args", return_value=self.args):
            self.assertEqual(prepare.main(), 0)
        self.assertFalse(self.args.output_dir.exists())

    def test_two_folder_mode_needs_no_patched_folder(self):
        self.args.apk_dir = self.original / "files/apk"
        self.args.files_dir = self.original / "files"
        self.args.patched_dir = Path(self.temp.name) / "does-not-exist"
        plan, report = prepare.preparation_plan(self.args)
        self.assertIn("gunbros_free/assets/gunbros.big", plan)
        self.assertIn("gunbros_free/files/pack12_wvga.big", plan)
        self.assertFalse(any("changed_by_overlay" in item for item in report["files"].values()))

    def test_two_folder_mode_rejects_missing_apk_selection(self):
        self.args.files_dir = self.original / "files"
        self.args.apk_dir = None
        with self.assertRaisesRegex(ValueError, "both"):
            prepare.preparation_plan(self.args)


if __name__ == "__main__":
    unittest.main()
