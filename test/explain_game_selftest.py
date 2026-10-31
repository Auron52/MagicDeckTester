#!/usr/bin/env python3
"""Engine-free reproduction-setting checks for explain_game.py."""
import json
from pathlib import Path
import tempfile
import subprocess
import re
import unittest
from unittest.mock import patch

import explain_game as explain


class ExplainGameSettingsTest(unittest.TestCase):
    def config(self):
        return dict(DECK_FILE={"demo":"deck.cod"}, DECK_PROF={"demo":"deck.profile.json"},
                    cases={"regression":["demo 5 3003 200 40", "demo 3 2002 150 10",
                                         "demo 0 2002 1000 10", "demo2hg 5 2002 75 40"]})

    def test_saved_play_job_keeps_depth_omitted_and_exact_chunk_identity(self):
        with tempfile.TemporaryDirectory() as td, patch.object(explain, "ROOT", td):
            p = Path(td) / "test/logs/regression/manifest.json"
            p.parent.mkdir(parents=True)
            saved = dict(name="demo_regression_d5_s3003", deck="saved.cod", profile="saved.profile.json",
                         games=10, seed=3143, game_index=140, budget_ms=40, starting_life=30,
                         opponent_heads=2, flags={"MTG_ENERGY_AURA_PIP":False}, weight=9)
            p.write_text(json.dumps({"jobs":[saved]}))
            r = explain.resolve("regression", saved["name"], self.config())
            j = explain.single_game_job(r, 143)
            self.assertNotIn("depth", j)
            self.assertNotIn("ignore_play_profile", j)
            self.assertEqual((j["seed"], j["game_index"], j["games"]), (3146,143,1))
            self.assertEqual((j["starting_life"], j["opponent_heads"], j["flags"]),
                             (30,2,saved["flags"]))
            self.assertEqual(j["profile"], "saved.profile.json")
            self.assertNotIn("weight", j)
            with self.assertRaises(ValueError):
                explain.single_game_job(r, 150)

    def test_matrix_fallback_mirrors_play_and_shallow_rows(self):
        with tempfile.TemporaryDirectory() as td, patch.object(explain, "ROOT", td):
            for depth, seed in [(5,3003), (3,2002), (0,2002)]:
                r = explain.resolve("regression", f"demo_regression_d{depth}_s{seed}", self.config())
                j = explain.single_game_job(r, 3)
                self.assertEqual((j["seed"],j["game_index"]), (seed+3,3))
                if depth == 5:
                    self.assertNotIn("depth", j)
                    self.assertNotIn("ignore_play_profile", j)
                else:
                    self.assertEqual(j["depth"], depth)
                    self.assertTrue(j["ignore_play_profile"])
                if depth == 0:
                    self.assertEqual(j["budget_ms"], 0)
            r = explain.resolve("regression", "demo2hg_regression_d5_s2002", self.config())
            j = explain.single_game_job(r, 3)
            self.assertEqual((j["starting_life"],j["opponent_heads"]), (30,2))

    def test_replay_command_uses_semantic_batch_job_without_cli_depth_pin(self):
        # Bind the adapter's flags to the actual batch-only parser, not a mock's invented CLI.
        main_source = (Path(__file__).resolve().parents[1] / "src/main.cpp").read_text()
        start = main_source.index('if (std::string(argv[1]) == "--batch")')
        parser = main_source[start:main_source.index('if (gate_probe)', start)]
        accepted = set(re.findall(r'flag\s*==\s*"(--[a-z-]+)"', parser))
        self.assertIn("--game-trace-dir", accepted)
        with tempfile.TemporaryDirectory() as td, patch.object(explain, "ROOT", td):
            r = explain.resolve("regression", "demo_regression_d5_s3003", self.config())

            def fake_run(cmd, **kwargs):
                self.assertIn("--batch", cmd)
                self.assertNotIn("--depth", cmd)
                self.assertNotIn("--ignore-play-profile", cmd)
                self.assertNotIn("--log-dir", cmd)
                self.assertNotIn("--full-trace", cmd)
                self.assertNotIn("timeout", kwargs)
                for flag in [s for s in cmd[2:] if s.startswith("--")]:
                    self.assertIn(flag, accepted)
                j = json.loads(Path(cmd[cmd.index("--batch")+1]).read_text())["jobs"][0]
                self.assertEqual((j["seed"], j["game_index"]), (3146,143))
                self.assertNotIn("depth", j)
                traces = Path(cmd[cmd.index("--game-trace-dir")+1])
                traces.mkdir(parents=True)
                (traces / "actual.json").write_text(json.dumps({"result":{"winner":"player","turn":6},"turns":[]}))
                return subprocess.CompletedProcess(cmd, 0, stdout="", stderr="")

            with patch.object(explain.subprocess, "run", side_effect=fake_run) as mocked:
                result = explain.run_game("NEVER_EXECUTED", r, 143)
            self.assertEqual(result["win"], 6)
            mocked.assert_called_once()

    def test_engine_failure_cannot_be_parsed_as_success(self):
        with tempfile.TemporaryDirectory() as td, patch.object(explain, "ROOT", td):
            r = explain.resolve("regression", "demo_regression_d5_s3003", self.config())
            failed = subprocess.CompletedProcess([], 2, stdout="", stderr="bad batch")
            with patch.object(explain.subprocess, "run", return_value=failed):
                with self.assertRaisesRegex(RuntimeError, "replay exited 2: bad batch"):
                    explain.run_game("NEVER_EXECUTED", r, 143)


if __name__ == "__main__":
    unittest.main()
