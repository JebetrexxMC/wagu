// MP 4.13.59 - Phone calls on every server (default, featured and custom). Included by mp.cpp after mp_rp_phone.inc.
//
// Mega Vil Futuristic keeps its own phone (numbers, SMS, apps). Everywhere else this phone calls a player
// by their id (or name) from the player list:
//  * /phone opens it (Contacts = everybody online, a Call button each), /call <id or name>, /answer, /hangup (/h)
//  * the other phone rings: the phone pops up with Answer / Decline, a ring icon shakes on the screen
//  * a connected call carries your voice chat to the other phone wherever they are (the server routes it)
//  * call sync: while you ring out or talk, CJ holds the cellphone to his ear (the game's own phone task,
//    upper body: you can still walk). The anim goes out with the normal anim sync, so every player sees
//    it, and their MP puts the cellphone (model 330) in your avatar's hand (mp.cpp, phone anims).

static struct PhoneState
{
    bool open = false; int app = 0;            // 0 contacts, 1 call screen
    int  ringFrom = -1; bool ringOut = false; uint64_t ringAt = 0;
    int  callWith = -1; uint64_t callStart = 0;
    char name[MP_NAME_LEN] = "";
    uint64_t endedAt = 0; char endText[64] = "";
    bool task = false; int taskPed = 0; uint64_t taskAt = 0;
    uint64_t ringSndAt = 0;
} s_Ph;

static int  PhoneCallPartner() { return s_Ph.callWith; }
static bool PhoneServerOk() { return IsConnected() && !RpOnServer() && client.GetServerBuild() >= MP_CALL_BUILD; }
static bool PhoneBusy() { return s_Ph.callWith >= 0 || s_Ph.ringFrom >= 0; }
static void PhoneSend(uint8_t act, int other)
{
    PktCall c; c.type = PKT_CALL; c.action = act; c.other = (uint8_t)(other >= 0 && other < MP_MAX_PLAYERS ? other : MP_INVALID_ID);
    client.Send(&c, sizeof(c));
}
static const char* PhoneName(int id) { return id >= 0 && id < MP_MAX_PLAYERS && players[id].active ? players[id].name : "Player"; }
static void PhoneEnded(const char* why)
{
    if(PhoneBusy()) SysMsg(COL_SYSTEM, "[PHONE] %s", why);
    s_Ph.callWith = -1; s_Ph.ringFrom = -1; s_Ph.ringOut = false;
    MP_CopyStr(s_Ph.endText, why, sizeof(s_Ph.endText)); s_Ph.endedAt = MP_NowMs();
}
static void PhoneReset()
{
    s_Ph.open = false; s_Ph.app = 0; s_Ph.ringFrom = s_Ph.callWith = -1; s_Ph.ringOut = false; s_Ph.endedAt = 0;
}
static void PhoneOpen() { if(!PhoneServerOk()) return; s_Ph.open = true; s_Ph.app = PhoneBusy() ? 1 : 0; }
static void PhoneDial(int id)
{
    if(!PhoneServerOk()) { SysMsg(COL_RED, RpOnServer() ? "Use the Mega Vil phone here: /phone or /call <number>." : "Phone calls need a server with MP %s or newer.", "4.13.59"); return; }
    if(id < 0 || id >= MP_MAX_PLAYERS || !players[id].active || id == myId) { SysMsg(COL_RED, "[PHONE] There is no player with that id."); return; }
    if(PhoneBusy()) PhoneSend(CALL_HANGUP, -1);
    s_Ph.ringFrom = id; s_Ph.ringOut = true; s_Ph.ringAt = MP_NowMs(); s_Ph.callWith = -1;
    MP_CopyStr(s_Ph.name, players[id].name, sizeof(s_Ph.name));
    s_Ph.open = true; s_Ph.app = 1; s_Ph.endedAt = 0;
    PhoneSend(CALL_DIAL, id);
}
static void PhoneAnswer() { if(s_Ph.ringFrom >= 0 && !s_Ph.ringOut && s_Ph.callWith < 0) PhoneSend(CALL_ACCEPT, s_Ph.ringFrom); }
static void PhoneHangup()
{
    if(s_Ph.ringFrom >= 0 && !s_Ph.ringOut && s_Ph.callWith < 0) PhoneSend(CALL_DECLINE, s_Ph.ringFrom);
    else if(PhoneBusy()) PhoneSend(CALL_HANGUP, -1);
    PhoneEnded("Call ended.");
}

