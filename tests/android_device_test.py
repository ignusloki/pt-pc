"""Avoid false compatibility results from Android Vulkan capability dumps."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools/android"))
from collect_device import analyze_vulkan, parse_meminfo, requirements


class AndroidCapabilitiesTest(unittest.TestCase):
    def setUp(self):
        features, limits = requirements()
        self.device = {"properties": {"deviceName": "Fixture GPU", "apiVersion": 4206871.0},
                       "features": {key: 1.0 for key in features},
                       "core12": {"properties": {key: 65536.0 for key in limits}}}

    def analyze(self, device=None):
        return analyze_vulkan({"devices": [device or self.device]})["devices"][0]

    def test_android_integral_floats_are_understood(self):
        report = self.analyze()
        self.assertEqual(report["api_version"], [1, 3, 279])
        self.assertEqual(report["queried_requirements"], "pass")

    def test_partial_bc_family_does_not_fail_android_mandatory_features(self):
        self.device["features"]["textureCompressionBC"] = 0.0
        self.assertEqual(self.analyze()["failed"], [])
        self.assertEqual(self.analyze()["optional_full_bc_family"]["reported"], 0)

    def test_missing_core_fields_are_unknown_not_unsupported(self):
        del self.device["features"]["dynamicRendering"]
        report = self.analyze()
        self.assertEqual(report["queried_requirements"], "incomplete")
        self.assertEqual(report["failed"], [])
        self.assertIn("dynamicRendering", report["unknown"])

    def test_older_api_is_rejected_even_with_other_features(self):
        self.device["properties"]["apiVersion"] = float((1 << 22) | (2 << 12))
        self.assertEqual(self.analyze()["failed"], ["Vulkan API"])

    def test_small_texture_table_limit_is_rejected(self):
        self.device["core12"]["properties"]["maxPerStageDescriptorUpdateAfterBindSamplers"] = 4096.0
        self.assertIn("maxPerStageDescriptorUpdateAfterBindSamplers", self.analyze()["failed"])

    def test_conflicting_duplicate_features_are_not_accepted(self):
        self.device["core12"]["features"] = {"dynamicRendering": 0.0}
        self.assertIn("dynamicRendering", self.analyze()["unknown"])

    def test_fractional_feature_value_is_not_accepted(self):
        self.device["features"]["dynamicRendering"] = 0.5
        self.assertIn("dynamicRendering", self.analyze()["unknown"])

    def test_system_memory_is_kept_separate_from_swap(self):
        info = parse_meminfo("MemTotal: 7445796 kB\nMemAvailable: 3497552 kB\nSwapTotal: 8388604 kB\n")
        self.assertEqual(info["MemAvailable_bytes"], 3581493248)
        self.assertEqual(info["SwapTotal_bytes"], 8589930496)


if __name__ == "__main__":
    unittest.main()
