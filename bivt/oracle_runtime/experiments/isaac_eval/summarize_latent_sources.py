#!/usr/bin/env python3
"""Summarize eval_latent_sources.py JSON: fall rate / distance per terrain family and source."""
import json
import sys

SOURCES = ('teacher', 'student', 'flatscan', 'zero')

for path in sys.argv[1:]:
    d = json.load(open(path))
    S, M = d['stats'], d['latent_mse']
    fams = list(M)
    print(f"\n=== {path}  difficulty={d['args']['difficulty']} steps={d['step']} envs={d['args']['num_envs']}")
    print(f"{'family':24s} " + ' '.join(f'{s:>18s}' for s in SOURCES) + '   stud-teach latent MSE')
    print(f"{'':24s} " + ' '.join(f'{"fall% (n) dist":>18s}' for _ in SOURCES))
    tot = {s: [0, 0, 0.0] for s in SOURCES}
    for f in fams:
        cells = []
        for s in SOURCES:
            r = S[s][f]
            n = r['episodes']
            tot[s][0] += n
            tot[s][1] += r['base_contact']
            tot[s][2] += r['dist']
            cells.append(f"{100 * r['base_contact'] / n:5.1f}% ({n:3d}) {r['dist'] / n:4.1f}m" if n else f'{"-":>18s}')
        m = M[f]
        print(f'{f:24s} ' + ' '.join(f'{c:>18s}' for c in cells) + (f'   {m[0] / m[1]:.4f}' if m[1] else ''))
    print(f"{'ALL':24s} " + ' '.join(
        f"{100 * t[1] / t[0]:5.1f}% ({t[0]:3d}) {t[2] / t[0]:4.1f}m" if t[0] else f'{"-":>18s}' for t in tot.values()))
