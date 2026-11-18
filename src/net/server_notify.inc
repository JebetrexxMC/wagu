// 5.00.07 (included by server.cpp): the notification system (GM_NOTIFY).
// The default server (Broker MQQ) always has it; a custom server when its host turned on
// "Notification system" (Add server > Gamemodes, Server management, mp_server --gamemodes notify).
// Only staff can send: the default server's [ADMIN] + [MOD] (they run Server management there), a custom
// server's host owner + its [ADMIN] / [MOD]. The phone types
//   /notify embed:<title> logo:<type> message:<text> timer:<seconds> start:<h:mm AM> end:<h:mm PM>
// and the server checks it, numbers it and sends it to everybody (MP 5.00.07+). Notifications that are
// still on (or start later) are kept, so a player who joins in between sees them too.

bool MPServer::NotifyOn() const
{
    if(IsBcwServer() || IsMw3Server() || IsRpServer() || IsDayzServer() || IsLgServer()) return false;
    return IsDefaultServer() || (GetModes() & GM_NOTIFY) != 0;
}
bool MPServer::NotifyStaff(int slot) const
{
    const Slot& s = m_Slots[slot];
    if(IsDefaultServer()) return s.admin || s.mod;
    return s.admin || s.mod || s.owner;
}
static uint32_t NotifyEnds(const PktNotify& n) { return n.end ? n.end : n.start + n.timer; }

void MPServer::NotifyHandle(int slot, const uint8_t* data, int len)
{
    if(slot < 0 || slot >= MP_MAX_PLAYERS || len < (int)sizeof(PktNotify) || !m_Slots[slot].used) return;
    if(AuthOn() && !m_Slots[slot].authed) return;
    PktNotify n; memcpy(&n, data, sizeof(n));
    n.title[sizeof(n.title) - 1] = 0; n.text[sizeof(n.text) - 1] = 0; n.sender[sizeof(n.sender) - 1] = 0;
    if(n.op != NOTIFY_SEND && n.op != NOTIFY_CLEAR) return;
    const bool on = NotifyOn();
    if(!on || !NotifyStaff(slot))
    {
        PktNotify r; memset(&r, 0, sizeof(r));
        r.type = PKT_NOTIFY; r.op = NOTIFY_DENIED; r.logo = on ? 1 : 0;
        SendTo(slot, &r, sizeof(r));
        return;
    }
    const uint64_t nowMs = MP_NowMs();
    if(nowMs - m_NotifyAt[slot] < 1500) return;   // flood guard
    m_NotifyAt[slot] = nowMs;
    const uint32_t now = (uint32_t)time(nullptr);
    if(n.op == NOTIFY_CLEAR)
    {
        m_Notify.clear();
        PktNotify c; memset(&c, 0, sizeof(c)); c.type = PKT_NOTIFY; c.op = NOTIFY_CLEAR; c.srvNow = now; c.from = (uint8_t)slot;
        for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used && m_Slots[i].build >= MP_NOTIFY_BUILD) SendTo(i, &c, sizeof(c));
        Log("%s cleared the notifications", m_Slots[slot].name);
        return;
    }
    if(!n.text[0] && !n.title[0]) return;
    if(n.timer < 3) n.timer = 10;
    if(n.timer > 600) n.timer = 600;
    if(n.logo >= NLOGO_COUNT) n.logo = NLOGO_INFO;
    // the phone sent its own clock: move it onto the server's clock
    const int64_t skew = n.srvNow ? (int64_t)now - (int64_t)n.srvNow : 0;
    int64_t st = n.start ? (int64_t)n.start + skew : (int64_t)now;
    int64_t en = n.end ? (int64_t)n.end + skew : 0;
    if(st < (int64_t)now) st = now;
    if(st > (int64_t)now + 7 * 86400) st = (int64_t)now + 7 * 86400;
    if(en && en <= st) en = 0;
    if(en > st + 86400) en = st + 86400;   // at most a day on screen
    n.start = (uint32_t)st; n.end = (uint32_t)en;
    n.type = PKT_NOTIFY; n.op = NOTIFY_SHOW; n.from = (uint8_t)slot; n.id = ++m_nNotifySeq; n.srvNow = now; n.pad = 0;
    MP_CopyStr(n.sender, m_Slots[slot].name, sizeof(n.sender));
    // keep it for players who join later (drop the ones that are over, at most MP_NOTIFY_KEEP)
    for(size_t i = 0; i < m_Notify.size();) { if(NotifyEnds(m_Notify[i]) <= now) m_Notify.erase(m_Notify.begin() + i); else ++i; }
    if(m_Notify.size() >= MP_NOTIFY_KEEP) m_Notify.erase(m_Notify.begin());
    m_Notify.push_back(n);
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used && m_Slots[i].build >= MP_NOTIFY_BUILD) SendTo(i, &n, sizeof(n));
    Log("%s sent a notification: [%s] %s (start +%ds, %s)", m_Slots[slot].name, n.title, n.text, (int)(n.start - now),
        n.end ? "until the end time" : "timer");
}

void MPServer::NotifyOnJoin(int slot)
{
    if(slot < 0 || slot >= MP_MAX_PLAYERS) return;
    m_NotifyAt[slot] = 0;
    if(m_Slots[slot].build < MP_NOTIFY_BUILD || m_Notify.empty() || !NotifyOn()) return;
    const uint32_t now = (uint32_t)time(nullptr);
    for(const PktNotify& k : m_Notify)
    {
        if(NotifyEnds(k) <= now) continue;
        PktNotify n = k; n.srvNow = now;
        SendTo(slot, &n, sizeof(n));
    }
}
