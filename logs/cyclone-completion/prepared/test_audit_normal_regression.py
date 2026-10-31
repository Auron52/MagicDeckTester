#!/usr/bin/env python3
"""Synthetic audit checks; the fake binary bytes are never executed."""
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

import audit_normal_regression as audit


class NormalAuditTest(unittest.TestCase):
    def test_strict_rows(self):
        self.assertEqual(audit.wins(b"50 3 a\n51 5 b\n"), {50:(3,"a"), 51:(5,"b")})
        for raw in [b"0 3 a\n0 4 b\n", b"0 3 xyz\n", b"0 3 a trailing\n"]:
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                audit.wins(raw)

    def test_completed_pair_offset_and_failure_guards(self):
        with tempfile.TemporaryDirectory() as td:
            w = Path(td)
            run = w / "test/logs/smoke"
            (run / "wins").mkdir(parents=True)
            key = "demo_smoke_d3_s1001"
            job = dict(name=key, seed=1051, game_index=50, games=2, depth=3,
                       budget_ms=10, weight=99, abandon_units=9)
            (run / "manifest.json").write_text(json.dumps({"jobs":[job]}))
            old, new = b"50 3 a\n51 5 b\n", b"50 4 c\n51 5 d\n"
            (run / "wins" / f"{key}.wins").write_bytes(new)
            of, nf = audit.fingerprint(audit.wins(old), 8), audit.fingerprint(audit.wins(new), 8)
            (run / "batch.log").write_text(f"{key}: played=2 avg={nf[0]:.4f} digest={nf[1]} units=99\n")
            fd_line = ("[fd-diverge] seed=1082 job=niv_smoke_d5_s1001 gi=81 realized_win=6 "
                       "predicted_win=5 proven_at_turn=1 leaf_est=none blind_salt=-1")
            nonconv_line = "[nonconv] job=demo_smoke_d3_s1001 gi=50 seed=1051 turn=3"
            (run / "batch.err").write_text(fd_line + "\n" + nonconv_line + "\n")
            (run / "mtg.run").write_bytes(b"FAKE BYTES NEVER EXECUTED")
            (w / "test/regression_result_smoke.txt").write_text("Result: 0 passed, 1 failed, 0 new\n")

            def fake_git(worktree, *args):
                if args == ("rev-parse", "HEAD:src"):
                    return b"approved\n"
                if args[0] == "status":
                    return b""
                if args[0] == "rev-parse":
                    return b"baselinecommit\n"
                if args == ("show", "baselinecommit:test/regression_gt.txt"):
                    return f"{key}={of[0]:.4f}/{of[1]}\n".encode()
                if args == ("show", f"baselinecommit:test/gt_logs/{key}.wins"):
                    return old
                raise AssertionError(args)

            def invoke(out):
                argv = ["audit", "--worktree", str(w), "--mode", "smoke", "--baseline-ref", "905",
                        "--expected-source", "approved", "--output", str(out)]
                with patch.object(audit, "git", fake_git), patch.object(sys, "argv", argv), contextlib.redirect_stdout(io.StringIO()):
                    audit.main()

            out = w / "audit"
            invoke(out)
            report = json.loads((out / "audit.json").read_text())
            self.assertEqual(report["tallies"]["searched"]["slower"], 1)
            self.assertEqual(report["tallies"]["searched"]["same_score_play_change"], 1)
            self.assertEqual(report["diagnostic_counts"], dict(fd=1, nonconv=1, counter_overflow=0))
            self.assertEqual(report["diagnostic_attention"][0], dict(line=1, text=fd_line, kinds=["fd"]))
            self.assertEqual(report["diagnostic_attention"][1], dict(line=2, text=nonconv_line, kinds=["nonconv"]))
            self.assertEqual((report["changed"][0]["actual_seed"], report["changed"][0]["gi"]), (1051, 50))
            rec = json.loads((out / "d8-recovery.UNSELECTED.json").read_text())["jobs"][0]
            self.assertEqual((rec["seed"], rec["game_index"], rec["depth"], rec["budget_ms"]), (1051,50,8,0))
            self.assertNotIn("weight", rec)
            self.assertNotIn("abandon_units", rec)
            self.assertEqual((out / "baseline" / f"{key}.wins").read_bytes(), old)
            with self.assertRaises(FileExistsError):
                invoke(out)
            (run / "wins" / f"{key}.wins").write_bytes(b"50 4 c\n")
            with self.assertRaisesRegex(ValueError, "GI coverage"):
                invoke(w / "bad-output")
            self.assertFalse((w / "bad-output").exists())
            # A known positive-units backstop omits the same GI on both sides. Only an exact
            # structured current abandonment witness can distinguish it from missing output.
            (run / "wins" / f"{key}.wins").write_bytes(new)
            job["games"] = 3
            (run / "manifest.json").write_text(json.dumps({"jobs":[job]}))
            witness = f"[goldfish] ABANDONED job={key} gi=52 units=9 limit=9 cause=UNITS elapsed=0.0s\n"
            (run / "batch.err").write_text(fd_line + "\n" + nonconv_line + "\n" + witness)
            invoke(w / "known-backstop")
            bounded = json.loads((w / "known-backstop/audit.json").read_text())
            self.assertEqual(bounded["cases"][0]["baseline_equal_abandoned_gis"], [52])
            self.assertEqual((bounded["games"], bounded["requested_games"]), (2, 3))
            for altered in ["", witness.replace("limit=9", "limit=8")]:
                (run / "batch.err").write_text(altered)
                with self.assertRaisesRegex(ValueError, "abandonment witness"):
                    invoke(w / "unwitnessed-backstop")
            self.assertFalse((w / "unwitnessed-backstop").exists())


if __name__ == "__main__":
    unittest.main()
