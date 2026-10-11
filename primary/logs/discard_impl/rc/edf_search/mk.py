import json, os
D = "/workspaces/MagicDeckTester/logs/discard_impl/rc/edf_search"
base = {"deck": "decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod",
        "profile": "decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json",
        "games": 1, "seed": 217164}
cells = {
    "g63": dict(game_index=63, depth=3, budget_ms=10, ignore_play_profile=True),
    "g163": dict(game_index=163, budget_ms=20),
}
for c, extra in cells.items():
    for arm, val in (("off", False), ("on", True)):
        j = dict(base); j.update(extra); j["flags"] = {"MTG_EDF_BUCKET_DISCARD": val}; j["name"] = f"{c}_{arm}"
        with open(os.path.join(D, f"{c}_{arm}.json"), "w") as f:
            json.dump({"jobs": [j]}, f, indent=1)
