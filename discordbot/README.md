---
title: MP login bot
description: Discord verification + account bot for the GTA SA MP mod (wagu)
tags:
  - python
  - discord.py
---

# MP login bot

The account system of the MP mod (3.21.6+, bot 3.21.7): the default server, Brief Case War and every custom
server with "Authentication system" on.

1. A player connects: the game shows a random code (8 characters, new every time).
2. In the Discord server ([discord.gg/tbsDknk34y](https://discord.gg/tbsDknk34y)) the player types
   `/gen CODE` in **#verification** (the command works only there; the answer is only visible to them).
3. The game shows Sign up: user, password, gender, age -> **Create**. Next time: Login only.
4. `/stats` (Discord, only visible to you, card with Refresh / Switch view buttons - Switch view = the character look; was `/stats` before 4.13.15) or `/stats` / `/char` in the game: your account.
   `/help` (4.13.15, replaces `/chardc`): the tutorial - install, account, playing, commands, problems (pages, only visible to you).
5. Forgot your account (3.56): `/forgotuser` shows your game username, `/resetpass` opens a form for a new password (all phones get logged out). In the game: Login screen > Forgot username? / Forgot password?, then `/gen CODE` in #verification.
   5.00.05: `/forgotpass` (form: new password; your phones stay logged in, they get a new saved login in `MP/auth_service.txt`), `/resetpass` = the same. `/forgotuser` now also lists the phones logged in to each account and writes `MP/accounts.txt` + `MP/auth_service.txt` again on them (made new when they were deleted; offline phones on the next game / Moquil / Textdraw Editor start). Only phones that really logged in (or showed a `/gen` code) are registered, by their `auth_device` id.
   `/delete` (5.00.05): a form (username + type DELETE) removes one of your game accounts, its saved logins and phones; the username is free again and `/create` can make a new one on the same Discord (max 3 per Discord).
   `/create user: pass: gender: age:` (5.00.05): make the game account right in Discord. A phone that showed a `/gen` code in the last 24 h is logged in to it by itself; otherwise Login once in the game.
6. 24/7 stream (4.13.16): `/streaming on` (server managers, while you are in a voice channel, or pick `channel:`) - the bot joins and plays the MP YouTube live (https://www.youtube.com/live/rFZHOHl-L8A) there 24/7, also after restarts. `/streaming status`, `/streaming off`. Discord blocks video / Go Live for bot accounts, so the bot plays the sound and posts a **Watch video (in voice)** button in the voice channel's chat: it opens Discord's Watch Together activity right in the voice channel, everybody who taps it watches the live together (`/streaming watch` posts the button again). Re-invite the bot with the log's invite link so it has Create Invite. The server needs ffmpeg (MP Railway installs it). If YouTube asks the server to sign in, put a cookies.txt at `DATA_DIR/yt_cookies.txt` (or set `YT_COOKIES`). Re-invite the bot with the new link from the log if it lacks Connect / Speak.
7. Account check (5.01.03): `/check` - only members with the role `1562343934590713937` (`CHECK_ROLE_ID`) can use it, everybody else gets "no permission". Lists every MP game account with the Discord username it belongs to (current name + the saved one, mention, created, last login, logins, phones, last server), 10 per page with Back / Next, and **Download CSV** for the whole list. `member:` = only that Discord's accounts, `search:` = part of an MP username, a Discord name or a Discord id. Only the one who asked sees the answer; no passwords or phone ids are shown.

Accounts are stored only here (SQLite, password hashed with PBKDF2-SHA256 + salt). The game and the
bot talk end-to-end encrypted (ECDH P-256 + AES-256-GCM) over public MQTT brokers; the bot signs a
one-time login ticket (ECDSA P-256) that the MP server checks. The host never sees a password.

## Runs on MP Railway (24/7, our own Railway)

Railway.app no longer takes new accounts, so the bot runs on **MP Railway**: a small self-hosted
Railway that lives on our HiveMQ server (or on a Termux phone). It clones this repo, installs
`requirements.txt`, starts `Procfile` (`worker: python main.py`), restarts on crash and on reboot,
and **deploys every new commit on `main` by itself within 2 minutes**.

**Automatic (no command on the server):** add the repository secrets `DISCORD_TOKEN` and
`SSH_PASSWORD` (or `SSH_KEY`) in Settings > Secrets and variables > Actions (`AUTH_PRIVATE_KEY` is
already there). The **Deploy to MP Railway** workflow then logs in to the server
(`mpsamp.duckdns.org`, user `root`; change with the `SSH_HOST` / `SSH_USER` variables) and installs /
updates the bot on every push. Run it any time: Actions > Deploy to MP Railway > Run workflow.

Or by hand on the HiveMQ server (once):

```bash
curl -fsSL https://raw.githubusercontent.com/ThirtyFoxMCx00E1/HiveMQ/main/railway/install.sh | sudo bash
```

It asks for `DISCORD_TOKEN` and `AUTH_PRIVATE_KEY` (hidden input, stored in
`/etc/mp-railway/discordpy-bot.env`, mode 600, never in git), deploys and prints
`ONLINE - /gen, /stats, /help are in your Discord server`. Termius console: **r) MP Railway**.

```
sudo mp-railway status        online? commit? which servers have the commands
sudo mp-railway logs -f       live log
sudo mp-railway deploy        deploy now (instead of waiting for the 2 min check)
sudo mp-railway env set DISCORD_TOKEN     change a secret (asks, hidden), restarts
```

Live status: [MP Railway page](https://thirtyfoxmcx00e1.github.io/wagu/railway.html) (the bot
publishes a signed status every 30 s; the page checks the signature with the mod's public key).

## Variables

Never put them in the code:

| Variable | Value |
|---|---|
| `DISCORD_TOKEN` | the bot token (Discord Developer Portal > Bot > Reset Token) |
| `AUTH_PRIVATE_KEY` | the 64-hex private key that matches the public key built into the mod |
| `VERIFY_CHANNEL` | optional, default `verification` |
| `STREAM_URL` | optional (4.12.2); YouTube / Twitch link for the 24/7 **Streaming** status, `off` = no status. `STATUS_TEXTS` (titles split by `|`) and `STATUS_SECONDS` (default 30) |
| `DATA_DIR` | optional; MP Railway sets `/var/lib/mp-railway/discordpy-bot`, Docker uses `/data` (volume) |
| `DATA_REPO_TOKEN` | optional (5.00.05): GitHub token with contents write on `DATA_REPO` (default this repo). The bot restores `accounts.db` from the encrypted copy on branch `mp-railway-data` before it starts and saves it there after every account change, so a redeploy / new container never resets the accounts. MP Railway Cloud does this itself (no token needed) |
| `CREATE_CHANNEL_ID` | optional (5.00.05), default `1563160216286003272`: when somebody asks "how do I create an account?" in any channel, the bot answers in public (one of 14 messages) pointing to this channel. Turn **Message Content Intent** on (Developer Portal > Bot > Privileged Gateway Intents); `AUTO_REPLY=0` turns it off |
| `CHECK_ROLE_ID` | optional (5.01.03), default `1562343934590713937`: the role allowed to use `/check` |
| `GLOBAL_SYNC` | optional, `1` = also register the commands globally (they then show twice in servers the bot is in) |

Discord: invite the bot with the `bot` and `applications.commands` scopes, create a text channel
named `verification`, and let the bot read/send there. Slash commands are synced to every server the
bot is in when it starts, and to new servers when it joins.

## Other hosts

Docker / Coolify / Dokploy / CapRover / Nixpacks work too: see [README-deploy.md](README-deploy.md)
(`Dockerfile`, `docker-compose.yml`, `captain-definition`, `nixpacks.toml`).