static void PhoneOnCall(const PktCall& c)
{
    const int o = c.other < MP_MAX_PLAYERS ? c.other : -1;
    char t[64];
    switch(c.action)
    {
        case CALL_RING:   // somebody calls me
            if(o < 0) break;
            if(s_Ph.callWith >= 0 || (s_Ph.ringFrom >= 0 && s_Ph.ringFrom != o)) { PhoneSend(CALL_DECLINE, o); break; }   // already on the phone
            s_Ph.ringFrom = o; s_Ph.ringOut = false; s_Ph.ringAt = MP_NowMs(); s_Ph.endedAt = 0;
            MP_CopyStr(s_Ph.name, PhoneName(o), sizeof(s_Ph.name));
            s_Ph.open = true; s_Ph.app = 1;
            SysMsg(COL_YELLOW, "[PHONE] %s is calling you: Answer (/answer) or Decline (/hangup).", s_Ph.name);
            break;
        case CALL_RINGING: if(o >= 0 && s_Ph.ringOut) { s_Ph.ringFrom = o; SysMsg(COL_SYSTEM, "[PHONE] Calling %s...", PhoneName(o)); } break;
        case CALL_LINKED:
            if(o < 0) break;
            s_Ph.callWith = o; s_Ph.ringFrom = -1; s_Ph.ringOut = false; s_Ph.callStart = MP_NowMs();
            MP_CopyStr(s_Ph.name, PhoneName(o), sizeof(s_Ph.name));
            s_Ph.open = true; s_Ph.app = 1;
            SysMsg(COL_GREEN, "[PHONE] On a call with %s. Your voice chat goes to their phone, wherever they are.", s_Ph.name);
            break;
        case CALL_ENDED:       if(PhoneBusy()) PhoneEnded("Call ended."); break;
        case CALL_DECLINED:    if(PhoneBusy()) { snprintf(t, sizeof(t), "%s declined the call.", PhoneName(o)); PhoneEnded(t); } break;
        case CALL_BUSY:        if(PhoneBusy()) { snprintf(t, sizeof(t), "%s is on another call.", PhoneName(o)); PhoneEnded(t); } break;
        case CALL_UNAVAILABLE: if(PhoneBusy()) PhoneEnded("The player can't take calls right now."); break;
        default: break;
    }
}

// call sync: the game's phone task on CJ while ringing out or talking (on foot)
static void PhoneProcess(int ped, uint64_t now)
{
    if(!IsConnected() || RpOnServer()) { if(PhoneBusy()) PhoneReset(); }
    if(s_Ph.ringFrom >= 0 && !players[s_Ph.ringFrom].active) PhoneEnded("The player left.");
    if(s_Ph.callWith >= 0 && !players[s_Ph.callWith].active) PhoneEnded("The player left.");
    if(s_Ph.ringFrom >= 0 && s_Ph.ringOut && now - s_Ph.ringAt > 30000) { PhoneSend(CALL_HANGUP, -1); PhoneEnded("No answer."); }
    if(s_Ph.ringFrom >= 0 && !s_Ph.ringOut && now - s_Ph.ringAt > 30000) { PhoneSend(CALL_DECLINE, s_Ph.ringFrom); PhoneEnded("Missed call."); }
    // ring tone (incoming) / ring back (outgoing): the phone tap sound every 1.5 s
    if(s_Ph.ringFrom >= 0 && s_Ph.callWith < 0 && now - s_Ph.ringSndAt > 1500) { s_Ph.ringSndAt = now; RpClick(RPS_PHONE); }
    const bool want = ped > 0 && (s_Ph.callWith >= 0 || (s_Ph.ringOut && s_Ph.ringFrom >= 0)) && !Game::IsCharInAnyCar(ped) && !Game::IsPaused();
    if(want && (!s_Ph.task || s_Ph.taskPed != ped))
    {
        if(now - s_Ph.taskAt < 1500) return;
        s_Ph.taskAt = now; Game::TaskUsePhone(ped, true); s_Ph.task = true; s_Ph.taskPed = ped;
    }
    else if(!want && s_Ph.task)
    {
        if(s_Ph.taskPed > 0 && Game::PedPtr(s_Ph.taskPed)) Game::TaskUsePhone(s_Ph.taskPed, false);
        s_Ph.task = false; s_Ph.taskPed = 0; s_Ph.taskAt = now;
    }
}

