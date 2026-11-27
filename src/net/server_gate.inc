// 5.02: open.mp / SA-MP 0.3.7 crossplay built in - the MPServer side (included by server.cpp).
// net/sampgate.cpp drives the phones on this server through these helpers. Each one locks m_Mutex;
// slot -1 = every real (phone) player. They never touch the SA-MP gamemode runtime (g_Samp).

void MPServer::SetGate(SampGate* g)
{
    std::lock_guard<std::mutex> lk(m_Mutex);
    m_pGate = g;
}

// 5.02.79: CoopFusion32 crossplay (net/coopgate.cpp)
void MPServer::SetPhoneHook(PhoneHook* h)
{
    std::lock_guard<std::mutex> lk(m_Mutex);
    m_pPhoneHook = h;
}
void MPServer::GateDamage(int fromSlot, int target, uint8_t weapon, float amount)
{   // a co-op player hurt the phone: the same packet a phone gets when another phone hits it (the kill is credited)
    std::lock_guard<std::mutex> lk(m_Mutex);
    if(target < 0 || target >= MP_MAX_PLAYERS || !m_Slots[target].used || m_Slots[target].ghost || !(amount > 0)) return;
    PktDamage p; memset(&p, 0, sizeof(p)); p.type = PKT_DAMAGE;
    p.from = (uint8_t)((fromSlot >= 0 && fromSlot < MP_MAX_PLAYERS && m_Slots[fromSlot].used) ? fromSlot : MP_INVALID_ID);
    p.target = (uint8_t)target; p.weapon = weapon; p.amount = amount;
    SendTo(target, &p, sizeof(p));
}
void MPServer::GateDeath(int victim, int killer, uint8_t weapon)
{   // a co-op player died: the kill feed on the phone
    std::lock_guard<std::mutex> lk(m_Mutex);
    if(victim < 0 || victim >= MP_MAX_PLAYERS || !m_Slots[victim].used || !m_Slots[victim].ghost) return;
    PktDeath p; p.type = PKT_DEATH; p.victim = (uint8_t)victim; p.weapon = weapon;
    p.killer = (uint8_t)((killer >= 0 && killer < MP_MAX_PLAYERS && m_Slots[killer].used) ? killer : MP_INVALID_ID);
    Broadcast(&p, sizeof(p));
}

#define GATE_EACH(slot, body) \
    for(int i_ = 0; i_ < MP_MAX_PLAYERS; ++i_) { if(!m_Slots[i_].used || m_Slots[i_].ghost || (slot >= 0 && i_ != slot)) continue; const int to = i_; (void)to; body; }

void MPServer::GateAct(int slot, uint8_t act, int a, int b, int32_t val, const float* pos, float h, const char* text)
{
    std::lock_guard<std::mutex> lk(m_Mutex);
    GATE_EACH(slot, CgAct(to, act, a, b, val, pos, h, text));
}

void MPServer::GateOp(int slot, uint8_t op, uint8_t a, uint8_t b, int32_t val, const float* f, const char* text)
{
    PktSamp p; memset(&p, 0, sizeof(p)); p.type = PKT_SAMP; p.op = op; p.a = a; p.b = b; p.val = val;
    if(f) memcpy(p.f, f, sizeof(p.f));
    if(text) MP_CopyStr(p.text, text, sizeof(p.text));
    std::lock_guard<std::mutex> lk(m_Mutex);
    GATE_EACH(slot, SendTo(to, &p, (int)sizeof(p)));
}

void MPServer::GateLabel(int slot, int id, bool add, const float* pos, float dist, uint32_t color, const char* text)
{   // same layout as SampLabelSend: the colour sits in the last 4 bytes of text
    PktSamp p; memset(&p, 0, sizeof(p)); p.type = PKT_SAMP; p.op = SPO_LABEL; p.a = add ? 1 : 0; p.val = id;
    if(pos) { p.f[0] = pos[0]; p.f[1] = pos[1]; p.f[2] = pos[2]; } p.f[3] = dist;
    MP_CopyStr(p.text, text ? text : "", sizeof(p.text) - 6);
    memcpy(&p.text[sizeof(p.text) - 5], &color, 4);
    std::lock_guard<std::mutex> lk(m_Mutex);
    GATE_EACH(slot, SendTo(to, &p, (int)sizeof(p)));
}

void MPServer::GateMsg(int slot, uint32_t color, const char* text)
{   // SA-MP lines are 144 chars, MP chat lines 128: longer ones go in two lines
    // 5.02.21: the cut is at a space (or a whole UTF-8 letter), never inside a {RRGGBB} code, and the second line
    // keeps the colour the first one ended with. Before, a 140-letter line was cut at byte 127: half a letter or half a
    // colour code, and the rest of the line came out in the wrong colour / as garbage.
    std::string s = text ? text : "";
    PktServerMsg m; memset(&m, 0, sizeof(m)); m.type = PKT_SERVER_MSG; m.color = color | 0xFF;
    const size_t room = sizeof(m.text) - 1;
    auto isCode = [&](size_t i) { if(i + 7 >= s.size() || s[i] != '{' || s[i + 7] != '}') return false; for(int k = 1; k <= 6; ++k) if(!isxdigit((unsigned char)s[i + k])) return false; return true; };
    std::vector<std::string> parts;
    std::string carry;   // the colour code in effect where the next part starts
    size_t at = 0;
    while(at < s.size())
    {
        const size_t lim = room > carry.size() ? room - carry.size() : room;
        size_t n = std::min(lim, s.size() - at);
        if(at + n < s.size())
        {
            size_t cut = n;
            for(size_t k = n; k > n / 2; --k) if(s[at + k] == ' ') { cut = k; break; }   // a space in the second half
            while(cut > 1 && ((unsigned char)s[at + cut] & 0xC0) == 0x80) --cut;          // not inside a UTF-8 letter
            for(size_t k = (cut > 7 ? cut - 7 : 0); k < cut; ++k) if(isCode(at + k)) { cut = k ? k : cut; break; }   // not inside {RRGGBB}
            n = cut;
        }
        parts.push_back(carry + s.substr(at, n));
        for(size_t k = at; k < at + n; ++k) if(isCode(k)) carry = s.substr(k, 8);
        at += n; while(at < s.size() && s[at] == ' ') ++at;
    }
    if(parts.empty()) parts.push_back("");
    std::lock_guard<std::mutex> lk(m_Mutex);
    for(const std::string& p : parts) { MP_CopyStr(m.text, p.c_str(), sizeof(m.text)); GATE_EACH(slot, SendTo(to, &m, (int)sizeof(m))); }
}

