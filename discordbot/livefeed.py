"""5.02.80: MP live feed -> one Discord channel, posted through a webhook the bot makes itself.

The mod (src/mp_dcfeed.inc) publishes each player's OWN events on every server to the public MQTT brokers
(topic wagu/feed1/e): joined / left the server, started / left a mission, and their chat (never commands).
This bot reads them and posts them in FEED_CHANNEL_ID (default 1576192312126472323) through a webhook it creates in
that channel ("MP Live Feed"). Each message carries the player's Discord username as the webhook name and no avatar,
so in Discord it looks like a plain webhook account: no bio, no profile picture, nothing to open.

Name: the Discord username saved with the player's game account (accounts.db) when the event names one, else the
Discord name the phone sent, else the in-game name. Mentions are never pinged (@everyone, @here, users, roles).

Variables (optional):
  FEED_CHANNEL_ID   the channel (default 1576192312126472323), "off" = feed off
  FEED_WEBHOOK_NAME the webhook the bot creates there (default "MP Live Feed")
The bot needs Manage Webhooks in that channel (the invite link in the log has it).
"""
import asyncio
import collections
import json
import logging
import os
import re
import time

import discord

log = logging.getLogger("mpbot.feed")

TOPIC = "wagu/feed1/e"
CHANNEL_ID = os.environ.get("FEED_CHANNEL_ID", "1576192312126472323").strip()
WEBHOOK_NAME = os.environ.get("FEED_WEBHOOK_NAME", "MP Live Feed").strip() or "MP Live Feed"
TYPES = {"join", "leave", "chat", "mission"}
NO_PING = discord.AllowedMentions.none()


def _clean(v, n):
    v = str(v or "")
    v = re.sub(r"[\x00-\x1f\x7f]", " ", v)
    return v.strip()[:n]


def _safe_name(v):
    # webhook names: 1-80 characters, and Discord refuses "discord" / "clyde" in them
    v = _clean(v, 80)
    v = re.sub(r"(?i)discord", "disc0rd", v)
    v = re.sub(r"(?i)clyde", "clyd3", v)
    return v or "MP player"


class LiveFeed:
    def __init__(self, client: discord.Client, core):
        self.client = client
        self.core = core
        self.on = CHANNEL_ID.isdigit()
        self.queue = None
        self.loop = None
        self.seen = collections.OrderedDict()   # event ids (the same event arrives from up to 3 brokers)
        self.rate = {}                          # player -> recent post times
        self.hook = None
        self.task = None
        self.stats = {"posted": 0, "dropped": 0, "errors": 0}

    # ---- MQTT thread side
    def feed(self, payload: bytes):
        if not self.on or not self.loop or not self.queue:
            return
        try:
            self.loop.call_soon_threadsafe(self.queue.put_nowait, payload)
        except Exception:
            pass   # queue full / loop closing

    # ---- asyncio side
    def start(self):
        if not self.on:
            log.info("live feed off (FEED_CHANNEL_ID=%s)", CHANNEL_ID or "-")
            return
        if self.task and not self.task.done():
            return
        self.loop = asyncio.get_running_loop()
        self.queue = asyncio.Queue(maxsize=500)
        self.task = asyncio.create_task(self._run())
        log.info("live feed: MP events -> channel %s (webhook %r)", CHANNEL_ID, WEBHOOK_NAME)

    async def _webhook(self):
        if self.hook:
            return self.hook
        ch = self.client.get_channel(int(CHANNEL_ID)) or await self.client.fetch_channel(int(CHANNEL_ID))
        try:
            for h in await ch.webhooks():
                if h.name == WEBHOOK_NAME and h.user and self.client.user and h.user.id == self.client.user.id:
                    self.hook = h
                    break
            if not self.hook:
                self.hook = await ch.create_webhook(name=WEBHOOK_NAME, reason="MP live feed (game chat, joins, missions)")
                log.info("live feed: created webhook %s in #%s", self.hook.id, getattr(ch, "name", CHANNEL_ID))
        except discord.Forbidden:
            log.warning("live feed: the bot needs Manage Webhooks in channel %s (re-invite with the link in the log)", CHANNEL_ID)
            raise
        return self.hook

    def _dup(self, eid):
        if not eid:
            return False
        if eid in self.seen:
            return True
        self.seen[eid] = time.time()
        while len(self.seen) > 4000:
            self.seen.popitem(last=False)
        return False

    def _limited(self, who):
        now = time.time()
        q = [t for t in self.rate.get(who, []) if now - t < 60]
        if (q and now - q[-1] < 1.0) or len(q) >= 20:   # 1 a second, 20 a minute per player
            self.rate[who] = q
            return True
        q.append(now)
        self.rate[who] = q
        if len(self.rate) > 5000:
            self.rate.clear()
        return False

    def _name_of(self, ev):
        user = _clean(ev.get("user"), 32)
        if user:
            try:
                with self.core.lock:
                    a = self.core._account("user_lc", user.lower())
                if a and a.get("discord_name"):
                    return a["discord_name"]
            except Exception:
                pass
        return _clean(ev.get("disc"), 40) or _clean(ev.get("name"), 32)

    def _text(self, ev):
        t, msg, srv = ev["t"], _clean(ev.get("msg"), 300), _clean(ev.get("srv"), 40) or "an MP server"
        game = _clean(ev.get("name"), 32)
        tag = f" ({game})" if game else ""
        if t == "chat":
            return f"**[{srv}]** {discord.utils.escape_mentions(msg)}" if msg else ""
        if t == "join":
            return f"joined **{srv}**{tag}"
        if t == "leave":
            return f"{msg or 'left the server'} - **{srv}**{tag}"
        if t == "mission":
            return f"{msg or 'is on a mission'} - **{srv}**{tag}"
        return ""

    async def _run(self):
        while True:
            payload = await self.queue.get()
            try:
                ev = json.loads(payload.decode("utf-8", "replace"))
                if not isinstance(ev, dict) or ev.get("t") not in TYPES or self._dup(_clean(ev.get("id"), 24)):
                    continue
                who = self._name_of(ev)
                text = self._text(ev)
                if not text or self._limited(who.lower()):
                    self.stats["dropped"] += 1
                    continue
                hook = await self._webhook()
                # no avatar_url: Discord shows the plain default picture, the name opens no profile
                await hook.send(content=text[:1900], username=_safe_name(who), allowed_mentions=NO_PING, wait=False)
                self.stats["posted"] += 1
            except discord.NotFound:
                self.hook = None   # somebody deleted the webhook: make it again next time
            except discord.Forbidden:
                self.stats["errors"] += 1
                await asyncio.sleep(60)
            except discord.HTTPException as e:
                self.stats["errors"] += 1
                await asyncio.sleep(getattr(e, "retry_after", 2) or 2)
            except Exception:
                self.stats["errors"] += 1
                log.exception("live feed event failed")