static bool PhoneCommand(const char* cmd, const char* arg)
{
    if(RpOnServer() || !IsConnected()) return false;   // Mega Vil: its own /phone /call /h
    if(!strcmp(cmd, "phone") || !strcmp(cmd, "p") || !strcmp(cmd, "cellphone"))
    { if(!PhoneServerOk()) { SysMsg(COL_RED, "Phone calls need a server with MP 4.13.59 or newer."); return true; } if(s_Ph.open) s_Ph.open = false; else PhoneOpen(); return true; }
    if(!strcmp(cmd, "call"))
    {
        if(!*arg) { PhoneOpen(); return true; }
        int id = -1;
        if(isdigit((unsigned char)arg[0])) id = atoi(arg);
        else for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(players[i].active && !strncasecmp(players[i].name, arg, strlen(arg))) { id = i; break; }
        PhoneDial(id); return true;
    }
    if(!strcmp(cmd, "answer") || !strcmp(cmd, "pickup")) { if(s_Ph.ringFrom >= 0 && !s_Ph.ringOut) PhoneAnswer(); else SysMsg(COL_RED, "[PHONE] Nobody is calling you."); return true; }
    if(!strcmp(cmd, "hangup") || !strcmp(cmd, "h")) { if(PhoneBusy()) PhoneHangup(); else SysMsg(COL_RED, "[PHONE] You're not on a call."); return true; }
    return false;
}

// a Call button for the player list (Players online)
static void PhoneButton(int id, float w)
{
    if(!PhoneServerOk() || id == myId) return;
    if(w < 0) w = ImGui::GetContentRegionAvail().x;
    const bool with = s_Ph.callWith == id || s_Ph.ringFrom == id;
    if(RpPBtn(with ? "Hang up##phb" : "Call (phone)##phb", w, UI::S(64), with ? 0xDC2626FF : 0x16A34AFF)) { if(with) PhoneHangup(); else PhoneDial(id); }
}

static void PhoneCallScreen(float w, float h)
{
    uint64_t now = MP_NowMs();
    bool incoming = s_Ph.ringFrom >= 0 && !s_Ph.ringOut && s_Ph.callWith < 0;
    bool outgoing = s_Ph.ringFrom >= 0 && s_Ph.ringOut && s_Ph.callWith < 0;
    const char* who = s_Ph.name[0] ? s_Ph.name : "Player";
    ImGui::Dummy(ImVec2(1, h * 0.10f));
    ImGui::SetWindowFontScale(1.5f);
    float tw = ImGui::CalcTextSize(who).x; ImGui::SetCursorPosX((w - tw) * 0.5f); RpPText(0xFFFFFFFF, "%s", who);
    ImGui::SetWindowFontScale(1.0f);
    char t[64];
    if(s_Ph.callWith >= 0) { int s = (int)((now - s_Ph.callStart) / 1000); snprintf(t, sizeof(t), "id %d   -   %d:%02d", s_Ph.callWith, s / 60, s % 60); }
    else if(incoming || outgoing) snprintf(t, sizeof(t), "id %d   -   %s", s_Ph.ringFrom, incoming ? "incoming call..." : "calling...");
    else snprintf(t, sizeof(t), "%s", s_Ph.endText[0] ? s_Ph.endText : "Call ended.");
    tw = ImGui::CalcTextSize(t).x; ImGui::SetCursorPosX((w - tw) * 0.5f); RpPText(0xBAE6FDFF, "%s", t);
    ImDrawList* dl = ImGui::GetWindowDrawList(); ImVec2 c = ImGui::GetCursorScreenPos(); c.x += w * 0.5f; c.y += h * 0.18f;
    if(incoming || outgoing) for(int i = 0; i < 3; ++i) { float k = fmodf(now / 1200.0f + i / 3.0f, 1.0f); dl->AddCircle(c, UI::S(40) + k * UI::S(70), RpCol(0x22C55EFF, 1.0f - k), 40, UI::S(3)); }
    dl->AddCircleFilled(c, UI::S(42), RpCol(s_Ph.callWith >= 0 ? 0x16A34AFF : 0x0EA5E9FF, 1), 40);
    { char ini[4] = { (char)toupper((unsigned char)who[0]), 0 }; float fs = UI::S(44); UI::OutlinedText(dl, fs, ImVec2(c.x - RpTextW(fs, ini) * 0.5f, c.y - fs * 0.55f), IM_COL32_WHITE, ini); }
    ImGui::Dummy(ImVec2(1, h * 0.40f));
    if(s_Ph.callWith >= 0) RpPText(0x86EFACFF, "Voice chat goes to this call. Hold the mic button to talk.");
    float bw = (w - UI::S(12)) / 2;
    if(incoming)
    {
        if(RpPBtn("Answer##pa", bw, UI::S(64), 0x16A34AFF)) PhoneAnswer();
        ImGui::SameLine();
        if(RpPBtn("Decline##pd", bw, UI::S(64), 0xDC2626FF)) PhoneHangup();
    }
    else if(outgoing || s_Ph.callWith >= 0) { if(RpPBtn("End call##pe", w, UI::S(64), 0xDC2626FF)) PhoneHangup(); }
    else if(RpPBtn("Contacts##pc", w, UI::S(52), 0x334155FF)) s_Ph.app = 0;
}