void MPServer::GateChat(int fromSlot, const char* text)
{
    if(fromSlot < 0 || fromSlot >= MP_MAX_PLAYERS || !text) return;
    PktChat p; memset(&p, 0, sizeof(p)); p.type = PKT_CHAT; p.id = (uint8_t)fromSlot;
    MP_CopyStr(p.text, text, sizeof(p.text));
    std::lock_guard<std::mutex> lk(m_Mutex);
    if(!m_Slots[fromSlot].used) return;
    GATE_EACH(-1, SendTo(to, &p, (int)sizeof(p)));
}

void MPServer::GateDialog(int slot, int id, int style, const char* cap, const char* info, const char* b1, const char* b2)
{
    std::lock_guard<std::mutex> lk(m_Mutex);
    GATE_EACH(slot, {
        if(id < 0) CgDlg(to, 0xFFFF, 0, "", "", "", "");
        else { AcDialogShown(to, (uint16_t)id); CgDlg(to, (uint16_t)id, (uint8_t)style, cap ? cap : "", info ? info : "", b1 ? b1 : "", b2 ? b2 : ""); }
    });
}

void MPServer::GateTextDraw(int slot, const PktTextDraw& t)
{
    std::lock_guard<std::mutex> lk(m_Mutex);
    GATE_EACH(slot, SendTo(to, &t, (int)sizeof(t)));
}

void MPServer::GateObject(uint32_t row, uint16_t model, const float* p, const float* r, bool add)
{   // the PC server's objects go in the dynamic map list (not saved: m_bDzMapDirty stays)
    DzMapRow d; memset(&d, 0, sizeof(d));
    d.id = row; d.model = model; d.interior = 255; d.flags = 0;
    d.x = p[0]; d.y = p[1]; d.z = p[2]; d.rx10 = DzMapAng10(r[0]); d.ry10 = DzMapAng10(r[1]); d.rz10 = DzMapAng10(r[2]);
    std::lock_guard<std::mutex> lk(m_Mutex);
    size_t at = m_DzMap.size();
    for(size_t i = 0; i < m_DzMap.size(); ++i) if(m_DzMap[i].r.id == row) { at = i; break; }
    if(add)
    {
        if(at == m_DzMap.size()) { DzMapObj o; o.r = d; o.owner = "open.mp"; m_DzMap.push_back(o); }
        else m_DzMap[at].r = d;
        DzMapSendOne(d, DZMO_SET);
    }
    else if(at < m_DzMap.size())
    {
        d = m_DzMap[at].r; m_DzMap.erase(m_DzMap.begin() + (long)at);
        DzMapSendOne(d, DZMO_DEL);
    }
}

bool MPServer::GatePos(int slot, float* pos, float* heading, int* health)
{
    std::lock_guard<std::mutex> lk(m_Mutex);
    if(slot < 0 || slot >= MP_MAX_PLAYERS || !m_Slots[slot].used || !m_Slots[slot].hasSync) return false;
    const SyncData& d = m_Slots[slot].lastSync;
    const bool car = (d.flags & SYNC_IN_CAR) != 0;
    memcpy(pos, car ? d.carPos : d.pos, sizeof(float) * 3);
    *heading = car ? d.carHeading : d.heading;
    *health = d.health;
    return true;
}

void MPServer::GateVoice(int fromSlot, int toSlot, uint16_t seq, uint8_t flags, const uint8_t* opus, int len)
{   // 5.02.29: a PC player's SampVoice voice (one 20 ms frame) as if his ghost slot talked
    if(!opus || len <= 0 || len > MP_VOICE_MAX_BYTES || fromSlot < 0 || fromSlot >= MP_MAX_PLAYERS) return;
    uint8_t out[sizeof(PktVoiceHdr) + MP_VOICE_MAX_BYTES];
    PktVoiceHdr h; h.type = PKT_VOICE; h.id = (uint8_t)fromSlot; h.seq = seq; h.flags = flags; h.pad = 0; h.len = (uint16_t)len;
    memcpy(out, &h, sizeof(h)); memcpy(out + sizeof(h), opus, (size_t)len);
    std::lock_guard<std::mutex> lk(m_Mutex);
    if(!m_Slots[fromSlot].used) return;
    GATE_EACH(toSlot, if(m_Slots[to].build >= MP_VOICE_BUILD) SendTo(to, out, (int)sizeof(h) + len));
}

int MPServer::GateWorldHour() { return (int)((WorldMinute() / 60) % 24); }

#undef GATE_EACH
