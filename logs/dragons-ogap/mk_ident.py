#!/usr/bin/env python3
# mk_ident.py: play-identity manifest (oracle OFF): Dragons std + 2HG, plus the two other decks whose cards reach the
# graveyard-return apply sites (Kitty Equipment: Cid, Freeflier Pilot; Landfall: Haunted Fengraf). 250-game chunks.
#   usage: python3 mk_ident.py <out.json>
import json, sys
decks = [('dragons', 'decks/Dragons/Dragons.cod', 'decks/Dragons/Dragons.profile.json', 20000),
         ('kitty', 'decks/Kitty Equipment/Kitty Equipment.cod', 'decks/Kitty Equipment/Kitty Equipment.profile.json', 5000),
         ('landfall', 'decks/Landfall/Landfall.cod', 'decks/Landfall/Landfall.profile.json', 5000)]
jobs = []
for tag, deck, prof, n in decks:
    for fmt, extra, nn in (('std', {}, n), ('2hg', {'starting_life': 30, 'opponent_heads': 2}, n // 2)):
        for g in range(0, nn, 250):
            gi = 700000 + g
            j = {'name': '%s_%s__g%d' % (tag, fmt, gi), 'deck': deck, 'profile': prof, 'games': 250,
                 'seed': 30000000 + gi, 'game_index': gi}
            j.update(extra)
            jobs.append(j)
json.dump({'jobs': jobs}, open(sys.argv[1], 'w'), indent=0)
print(len(jobs), 'jobs', sum(j['games'] for j in jobs), 'games')
