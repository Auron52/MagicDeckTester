import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("suite_gate", ROOT / "scripts/suite_gate.py")
suite_gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(suite_gate)


class SuiteCostShapeTests(unittest.TestCase):
    def test_seeds_pool_by_depth_and_budget_before_taking_maximum(self):
        cases = [
            ("sample", 5, 2002, 10, 20),
            ("sample", 5, 3003, 30, 20),
            ("sample", 5, 4004, 5, 30),
            ("sample", 3, 2002, 5, 10),
            ("sample", 5, 30021, 1, 0),
        ]
        jobs = {
            "sample_regression_d5_s2002": (10, 1000),
            "sample_regression_d5_s3003": (30, 6000),
            "sample_regression_d5_s4004": (5, 800),
            "sample_regression_d3_s2002": (5, 750),
            "sample_regression_d5_s30021": (1, 100000),
        }
        with patch.object(suite_gate, "_cases", return_value=cases), \
             patch.object(suite_gate, "parse_batch_log", return_value=jobs):
            result = suite_gate.measure("unused", "sample", run=False)
        self.assertEqual(result["cost_per_game_ms"], 175.0)
        self.assertEqual(result["worst_case"], "sample_regression_d5_b20")
        self.assertEqual(result["total_core_ms"], 108550)
        self.assertEqual(result["cost_metric"], suite_gate.COST_METRIC)


if __name__ == "__main__":
    unittest.main()
