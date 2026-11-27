#!/usr/bin/env python3
"""Engine-free checks of play_setting_stage.py's escalation-beam admission (the flip arm, the verdict log, --write).

    python3 test/play_setting_beam_selftest.py
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "scripts"))
import play_setting_stage as ps   # noqa: E402

ENABLED = {"enabled": True, "target_depth": 5, "budget_ms": 30, "note": "x"}


class BeamAdmission(unittest.TestCase):
    def test_flip_removes_a_live_beam(self):
        live = {"value_play": dict(ENABLED, beam_width=3, beam_leafdepth=2), "model": 1}
        doc, on = ps.beam_flip(live, dict(ENABLED))
        self.assertFalse(on)
        self.assertNotIn("beam_width", doc["value_play"])
        self.assertNotIn("beam_leafdepth", doc["value_play"])
        self.assertEqual(doc["model"], 1)
        self.assertEqual(live["value_play"]["beam_width"], 3)          # the live doc is not mutated

    def test_flip_adds_the_beam_to_a_block_without_one(self):
        for vp in (dict(ENABLED), dict(ENABLED, beam_width=0, beam_leafdepth=2)):   # Hinata's beam_width 0 is no beam
            doc, on = ps.beam_flip({"value_play": vp}, dict(ENABLED))
            self.assertTrue(on)
            self.assertEqual((doc["value_play"]["beam_width"], doc["value_play"]["beam_leafdepth"]), (3, 2))

    def test_flip_without_an_enabled_block_enables_the_centre(self):
        centre = dict(ENABLED, target_depth=5, budget_ms=20)
        doc, on = ps.beam_flip({"value_play": {}, "model": 2}, centre)
        self.assertTrue(on)
        self.assertTrue(doc["value_play"]["enabled"])
        self.assertEqual(doc["value_play"]["beam_width"], 3)
        self.assertEqual(doc["model"], 2)

    def test_install_admitted_keeps_or_adds(self):
        beamed = {"value_play": dict(ENABLED, beam_width=4, beam_leafdepth=1)}
        doc, changed = ps.beam_install(beamed, True, None)
        self.assertFalse(changed)
        self.assertEqual(doc["value_play"]["beam_width"], 4)             # an existing width is kept
        doc, changed = ps.beam_install({"value_play": dict(ENABLED)}, True, None)
        self.assertTrue(changed)
        self.assertEqual((doc["value_play"]["beam_width"], doc["value_play"]["beam_leafdepth"]), (3, 2))

    def test_install_admitted_on_a_disabled_block_takes_the_measured_flip(self):
        flip = {"value_play": dict(ENABLED, beam_width=3, beam_leafdepth=2)}
        doc, changed = ps.beam_install({"value_play": {}}, True, flip)
        self.assertTrue(changed)
        self.assertIs(doc, flip)

    def test_install_refused_removes_both_keys(self):
        for vp in (dict(ENABLED, beam_width=3, beam_leafdepth=2), dict(ENABLED, beam_width=0, beam_leafdepth=2),
                   dict(ENABLED, beam_leafdepth=2)):
            doc, changed = ps.beam_install({"value_play": vp}, False, None)
            self.assertTrue(changed)
            self.assertFalse(any(k in doc["value_play"] for k in ps.BEAM_KEYS))
            self.assertEqual(doc["value_play"]["note"], "x")
        doc, changed = ps.beam_install({"value_play": dict(ENABLED)}, False, None)
        self.assertFalse(changed)

    def test_verdict_log_renames_the_two_arms(self):
        lines = ["cg-live_s7505000: played=1000 avg=4.5 digest=a ms=1 units=10",
                 "cg-xbeam_d5b30_s7505000: played=1000 avg=4.4 digest=b ms=1 units=14",
                 "cg-d6b30_s7505000: played=1000 avg=4.4 digest=c ms=1 units=12",
                 "other-live_s7505000: played=1000 avg=9.9 digest=d ms=1 units=1",
                 "[play] cg-live_s7505000 depth=5"]
        out = ps.beam_log_lines(lines, "cg", "xbeam_d5b30", "live")     # live carries the beam; the flip is no-beam
        self.assertEqual(out, ["beam_s7505000: played=1000 avg=4.5 digest=a ms=1 units=10",
                               "live_s7505000: played=1000 avg=4.4 digest=b ms=1 units=14"])

    def test_the_picking_rule_reads_the_renamed_log(self):
        """End to end through vlq_adopt_pick.py: a cheaper beam of equal quality is admitted; a dearer one, or one
        notably worse (+0.01 turns on every seed), is refused."""
        import subprocess, tempfile
        def run(beam_units, beam_delta):
            lines = []
            for i, s in enumerate(ps.SEEDS):
                a = 4.5 + 0.01 * (i % 3)
                lines.append(f"cg-live_s{s}: played=1000 avg={a:.4f} digest=n{i} ms=1 units=1000")
                lines.append(f"cg-xbeam_d5b30_s{s}: played=1000 avg={a + beam_delta + 0.001 * (i % 2):.4f} digest=b{i} "
                             f"ms=1 units={beam_units}")
            with tempfile.NamedTemporaryFile("w", suffix=".log", delete=False) as f:
                f.write("\n".join(ps.beam_log_lines(lines, "cg", "live", "xbeam_d5b30")) + "\n")
            r = subprocess.run([sys.executable, ps.PICK, f.name, "--tol", str(ps.TOL), "--candidates", "beam"],
                               capture_output=True, text=True)
            os.unlink(f.name)
            return r.returncode
        self.assertEqual(run(700, 0.0), 0)      # cheaper, same quality: admitted
        self.assertEqual(run(1300, 0.0), 2)     # dearer: refused
        self.assertEqual(run(700, 0.01), 2)     # cheaper but notably worse: refused


if __name__ == "__main__":
    unittest.main()
