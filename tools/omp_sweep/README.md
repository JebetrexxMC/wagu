# Weekly open.mp crossplay sweep (5.02.05)

`sh tools/omp_sweep/sweep.sh [out.tsv]` reads https://api.open.mp/servers, takes the 12 busiest servers and the
busiest one with a password, and joins each with the crossplay gate test (`/tmp/sampgate_test`, built by the
script): a pretend phone joins MP's private server, the gate joins the PC server, then chat + /command + movement.
Then it starts a local omp-server 1.6.7 with a password and tries the right / a wrong / an empty password.
Output: `tools/omp_sweep/results/<date>.tsv` (`date rank address players password version result status last_message name`).

| result | meaning |
|---|---|
| stayed | joined and still connected after chat + /command + movement |
| kicked | joined, then the server closed the connection (own rules / anti-bot / accounts) |
| launcher | never sends the game start (InitGame) - servers that only take their own launcher |
| no_answer | the server did not answer at all |
| wrong_pw | "Wrong server password." |

`python3 tools/omp_sweep/compare.py old.tsv new.tsv` prints the changes (servers that entered / left the top 12,
results that changed, counts before / now, password results) as changelog-ready lines. A weekly automation runs
both every Monday 09:00 (Asia/Manila).

Baseline 2026-11-20: stayed 4 (B-Zone RPG, ArSaCiA, [BPS], Vice Side RP), kicked 2 (blue.bugged.ro "You need to log in
first!", NEWTHINGS 3.0), launcher 5 (GRAND MOBILE, Rio Rise #87 / #90, GTA NewGamers, Arcane OGC), no_answer 1 (FZ
Roleplay); Aspirasi Roleplay (password) wrong password -> "Wrong server password."; local omp-server: right password
stays, wrong / empty -> wrong_pw.
