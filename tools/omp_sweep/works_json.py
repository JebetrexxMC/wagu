#!/usr/bin/env python3
# 5.02.57: results/spawn-<date>.tsv -> docs/works.json (the "Works with MP" list on mpsamp-open.github.io/works.html)
# Usage: works_json.py results/spawn-2026-11-22.tsv
import csv, json, re, sys, os, collections
p = sys.argv[1]
date = re.search(r"(\d{4}-\d\d-\d\d)", p).group(1)
rows = list(csv.DictReader(open(p, encoding="utf-8", errors="replace"), delimiter="\t"))
clean = lambda s: re.sub(r"\{[0-9a-fA-F]{6}\}", "", s or "").strip()
seen, ok = set(), []
for r in rows:
    if r["result"] != "stayed" or r["addr"] in seen: continue
    seen.add(r["addr"])
    ok.append({"a": r["addr"], "n": clean(r["name"])[:60] or r["addr"], "p": int(r["players"] or 0), "v": r["version"],
               "d": int(r["dialogs"] or 0) > 0})
ok.sort(key=lambda s: -s["p"])
cnt = collections.Counter(r["result"] for r in rows)
out = {"date": date, "checked": len(rows), "counts": dict(cnt), "servers": ok,
       "how": "each open server on api.open.mp joined once as MP for Android (sampgate, remote role): InitGame, class selection, stayed connected"}
dst = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "docs", "works.json")
json.dump(out, open(dst, "w", encoding="utf-8"), ensure_ascii=False, separators=(",", ":"))
print(dst, len(ok), dict(cnt))
