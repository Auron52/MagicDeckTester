#!/bin/bash
R=/workspaces/MagicDeckTester/logs/discard_impl/rc3
export ENV_EXTRA="MTG_DECISION_PROGRESS=1 MTG_TURN_CENSUS=$R/snow/stuck221/old/census.tsv"
$R/scripts/run1.sh snow ship 221 1540222 old $R/snow/stuck221/old
