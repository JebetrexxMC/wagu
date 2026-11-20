#!/usr/bin/env python3
# 5.02.05: compare two sweep TSVs (tools/omp_sweep/sweep.sh) and print the changes as changelog-ready lines.
#   python3 tools/omp_sweep/compare.py old.tsv new.tsv
import csv, sys
def load(p):
    with open(p, newline='') as f:
        return [r for r in csv.DictReader(f, delimiter='\t')]
old, new = load(sys.argv[1]), load(sys.argv[2])
key = lambda r: r['address']
o = {key(r): r for r in old if r['rank'] not in ('local',)}
n = {key(r): r for r in new if r['rank'] not in ('local',)}
top_o = {k for k, r in o.items() if r['rank'] != 'pw'}; top_n = {k for k, r in n.items() if r['rank'] != 'pw'}
print('Top 12 now: ' + ', '.join(f"{r['name']} ({r['result']})" for r in new if r['rank'] not in ('local', 'pw')))
for k in sorted(top_n - top_o): print(f"NEW in top 12: {n[k]['name']} {k} -> {n[k]['result']}")
for k in sorted(top_o - top_n): print(f"LEFT top 12: {o[k]['name']} {k} (was {o[k]['result']})")
for k in sorted(top_n & top_o):
    if o[k]['result'] != n[k]['result']:
        print(f"CHANGED: {n[k]['name']} {k}: {o[k]['result']} -> {n[k]['result']} ({n[k]['status']} | {n[k]['last_message']})")
cnt = lambda rows: {c: sum(1 for r in rows if r['rank'] not in ('local', 'pw') and r['result'] == c) for c in ('stayed', 'kicked', 'launcher', 'no_answer', 'wrong_pw')}
print('Counts before: ' + ', '.join(f'{k} {v}' for k, v in cnt(old).items()))
print('Counts now:    ' + ', '.join(f'{k} {v}' for k, v in cnt(new).items()))
for r in new:
    if r['rank'] in ('local', 'pw'): print(f"Password: {r['name']} {r['address']}: {r['result']} {r['status']}")