static void PhoneContacts(float w)
{
    RpPTitle("Contacts - players online");
    int n = 0;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        if(!players[i].active || i == myId) continue;
        ++n; ImGui::PushID(i);
        float bw = UI::S(96);
        ImGui::AlignTextToFramePadding();
        ImVec4 col = ImGui::ColorConvertU32ToFloat4(UI::FromRGBA(players[i].color | 0xFF));
        ImGui::TextColored(col, "%s", players[i].name); ImGui::SameLine(); RpPText(0x94A3B8FF, "id %d", i);
        ImGui::SameLine(w - bw);
        if(RpPBtn("Call##c", bw, UI::S(44), 0x16A34AFF)) PhoneDial(i);
        ImGui::PopID();
    }
    if(!n) RpPText(0x94A3B8FF, "Nobody else is online on this server yet.");
    RpPSep();
    RpPText(0x94A3B8FF, "/call <id or name>   /answer   /hangup");
}

// the phone (same look as the Mega Vil phone) + a ringing icon while the phone is closed
static void PhoneRender()
{
    if(!PhoneServerOk() || FloatMenu::IsModal() || Game::IsPaused() || SampDialogOpen()) return;
    uint64_t now = MP_NowMs();
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    if(!s_Ph.open)
    {
        if(!PhoneBusy()) return;
        // ringing / on a call with the phone put away: a shaking phone icon, tap = open
        const float s = UI::S(120);
        const bool ring = s_Ph.ringFrom >= 0 && !s_Ph.ringOut && s_Ph.callWith < 0;
        ImVec2 pos(ds.x - UI::S(560) - s * 0.5f, ds.y * 0.60f - s * 0.5f);
        BcwBeginOverlay("##PhIcon", ImVec2(pos.x - UI::S(6), pos.y - UI::S(6)), ImVec2(s + UI::S(12), s + UI::S(12)), 0.0f);
        ImGui::SetCursorScreenPos(pos);
        if(ImGui::InvisibleButton("##phi", ImVec2(s, s))) PhoneOpen();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float shake = ring ? sinf((float)(now % 100000) * 0.03f) * UI::S(6) : 0.0f;
        const ImVec2 c(pos.x + s * 0.5f + shake, pos.y + s * 0.5f);
        const ImU32 tint = (ring && now % 600 < 300) ? IM_COL32(120, 255, 160, 255) : IM_COL32_WHITE;
        ImVec2 uv(1, 1);
        if(void* tex = HUD::Texture("rp_phone.png", &uv)) dl->AddImage((ImTextureID)tex, ImVec2(c.x - s * 0.5f, c.y - s * 0.5f), ImVec2(c.x + s * 0.5f, c.y + s * 0.5f), ImVec2(0, 0), uv, tint);
        else
        {
            const float bw = s * 0.30f, bh = s * 0.46f;
            dl->AddRectFilled(ImVec2(c.x - bw, c.y - bh), ImVec2(c.x + bw, c.y + bh), IM_COL32(12, 14, 20, 255), UI::S(12));
            dl->AddRectFilled(ImVec2(c.x - bw + UI::S(6), c.y - bh + UI::S(8)), ImVec2(c.x + bw - UI::S(6), c.y + bh - UI::S(14)), RpCol(0x22C55EFF, 1), UI::S(8));
            dl->AddRect(ImVec2(c.x - bw, c.y - bh), ImVec2(c.x + bw, c.y + bh), tint, UI::S(12), 0, UI::S(4));
        }
        BcwEndOverlay();
        return;
    }
    if(s_Ph.app == 1 && !PhoneBusy() && s_Ph.endedAt && now - s_Ph.endedAt > 3500) { s_Ph.app = 0; s_Ph.endedAt = 0; }
    float H = ds.y * 0.80f, W = H * 0.49f;
    float x = ds.x - W - UI::S(380), y = (ds.y - H) * 0.5f;
    if(x < UI::S(10)) x = UI::S(10);
    ImGui::SetNextWindowPos(ImVec2(x, y)); ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##MpPhone", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float bez = UI::S(12), rad = UI::S(42);
    ImVec2 a(x, y), b(x + W, y + H), sa(x + bez, y + bez), sb(x + W - bez, y + H - bez);
    dl->AddRectFilled(a, b, IM_COL32(10, 10, 14, 255), rad);
    dl->AddRect(a, b, RpCol(0x22C55EFF, 0.9f), rad, 0, UI::S(2.5f));
    dl->AddRectFilledMultiColor(sa, sb, RpCol(0x0F766EFF, 1), RpCol(0x0F766EFF, 1), RpCol(0x06121FFF, 1), RpCol(0x06121FFF, 1));
    dl->AddRect(ImVec2(sa.x - UI::S(6), sa.y - UI::S(6)), ImVec2(sb.x + UI::S(6), sb.y + UI::S(6)), IM_COL32(10, 10, 14, 255), rad - UI::S(4), 0, UI::S(12));
    dl->AddRectFilled(ImVec2(x + W * 0.38f, sa.y), ImVec2(x + W * 0.62f, sa.y + UI::S(22)), IM_COL32(10, 10, 14, 255), UI::S(11), ImDrawFlags_RoundCornersBottom);
    time_t tt = time(nullptr); char clock[16]; strftime(clock, sizeof(clock), "%H:%M", localtime(&tt));
    UI::OutlinedText(dl, UI::S(18), ImVec2(sa.x + UI::S(26), sa.y + UI::S(4)), IM_COL32_WHITE, clock);
    const char* net = "MP 5G  100%"; UI::OutlinedText(dl, UI::S(16), ImVec2(sb.x - RpTextW(UI::S(16), net) - UI::S(26), sa.y + UI::S(5)), IM_COL32_WHITE, net);
    const float pad = UI::S(16), top = UI::S(40), bottom = UI::S(64);
    float cw = (sb.x - sa.x) - pad * 2;
    ImGui::SetCursorScreenPos(ImVec2(sa.x + pad, sa.y + top));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("##mpphscr", ImVec2(cw, (sb.y - sa.y) - top - bottom), false, ImGuiWindowFlags_NoScrollbar);
    float w = cw - UI::S(4);
    if(s_Ph.app == 1) PhoneCallScreen(w, (sb.y - sa.y) - top - bottom); else PhoneContacts(w);
    RpDragScroll();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    float hy = sb.y - bottom + UI::S(10), hbw = (cw - UI::S(10)) / 2;
    ImGui::SetCursorScreenPos(ImVec2(sa.x + pad, hy));
    if(RpPBtn(PhoneBusy() && s_Ph.app == 0 ? "Call##phh" : "Contacts##phh", hbw, UI::S(44), 0x1E293BFF)) s_Ph.app = PhoneBusy() && s_Ph.app == 0 ? 1 : 0;
    ImGui::SameLine(0, UI::S(10));
    if(RpPBtn("Close##phc", hbw, UI::S(44), 0x1E293BFF)) s_Ph.open = false;
    dl->AddRectFilled(ImVec2(x + W * 0.36f, sb.y - UI::S(10)), ImVec2(x + W * 0.64f, sb.y - UI::S(5)), IM_COL32(255, 255, 255, 200), UI::S(3));
    ImGui::End();
    ImGui::PopStyleVar(2); ImGui::PopStyleColor();
}
