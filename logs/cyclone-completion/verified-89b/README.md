# Cyclone89b validation evidence: cost gate failed

Source `89b146d10406063213d9abe41e1cfdf1a616c021`; PGO `20de8d519c6c7c8494925d5170ff91ed06f5b4fcde4fd1bc04d1325113bb5e80`.

The quiet 855-game cost gate failed for all three measured arms: live 13.6866x, PB 3.5175x and PB+KEEP+MYRIAD 3.5503x versus same-run Hinata 2040.213 ms/game (bar 3x). This archive permits no suite admission, artifact generation or policy adoption.

The native controller's original exit 2/INCOMPLETE is preserved alongside a separate read-only parser recovery closure validating 15 actual native Claude games; it is not relabeled as controller 0.

`evidence.tar.gz` and `index.json` preserve the closed gates, raw engine inputs/outcomes/diagnostic traces, initial versus accepted normal results, source-focused tests and reviewer limits. Paired baseline and historical C2 evidence has separate labels; it is never aliased as 89b measurement. Native conversation transcripts, credentials and compiled binaries are excluded.

The normal initial harness exits remain 1; later reviewed acceptance is separately 0. Existing FD observations are classified by exact tuples and outcomes, not described as repaired. The final per-stage receipts govern the scope of memory, native-Claude, VERIFY and cost claims.
