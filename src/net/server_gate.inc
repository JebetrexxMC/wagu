// 5.02: open.mp / SA-MP 0.3.7 crossplay built in - the MPServer side (included by server.cpp).
// net/sampgate.cpp drives the phones on this server through these helpers. Each one locks m_Mutex;
// slot -1 = every real (phone) player. They never touch the SA-MP gamemode runtime (g_Samp).

void MPServer::SetGate(SampGate* g)
{
    std::lock_guard<std::mutex> lk(m_Mutex);
    m_pGate = g;
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
    std::string s = text ? text : "";
    PktServerMsg m; memset(&m, 0, sizeof(m)); m.type = PKT_SERVER_MSG; m.color = color | 0xFF;
    std::lock_guard<std::mutex> lk(m_Mutex);
    size_t at = 0;
    do { MP_CopyStr(m.text, s.c_str() + at, sizeof(m.text)); GATE_EACH(slot, SendTo(to, &m, (int)sizeof(m))); at += sizeof(m.text) - 1; } while(at < s.size());
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

int MPServer::GateWorldHour() { return (int)((WorldMinute() / 60) % 24); }

#undef GATE_EACH
