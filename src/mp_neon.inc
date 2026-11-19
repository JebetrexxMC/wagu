// MP 4.13.22: the "neon" main menu + server browser (Screenshots/neon-main-menu.jpg, neon-server-browser.jpg).
//
// ServerBrowserMode= in files/TYFMC/TYFMC.MP.ini ([MP]):
//   neon     (set by hand) the neon title menu: Resume / New game / Browse servers / Quick connect / Settings / About /
//            Quit game, NEON FEATURED card (5.01.13: no Neon Identity card, only the Discord pop-up), the version label; Browse servers is
//            the neon server browser (its own layout - not the launcher panel). Also shown on the black screen
//            after leaving a server.
//   mp       the old [Multiplayer] tile on the game's menu + the MP launcher panel (4.13.4 - 4.13.21)
//   default  (default, 4.13.30) the game's own main menu only - MP does not hook or touch the title screen
// In the game (floating 3-dots menu > Server) the launcher panel stays as it was.
// Background: neon_bg.jpg (the San Andreas fan art), logo: title.png. Fonts: imgui/neonfonts.h (OFL).

enum { NP_MAIN = 0, NP_BROWSER, NP_SETTINGS, NP_ABOUT, NP_QUIT };
static bool NeonPadUI();   // 5.01.12: the controller hints (SELECT / A CONFIRM, B BACK, LB RB ...) only while a pad is in use
static int  s_nNeonPage = NP_MAIN, s_nNeonSel = 2;   // Browse servers
static bool s_bNeonHidden = false;                   // "Game settings": the game's own menu for now
static LRow s_NeonPending;
static char s_szNeonSearch[64] = "";
static bool s_bNeonQuickAfterLoad = false;   // 4.13.28: Quick connect tapped on the title
static int  s_nNeonList = 0;                         // 0 all, 1 featured, 2 favourites, 3 Wi-Fi
static bool s_bNeonListOpen = false, s_bNeonFilterOpen = false;
static bool s_bNeonHideFull = false, s_bNeonHideEmpty = false, s_bNeonHideLocked = false;
static char s_szNeonSelKey[64] = "";
static int  s_nNeonSetSec = -1;
static uint64_t s_nNeonCopiedAt = 0, s_nNeonTapAt = 0;

bool MP::NeonMenuMode() { return NeonOn(); }

// ---- small drawing helpers ------------------------------------------------------------------
// 5.01.14: the MP launcher palette (was gold): kNGold = the launcher blue accent, slate panels, light lines
static const ImU32 kNGold = IM_COL32(126, 178, 226, 255), kNGoldDim = IM_COL32(90, 128, 166, 255);
static const ImU32 kNGrey = IM_COL32(150, 157, 166, 255), kNText = IM_COL32(226, 229, 233, 255);
static const ImU32 kNPanel = IM_COL32(30, 34, 40, 225), kNLine = IM_COL32(64, 70, 78, 255);
static const ImU32 kNBlue = IM_COL32(159, 194, 255, 255);
// letter-spaced text (the tracked small caps labels of the reference)
static float NeonTextW(ImFont* f, float fs, const char* t, float sp)
{
    float w = 0; const char* p = t;
    while(*p) { unsigned int c = 0; int k = ImTextCharFromUtf8(&c, p, nullptr); if(k <= 0) break;
                w += f->CalcTextSizeA(fs, FLT_MAX, 0, p, p + k).x + sp; p += k; }
    return w > 0 ? w - sp : 0;
}
static void NeonText(ImDrawList* dl, ImFont* f, float fs, ImVec2 at, ImU32 col, const char* t, float sp = 0, bool shadow = false)
{
    if(!t || !*t) return;
    if(sp <= 0.01f)
    {
        if(shadow) dl->AddText(f, fs, ImVec2(at.x + fs * 0.04f, at.y + fs * 0.06f), IM_COL32(0, 0, 0, 200), t);
        dl->AddText(f, fs, at, col, t); return;
    }
    const char* p = t; float x = at.x;
    while(*p)
    {
        unsigned int c = 0; int k = ImTextCharFromUtf8(&c, p, nullptr); if(k <= 0) break;
        if(shadow) dl->AddText(f, fs, ImVec2(x + fs * 0.04f, at.y + fs * 0.06f), IM_COL32(0, 0, 0, 200), p, p + k);
        dl->AddText(f, fs, ImVec2(x, at.y), col, p, p + k);
        x += f->CalcTextSizeA(fs, FLT_MAX, 0, p, p + k).x + sp; p += k;
    }
}
static void NeonTextR(ImDrawList* dl, ImFont* f, float fs, ImVec2 rightTop, ImU32 col, const char* t, float sp = 0)
{ NeonText(dl, f, fs, ImVec2(rightTop.x - NeonTextW(f, fs, t, sp), rightTop.y), col, t, sp); }
static void NeonUpper(char* o, size_t n, const char* s) { size_t i = 0; for(; s && s[i] && i + 1 < n; ++i) o[i] = (char)toupper((unsigned char)s[i]); o[i] = 0; }
static void NeonTri(ImDrawList* dl, ImVec2 c, float r, ImU32 col, int dir = 0)   // 0 right, 1 down, 2 up, 3 left
{
    if(dir == 0) dl->AddTriangleFilled(ImVec2(c.x - r * 0.6f, c.y - r), ImVec2(c.x - r * 0.6f, c.y + r), ImVec2(c.x + r * 0.9f, c.y), col);
    else if(dir == 1) dl->AddTriangleFilled(ImVec2(c.x - r, c.y - r * 0.6f), ImVec2(c.x + r, c.y - r * 0.6f), ImVec2(c.x, c.y + r * 0.9f), col);
    else if(dir == 2) dl->AddTriangleFilled(ImVec2(c.x - r, c.y + r * 0.6f), ImVec2(c.x + r, c.y + r * 0.6f), ImVec2(c.x, c.y - r * 0.9f), col);
    else dl->AddTriangleFilled(ImVec2(c.x + r * 0.6f, c.y - r), ImVec2(c.x + r * 0.6f, c.y + r), ImVec2(c.x - r * 0.9f, c.y), col);
}
static void NeonChevron(ImDrawList* dl, ImVec2 c, float r, ImU32 col, float th, bool down = false)
{
    if(down) { ImVec2 p[3] = { ImVec2(c.x - r, c.y - r * 0.5f), ImVec2(c.x, c.y + r * 0.5f), ImVec2(c.x + r, c.y - r * 0.5f) }; dl->AddPolyline(p, 3, col, 0, th); }
    else     { ImVec2 p[3] = { ImVec2(c.x - r * 0.5f, c.y - r), ImVec2(c.x + r * 0.5f, c.y), ImVec2(c.x - r * 0.5f, c.y + r) }; dl->AddPolyline(p, 3, col, 0, th); }
}
// a tap target (no look): true on release inside
static bool NeonHit(const char* id, ImVec2 a, ImVec2 b)
{
    ImGui::SetCursorScreenPos(a);
    return ImGui::InvisibleButton(id, ImVec2(ImMax(1.0f, b.x - a.x), ImMax(1.0f, b.y - a.y)));
}
static void NeonBackdrop(ImDrawList* dl, ImVec2 ds, int dark)
{
    dl->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(6, 6, 8, 255));
    ImVec2 uv(1, 1);
    if(void* t = HUD::Texture("neon_bg.jpg", &uv))
    {   // the art on the right (like the reference: the character right, the menu over the dark left)
        const float ih = ds.y, iw = ih * (1131.0f / 707.0f), x0 = ImMax(ds.x - iw, ds.x * 0.30f);
        dl->AddImage((ImTextureID)t, ImVec2(x0, 0), ImVec2(x0 + iw, ih), ImVec2(0, 0), uv, IM_COL32(255, 255, 255, 255));
        dl->AddRectFilledMultiColor(ImVec2(x0, 0), ImVec2(x0 + iw * 0.55f, ds.y), IM_COL32(6, 6, 8, 255), IM_COL32(6, 6, 8, 0), IM_COL32(6, 6, 8, 0), IM_COL32(6, 6, 8, 255));
    }
    dl->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(24, 6, 8, dark));   // the dark red night of the reference
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(ds.x * 0.62f, ds.y), IM_COL32(4, 4, 6, 235), IM_COL32(4, 4, 6, 60), IM_COL32(4, 4, 6, 60), IM_COL32(4, 4, 6, 235));
    dl->AddRectFilledMultiColor(ImVec2(0, ds.y * 0.78f), ds, IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 190), IM_COL32(0, 0, 0, 190));
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(ds.x, ds.y * 0.16f), IM_COL32(0, 0, 0, 150), IM_COL32(0, 0, 0, 150), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0));
}

// ---- the server rows (the launcher's data, merged: featured first) --------------------------
static void NeonNearby(std::vector<LanServer>& nearby)
{
    ServerList::Poll();
    if(MP_NowMs() - ServerList::LastRefreshMs() > 10000) ServerList::Refresh();
    { std::lock_guard<std::mutex> lk(s_LanMutex); nearby = s_Nearby; }
    static std::vector<LanServer> live; static uint64_t liveAt = 0;
    if(MP_NowMs() - liveAt > 1000) { liveAt = MP_NowMs(); live = Online::Snapshot(); }
    for(auto& n : nearby) if(n.token) for(auto& l : live) if(l.token == n.token) { n.players = l.players; n.maxPlayers = l.maxPlayers; break; }
    for(auto& l : live)
    {   // lobby servers the last scan has not seen yet
        bool have = false; for(auto& n : nearby) if(n.token == l.token && l.token) { have = true; break; }
        if(!have) nearby.push_back(l);
    }
    if(s_bHasNetwork && !IsSearching() && MP_NowMs() - s_nNearbyTime > 15000) { s_nNearbyTime = MP_NowMs(); StartJob(JOB_SCAN, false, "", MP_DEFAULT_PORT); }
}
static bool NeonFeatured(const LRow& r) { return r.kind == 0 || (r.kind >= 3 && r.kind <= 7); }
static bool NeonFind(const char* hay, const char* needle)
{
    if(!needle[0]) return true; if(!hay || !hay[0]) return false;
    char a[200], b[64]; MP_CopyStr(a, hay, sizeof(a)); MP_CopyStr(b, needle, sizeof(b));
    for(char* c = a; *c; ++c) *c = (char)tolower((unsigned char)*c);
    for(char* c = b; *c; ++c) *c = (char)tolower((unsigned char)*c);
    return strstr(a, b) != nullptr;
}
static bool NeonIsAddress(const char* s)
{   // "1.2.3.4", "1.2.3.4:7777", "host.name:7777", "mp://..."
    if(!s || !*s) return false;
    if(!strncasecmp(s, "mp://", 5)) return true;
    int dots = 0, digits = 0; for(const char* p = s; *p; ++p) { if(*p == ' ') return false; if(*p == '.') ++dots; if(isdigit((unsigned char)*p)) ++digits; }
    return dots >= 1 && (strchr(s, ':') || digits >= 4);
}
static void NeonBuild(std::vector<LRow>& out)
{
    out.clear();
    std::vector<LanServer> nearby; NeonNearby(nearby);
    char keep[32]; MP_CopyStr(keep, s_szSearch, sizeof(keep)); s_szSearch[0] = 0;
    const int sub = s_nInetSub, sc = s_nSortCol; s_nSortCol = -1;
    std::vector<LRow> tmp;
    auto add = [&](const LRow& r)
    {
        for(auto& q : out) if(q.port == r.port && !strcasecmp(q.name, r.name) && q.kind == r.kind) return;
        out.push_back(r);
    };
    if(s_nNeonList == 0 || s_nNeonList == 1) { s_nInetSub = 1; LBuildRows(LT_INTERNET, nearby, tmp); for(auto& r : tmp) add(r); }
    if(s_nNeonList == 0) { s_nInetSub = 0; LBuildRows(LT_INTERNET, nearby, tmp); for(auto& r : tmp) add(r); }
    if(s_nNeonList == 0 || s_nNeonList == 3) { LBuildRows(LT_LAN, nearby, tmp); for(auto& r : tmp) add(r); }
    if(s_nNeonList == 2) { LBuildRows(LT_FAVORITES, nearby, tmp); for(auto& r : tmp) add(r); }
    s_nInetSub = sub; s_nSortCol = sc; MP_CopyStr(s_szSearch, keep, sizeof(s_szSearch));
    const bool addr = NeonIsAddress(s_szNeonSearch);
    std::vector<LRow> f;
    for(auto& r : out)
    {
        if(!addr && s_szNeonSearch[0] && !NeonFind(r.name, s_szNeonSearch) && !NeonFind(r.desc, s_szNeonSearch) && !NeonFind(r.mode, s_szNeonSearch) && !NeonFind(r.lang, s_szNeonSearch)) continue;
        if(s_bNeonHideFull && r.maxp > 0 && r.players >= r.maxp) continue;
        if(s_bNeonHideEmpty && r.players <= 0) continue;
        if(s_bNeonHideLocked && r.locked) continue;
        f.push_back(r);
    }
    std::stable_sort(f.begin(), f.end(), [](const LRow& a, const LRow& b)
    {
        const bool fa = NeonFeatured(a), fb = NeonFeatured(b);
        if(fa != fb) return fa;
        if(fa) return false;   // the featured servers keep their own order (default, BCW, MW3, Mega Vil, Day Z, League)
        return a.players > b.players;
    });
    out.swap(f);
}
static void NeonRowKey(const LRow& r, char* o, size_t n) { snprintf(o, n, "%d|%s|%d", r.kind, r.name, r.port); }
static void NeonRowIcon(ImDrawList* dl, const LRow& r, ImVec2 a, float sz)
{
    const ImVec2 b(a.x + sz, a.y + sz);
    ImVec2 uv(1, 1); void* t = LRowPreview(r, &uv); if(!t) t = LRowBg(r, &uv);
    if(t)
    {
        dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 255), sz * 0.12f);
        LImageCover(dl, t, uv, a, b, IM_COL32(255, 255, 255, 255));
        dl->AddRect(a, b, IM_COL32(120, 64, 40, 255), sz * 0.12f, 0, UI::S(1.5f));
        return;
    }
    uint32_t h = 2166136261u; for(const char* p = r.name; *p; ++p) h = (h ^ (unsigned char)tolower((unsigned char)*p)) * 16777619u;
    static const ImU32 kc[5] = { IM_COL32(150, 52, 52, 255), IM_COL32(52, 140, 72, 255), IM_COL32(56, 92, 160, 255), IM_COL32(160, 120, 50, 255), IM_COL32(120, 70, 150, 255) };
    const ImU32 c = kc[h % 5];
    dl->AddRectFilled(a, b, IM_COL32(16, 16, 19, 255), sz * 0.12f);
    dl->AddRect(a, b, c, sz * 0.12f, 0, UI::S(1.5f));
    char ini[4] = { 0 }; int n = 0;
    for(const char* p = r.name; *p && n < 2; ++p) if(isalnum((unsigned char)*p) && (p == r.name || !isalnum((unsigned char)p[-1]))) ini[n++] = (char)toupper((unsigned char)*p);
    if(!n) { ini[0] = 'M'; ini[1] = 'P'; }
    ImFont* f = UI::CapsFont(); const float fs = sz * 0.30f;
    const ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0, ini);
    dl->AddText(f, fs, ImVec2(a.x + (sz - ts.x) * 0.5f, a.y + (sz - ts.y) * 0.5f), (c & 0x00FFFFFF) | 0xE0000000, ini);
}
static void NeonFlags(ImDrawList* dl, const LRow& r, ImVec2 rightTop, float fh)
{
    const float fw = fh * 1.45f;
    const int ci = LCountryIndex(r.cc[0] ? r.cc : MP_CC_GLOBAL);
    ImVec2 a(rightTop.x - fw, rightTop.y);
    LDrawFlag(dl, ci, a, ImVec2(a.x + fw, a.y + fh));
}
static void NeonJoinRow(const LRow& r);

// ---- the server browser (Browse servers) ------------------------------------------------------
static void OnNeonSearch(const char* t, void*) { MP_CopyStr(s_szNeonSearch, t ? t : "", sizeof(s_szNeonSearch)); }
static bool NeonJoinAddress(const char* s)
{
    if(!NeonIsAddress(s)) return false;
    char host[96]; MP_CopyStr(host, !strncasecmp(s, "mp://", 5) ? s + 5 : s, sizeof(host));
    int port = MP_DEFAULT_PORT; if(char* c = strrchr(host, ':')) { *c = 0; port = atoi(c + 1); if(port <= 0 || port > 65535) port = MP_DEFAULT_PORT; }
    LRow r; memset(&r, 0, sizeof(r)); r.kind = 1; MP_CopyStr(r.ip, host, sizeof(r.ip)); r.port = port; MP_CopyStr(r.name, host, sizeof(r.name));
    NeonJoinRow(r); return true;
}
static void NeonBrowser(ImDrawList* dl, ImVec2 ds)
{
    const float u = ds.y / 773.0f;
    auto X = [&](float rx) { return ds.x * rx / 1487.0f; };
    ImFont* uf = UI::Font(); ImFont* cf = UI::CapsFont(); ImFont* pd = UI::PricedownFont() ? UI::PricedownFont() : UI::GothicFont();
    static std::vector<LRow> rows; static uint64_t builtAt = 0; static int builtList = -1; static char builtQ[64] = "\x01";
    if(MP_NowMs() - builtAt > 1000 || builtList != s_nNeonList || strcmp(builtQ, s_szNeonSearch))
    { builtAt = MP_NowMs(); builtList = s_nNeonList; MP_CopyStr(builtQ, s_szNeonSearch, sizeof(builtQ)); NeonBuild(rows); }
    int sel = -1; char key[64];
    for(int i = 0; i < (int)rows.size(); ++i) { NeonRowKey(rows[i], key, sizeof(key)); if(!strcmp(key, s_szNeonSelKey)) sel = i; }
    if(sel < 0 && !rows.empty()) { sel = 0; NeonRowKey(rows[0], s_szNeonSelKey, sizeof(s_szNeonSelKey)); }
    int total = 0; for(auto& r : rows) total += r.players > 0 ? r.players : 0;

    // header
    NeonText(dl, uf, 10.5f * u, ImVec2(X(57), 30 * u), kNGrey, "MP NEON NETWORK", 4.2f * u);
    NeonText(dl, pd, 62 * u, ImVec2(X(55), 42 * u), IM_COL32(190, 192, 196, 255), "choose your server", 0, true);
    char t1[48], t2[64]; snprintf(t1, sizeof(t1), "%d SERVERS", (int)rows.size()); snprintf(t2, sizeof(t2), "%d PLAYERS ONLINE", total);
    NeonTextR(dl, uf, 11.5f * u, ImVec2(X(1185), 33 * u), kNGrey, t1, 1.6f * u);
    NeonTextR(dl, uf, 11.5f * u, ImVec2(X(1185), 52 * u), kNGrey, t2, 1.6f * u);
    {   // BACK TO MAIN MENU
        const ImVec2 a(X(1212), 30 * u), b(X(1430), 70 * u);
        dl->AddRectFilled(a, b, IM_COL32(10, 10, 12, 240), 5 * u);
        dl->AddRect(a, b, IM_COL32(205, 182, 120, 255), 5 * u, 0, 1.6f * u);
        const float cy = (a.y + b.y) * 0.5f;
        dl->AddLine(ImVec2(a.x + 18 * u, cy), ImVec2(a.x + 31 * u, cy), kNText, 1.6f * u);
        NeonChevron(dl, ImVec2(a.x + 21 * u, cy), 5 * u, kNText, 1.6f * u); // drawn as "<" below
        dl->AddLine(ImVec2(a.x + 18 * u, cy), ImVec2(a.x + 24 * u, cy - 6 * u), kNText, 1.6f * u);
        dl->AddLine(ImVec2(a.x + 18 * u, cy), ImVec2(a.x + 24 * u, cy + 6 * u), kNText, 1.6f * u);
        NeonText(dl, uf, 10.5f * u, ImVec2(a.x + 44 * u, cy - 6.5f * u), kNText, "BACK TO MAIN MENU", 2.6f * u);
        if(NeonHit("##nbBack", a, b)) { s_nNeonPage = NP_MAIN; s_bNeonListOpen = s_bNeonFilterOpen = false; }
    }
    // toolbar
    const ImVec2 ta(X(57), 129 * u), tb(X(1430), 179 * u);
    dl->AddRectFilled(ta, tb, IM_COL32(13, 14, 17, 240), 5 * u);
    dl->AddRect(ta, tb, kNLine, 5 * u, 0, 1.2f * u);
    const float tcy = (ta.y + tb.y) * 0.5f;
    static const char* const kLists[4] = { "NEON SERVERS", "FEATURED", "FAVORITES", "WI-FI / LAN" };
    {
        NeonText(dl, uf, 11 * u, ImVec2(X(78), tcy - 7 * u), IM_COL32(180, 182, 188, 255), kLists[s_nNeonList], 3.2f * u);
        const float w = NeonTextW(uf, 11 * u, kLists[s_nNeonList], 3.2f * u);
        NeonChevron(dl, ImVec2(X(78) + w + 16 * u, tcy), 4.5f * u, IM_COL32(180, 182, 188, 255), 1.4f * u, true);
        if(NeonHit("##nbList", ImVec2(X(66), ta.y), ImVec2(X(245), tb.y))) { s_bNeonListOpen = !s_bNeonListOpen; s_bNeonFilterOpen = false; }
    }
    {   // refresh
        const ImVec2 c(X(268), tcy); const float r = 7.5f * u;
        dl->PathArcTo(c, r, 0.6f, 5.6f, 18); dl->PathStroke(IM_COL32(180, 182, 188, 255), 0, 1.6f * u);
        NeonTri(dl, ImVec2(c.x + r * 0.8f, c.y - r * 0.55f), 3 * u, IM_COL32(180, 182, 188, 255), 2);
        if(NeonHit("##nbRefresh", ImVec2(c.x - 16 * u, ta.y), ImVec2(c.x + 16 * u, tb.y))) { builtAt = 0; ServerList::Refresh(); s_nNearbyTime = 0; }
    }
    {   // search / address
        const ImVec2 a(X(293), 135 * u), b(X(1172), 172 * u);
        dl->AddRectFilled(a, b, IM_COL32(8, 9, 11, 255), 4 * u);
        dl->AddRect(a, b, IM_COL32(48, 50, 56, 255), 4 * u, 0, 1.2f * u);
        const ImVec2 mc(a.x + 22 * u, (a.y + b.y) * 0.5f - 1 * u);
        dl->AddCircle(mc, 5.5f * u, IM_COL32(190, 192, 198, 255), 16, 1.5f * u);
        dl->AddLine(ImVec2(mc.x + 4 * u, mc.y + 4 * u), ImVec2(mc.x + 8 * u, mc.y + 8 * u), IM_COL32(190, 192, 198, 255), 1.6f * u);
        char up[80]; NeonUpper(up, sizeof(up), s_szNeonSearch);
        NeonText(dl, uf, 14 * u, ImVec2(a.x + 38 * u, (a.y + b.y) * 0.5f - 8.5f * u), s_szNeonSearch[0] ? kNText : IM_COL32(170, 172, 178, 255),
                 s_szNeonSearch[0] ? s_szNeonSearch : "SEARCH SERVERS OR ENTER AN ADDRESS", s_szNeonSearch[0] ? 0.6f * u : 1.0f * u);
        if(NeonHit("##nbSearch", a, b)) VKeyboard::Open("Search servers or enter an address (ip:port)", s_szNeonSearch, 63, OnNeonSearch);
    }
    {   // CONNECT
        const ImVec2 a(X(1178), 135 * u), b(X(1301), 172 * u);
        dl->AddRectFilled(a, b, IM_COL32(18, 19, 23, 255), 4 * u);
        dl->AddRect(a, b, IM_COL32(56, 58, 64, 255), 4 * u, 0, 1.2f * u);
        NeonTri(dl, ImVec2(a.x + 19 * u, (a.y + b.y) * 0.5f), 3.2f * u, kNGold);
        NeonText(dl, uf, 10.5f * u, ImVec2(a.x + 32 * u, (a.y + b.y) * 0.5f - 6.5f * u), kNText, "CONNECT", 3.0f * u);
        if(NeonHit("##nbConnect", a, b)) { if(!NeonJoinAddress(s_szNeonSearch) && sel >= 0) NeonJoinRow(rows[sel]); }
    }
    {   // FILTERS
        NeonText(dl, uf, 11 * u, ImVec2(X(1322), tcy - 7 * u), IM_COL32(180, 182, 188, 255), "FILTERS", 3.2f * u);
        const float fx = X(1322) + NeonTextW(uf, 11 * u, "FILTERS", 3.2f * u) + 14 * u;
        dl->AddTriangleFilled(ImVec2(fx - 5 * u, tcy - 5 * u), ImVec2(fx + 5 * u, tcy - 5 * u), ImVec2(fx, tcy + 1 * u), IM_COL32(180, 182, 188, 255));
        dl->AddRectFilled(ImVec2(fx - 1 * u, tcy), ImVec2(fx + 1 * u, tcy + 5 * u), IM_COL32(180, 182, 188, 255));
        if(NeonHit("##nbFilter", ImVec2(X(1310), ta.y), ImVec2(X(1428), tb.y))) { s_bNeonFilterOpen = !s_bNeonFilterOpen; s_bNeonListOpen = false; }
    }
    // column heads
    NeonText(dl, uf, 10.5f * u, ImVec2(X(74), 191 * u), kNGrey, "SERVERS", 1.8f * u);
    NeonTextR(dl, uf, 10.5f * u, ImVec2(X(897), 191 * u), kNGrey, "PLAYERS", 1.8f * u);
    NeonTextR(dl, uf, 10.5f * u, ImVec2(X(944), 191 * u), kNGrey, "PING", 1.8f * u);

    // the list (scrolls; tap = select, tap the selected one again = join)
    const ImVec2 la(X(57), 219 * u), lb(X(966), 712 * u);
    ImGui::SetCursorScreenPos(la);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 4 * u);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(0.25f, 0.26f, 0.29f, 1));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
    if(ImGui::BeginChild("##neonList", ImVec2(lb.x - la.x, lb.y - la.y), false, ImGuiWindowFlags_NoBackground))
    {
        ImDrawList* cl = ImGui::GetWindowDrawList();
        const float rw = X(954) - X(57), rh = 115 * u, gap = 5 * u;
        if(rows.empty())
        {
            const char* m = IsSearching() ? "SEARCHING FOR SERVERS..." : (s_szNeonSearch[0] ? "NO SERVER MATCHES THE SEARCH" : "NO SERVERS ONLINE RIGHT NOW");
            NeonText(cl, uf, 13 * u, ImVec2(la.x + 24 * u, la.y + 30 * u), kNGrey, m, 2.0f * u);
        }
        for(int i = 0; i < (int)rows.size(); ++i)
        {
            const LRow& r = rows[i];
            ImVec2 a = ImGui::GetCursorScreenPos(); a.x = la.x; const ImVec2 b(a.x + rw, a.y + rh);
            ImGui::PushID(i);
            ImGui::SetCursorScreenPos(a);
            const bool tap = ImGui::InvisibleButton("##row", ImVec2(rw, rh));
            const bool press = ImGui::IsItemActive();
            ImGui::PopID();
            const bool on = i == sel;
            cl->AddRectFilled(a, b, on ? IM_COL32(24, 22, 18, 245) : (press ? IM_COL32(26, 27, 31, 245) : IM_COL32(17, 18, 21, 238)), 7 * u);
            cl->AddRect(a, b, on ? kNGold : kNLine, 7 * u, 0, on ? 1.6f * u : 1.1f * u);
            if(on) cl->AddRectFilled(ImVec2(a.x + 1 * u, a.y + 8 * u), ImVec2(a.x + 4 * u, b.y - 8 * u), kNGold, 2 * u);
            NeonTri(cl, ImVec2(a.x + 19 * u, a.y + rh * 0.5f), 2.6f * u, on ? kNGold : IM_COL32(110, 112, 118, 255));
            NeonRowIcon(cl, r, ImVec2(a.x + 41 * u, a.y + (rh - 66 * u) * 0.5f), 66 * u);
            char nm[64]; NeonUpper(nm, sizeof(nm), r.name);
            cl->PushClipRect(ImVec2(a.x + 120 * u, a.y), ImVec2(a.x + 500 * u * (ds.x / ds.y) / (1487.0f / 773.0f), b.y), true);
            NeonText(cl, cf, 19 * u, ImVec2(a.x + 127 * u, a.y + 31 * u), kNText, nm, 2.4f * u, true);
            cl->PopClipRect();
            char desc[MP_DESC_LEN + 40];
            if(r.desc[0]) MP_CopyStr(desc, r.desc, sizeof(desc));
            else snprintf(desc, sizeof(desc), "%s%s%s", r.mode[0] ? r.mode : "Freeroam", r.lang[0] ? " - " : "", r.lang[0] ? r.lang : "");
            const float dmaxX = X(560) - (NeonFeatured(r) ? 110 * u : 0);
            float dw = uf->CalcTextSizeA(14.5f * u, FLT_MAX, 0, desc).x;
            if(a.x + 127 * u + dw > dmaxX) { size_t n = strlen(desc); while(n > 3 && a.x + 127 * u + uf->CalcTextSizeA(14.5f * u, FLT_MAX, 0, desc, desc + n).x > dmaxX - 14 * u) --n; strcpy(desc + n, "..."); dw = uf->CalcTextSizeA(14.5f * u, FLT_MAX, 0, desc).x; }
            NeonText(cl, uf, 14.5f * u, ImVec2(a.x + 127 * u, a.y + 64 * u), IM_COL32(192, 195, 201, 255), desc);
            if(NeonFeatured(r))
            {
                const ImVec2 ga(a.x + 127 * u + dw + 12 * u, a.y + 66 * u);
                const float gw = NeonTextW(uf, 9 * u, "NEON FEATURED", 1.2f * u) + 14 * u;
                cl->AddRect(ga, ImVec2(ga.x + gw, ga.y + 16 * u), kNGold, 1 * u, 0, 1.1f * u);
                NeonText(cl, uf, 9 * u, ImVec2(ga.x + 7 * u, ga.y + 2.8f * u), kNGold, "NEON FEATURED", 1.2f * u);
            }
            NeonFlags(cl, r, ImVec2(X(733), a.y + rh * 0.5f - 6 * u), 12 * u);
            {   // favourite star
                const ImVec2 sc(X(760), a.y + rh * 0.5f);
                const bool fav = LIsFav(r.name, r.port);
                LStar(cl, sc, 8 * u, fav ? kNGold : IM_COL32(150, 152, 158, 255), fav);
                ImGui::PushID(i + 100000);
                if(NeonHit("##star", ImVec2(sc.x - 18 * u, a.y + 20 * u), ImVec2(sc.x + 18 * u, b.y - 20 * u))) LSetFav(r.name, r.port, !fav);
                ImGui::PopID();
            }
            cl->AddLine(ImVec2(X(786), a.y + 26 * u), ImVec2(X(786), b.y - 26 * u), kNLine, 1.1f * u);
            char pl[32]; if(r.maxp > 0) snprintf(pl, sizeof(pl), "%d / %d", r.players < 0 ? 0 : r.players, r.maxp); else MP_CopyStr(pl, "-", sizeof(pl));
            ImFont* bf = UI::Font();
            const ImVec2 ps = bf->CalcTextSizeA(14.5f * u, FLT_MAX, 0, pl);
            cl->AddText(bf, 14.5f * u, ImVec2(X(869) - ps.x, a.y + rh * 0.5f - ps.y * 0.5f), kNText, pl);
            cl->AddText(bf, 14.5f * u, ImVec2(X(869) - ps.x + 0.8f * u, a.y + rh * 0.5f - ps.y * 0.5f), kNText, pl);
            char pg[24]; if(r.ping >= 0) snprintf(pg, sizeof(pg), "%d MS", r.ping); else MP_CopyStr(pg, "- MS", sizeof(pg));
            const ImVec2 gs = bf->CalcTextSizeA(14.5f * u, FLT_MAX, 0, pg);
            const ImU32 pc = r.ping < 0 ? kNGrey : (r.ping < 90 ? kNBlue : (r.ping < 180 ? IM_COL32(240, 200, 110, 255) : IM_COL32(240, 120, 110, 255)));
            cl->AddText(bf, 14.5f * u, ImVec2(X(938) - gs.x, a.y + rh * 0.5f - gs.y * 0.5f), pc, pg);
            cl->AddText(bf, 14.5f * u, ImVec2(X(938) - gs.x + 0.8f * u, a.y + rh * 0.5f - gs.y * 0.5f), pc, pg);
            if(tap && !s_bNeonListOpen && !s_bNeonFilterOpen)
            {
                const uint64_t now = MP_NowMs();
                if(on && now - s_nNeonTapAt < 600) NeonJoinRow(r);
                NeonRowKey(r, s_szNeonSelKey, sizeof(s_szNeonSelKey)); s_nNeonTapAt = now;
            }
            ImGui::SetCursorScreenPos(ImVec2(la.x, b.y + gap));
        }
        ImGui::Dummy(ImVec2(1, 10 * u));
        LDragScroll();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(3); ImGui::PopStyleVar();

    // the selected server
    const ImVec2 pa(X(997), 192 * u), pb(X(1430), 712 * u);
    dl->AddRectFilled(pa, pb, IM_COL32(13, 14, 17, 242), 7 * u);
    dl->AddRect(pa, pb, kNLine, 7 * u, 0, 1.1f * u);
    if(sel >= 0)
    {
        const LRow& r = rows[sel];
        const float px = pa.x + 24 * u, pw = pb.x - pa.x - 48 * u;
        NeonRowIcon(dl, r, ImVec2(px, pa.y + 18 * u), 66 * u);
        NeonText(dl, uf, 10.5f * u, ImVec2(px + 81 * u, pa.y + 30 * u), kNGold, "SELECTED SERVER", 1.4f * u);
        char nm[64]; NeonUpper(nm, sizeof(nm), r.name);
        float ns = 21 * u; while(ns > 11 * u && NeonTextW(cf, ns, nm, 3.0f * u) > pw - 81 * u) ns -= 1 * u;
        NeonText(dl, cf, ns, ImVec2(px + 82 * u, pa.y + 52 * u), kNText, nm, 3.0f * u, true);
        // description (wrapped, 4 lines)
        char dsc[MP_DESC_LEN + 80];
        if(r.desc[0]) MP_CopyStr(dsc, r.desc, sizeof(dsc));
        else snprintf(dsc, sizeof(dsc), "%s on MP. %s%s Tap JOIN SERVER to play.", r.mode[0] ? r.mode : "Freeroam", r.lang[0] ? "Language: " : "", r.lang[0] ? r.lang : "");
        dl->PushClipRect(ImVec2(px, pa.y + 100 * u), ImVec2(pb.x - 20 * u, pa.y + 196 * u), true);
        dl->AddText(uf, 14.5f * u, ImVec2(px, pa.y + 104 * u), IM_COL32(205, 208, 214, 255), dsc, nullptr, pw);
        dl->PopClipRect();
        // server link
        const ImVec2 sa(px, pa.y + 206 * u), sb(pb.x - 24 * u, pa.y + 259 * u);
        dl->AddRectFilled(sa, sb, IM_COL32(9, 10, 12, 255), 4 * u); dl->AddRect(sa, sb, kNLine, 4 * u, 0, 1.1f * u);
        NeonText(dl, uf, 9.5f * u, ImVec2(sa.x + 11 * u, sa.y + 9 * u), kNGrey, "SERVER LINK", 1.4f * u);
        char link[128];
        if(r.ip[0]) snprintf(link, sizeof(link), "mp://%s:%d", r.ip, r.port); else snprintf(link, sizeof(link), "mp://%s", r.name);
        NeonText(dl, uf, 12 * u, ImVec2(sa.x + 11 * u, sa.y + 27 * u), kNText, link);
        {
            const ImVec2 ca(sb.x - 106 * u, sa.y + 10 * u), cb(sb.x - 10 * u, sb.y - 10 * u);
            dl->AddRect(ca, cb, kNGoldDim, 3 * u, 0, 1.2f * u);
            dl->AddRect(ImVec2(ca.x + 9 * u, ca.y + 9 * u), ImVec2(ca.x + 17 * u, ca.y + 19 * u), kNGold, 1 * u, 0, 1.1f * u);
            dl->AddRect(ImVec2(ca.x + 12 * u, ca.y + 6 * u), ImVec2(ca.x + 20 * u, ca.y + 16 * u), kNGold, 1 * u, 0, 1.1f * u);
            const bool copied = MP_NowMs() - s_nNeonCopiedAt < 1500;
            NeonText(dl, uf, 9.5f * u, ImVec2(ca.x + 28 * u, ca.y + 9 * u), kNGold, copied ? "COPIED" : "COPY LINK", 1.0f * u);
            if(NeonHit("##nbCopy", ca, cb)) { ImGui::SetClipboardText(link); s_nNeonCopiedAt = MP_NowMs(); }
        }
        // players / ping / mode boxes
        const float bw = (sb.x - sa.x - 2 * 8 * u) / 3.0f, by0 = pa.y + 275 * u, by1 = pa.y + 331 * u;
        char v0[32], v1[24], v2[MP_MODE_LEN + 4];
        if(r.maxp > 0) snprintf(v0, sizeof(v0), "%d / %d", r.players < 0 ? 0 : r.players, r.maxp); else MP_CopyStr(v0, "-", sizeof(v0));
        if(r.ping >= 0) snprintf(v1, sizeof(v1), "%d MS", r.ping); else MP_CopyStr(v1, "-", sizeof(v1));
        NeonUpper(v2, sizeof(v2), r.mode[0] ? r.mode : "Freeroam");
        const char* hl[3] = { "PLAYERS", "PING", "MODE" }; const char* vl[3] = { v0, v1, v2 };
        for(int k = 0; k < 3; ++k)
        {
            const ImVec2 ba(sa.x + k * (bw + 8 * u), by0), bb(ba.x + bw, by1);
            dl->AddRectFilled(ba, bb, IM_COL32(9, 10, 12, 255), 4 * u); dl->AddRect(ba, bb, kNLine, 4 * u, 0, 1.1f * u);
            NeonText(dl, uf, 9.5f * u, ImVec2(ba.x + 10 * u, ba.y + 9 * u), kNGrey, hl[k], 1.4f * u);
            dl->PushClipRect(ba, ImVec2(bb.x - 4 * u, bb.y), true);
            NeonText(dl, uf, 13.5f * u, ImVec2(ba.x + 10 * u, ba.y + 27 * u), kNText, vl[k]);
            NeonText(dl, uf, 13.5f * u, ImVec2(ba.x + 10.7f * u, ba.y + 27 * u), kNText, vl[k]);
            dl->PopClipRect();
        }
        // regions & languages
        {
            const float gy = pa.y + 360 * u; const ImVec2 gc(px + 6 * u, gy + 6 * u);
            dl->AddCircle(gc, 6 * u, kNGold, 16, 1.2f * u); dl->AddLine(ImVec2(gc.x - 6 * u, gc.y), ImVec2(gc.x + 6 * u, gc.y), kNGold, 1.0f * u);
            dl->AddLine(ImVec2(gc.x, gc.y - 6 * u), ImVec2(gc.x, gc.y + 6 * u), kNGold, 1.0f * u);
            NeonText(dl, uf, 10.5f * u, ImVec2(px + 20 * u, gy), IM_COL32(200, 202, 208, 255), "REGIONS & LANGUAGES", 1.4f * u);
            const int ci = LCountryIndex(r.cc[0] ? r.cc : MP_CC_GLOBAL);
            LDrawFlag(dl, ci, ImVec2(px, gy + 20 * u), ImVec2(px + 17 * u, gy + 32 * u));
            char rl[160]; const char* cn = LCountryNameAt(ci);
            static const char* const kReg[4] = { "ASIA", "EUROPE", "NORTH AMERICA", "OCEANIA" };
            char lu[MP_LANG_LEN + 4]; NeonUpper(lu, sizeof(lu), r.lang[0] ? r.lang : "All");
            char cu[64]; NeonUpper(cu, sizeof(cu), cn ? cn : "Global");
            snprintf(rl, sizeof(rl), "/ %s   / %s%s%s", cu, lu, r.region >= 0 && r.region < 4 ? "   / " : "", r.region >= 0 && r.region < 4 ? kReg[r.region] : "");
            dl->PushClipRect(ImVec2(px + 22 * u, gy + 16 * u), ImVec2(pb.x - 16 * u, gy + 60 * u), true);
            NeonText(dl, uf, 10.5f * u, ImVec2(px + 24 * u, gy + 21 * u), IM_COL32(165, 168, 175, 255), rl, 0.6f * u);
            std::vector<std::string> pls; LDetailPlayers(r, pls);
            if(!pls.empty())
            {
                std::string s = "PLAYING: "; for(size_t k = 0; k < pls.size() && k < 8; ++k) { if(k) s += ", "; s += pls[k]; }
                NeonText(dl, uf, 10.5f * u, ImVec2(px, gy + 40 * u), IM_COL32(140, 143, 150, 255), s.c_str());
            }
            dl->PopClipRect();
        }
        {   // JOIN SERVER
            const ImVec2 ja(pa.x + 18 * u, pb.y - 79 * u), jb(pb.x - 17 * u, pb.y - 17 * u);
            const bool connecting = client.GetState() != NET_DISCONNECTED;
            ImGui::SetCursorScreenPos(ja);
            const bool tap = ImGui::InvisibleButton("##nbJoin", ImVec2(jb.x - ja.x, jb.y - ja.y));
            const bool press = ImGui::IsItemActive();
            dl->AddRectFilledMultiColor(ja, jb, IM_COL32(40, 38, 32, 255), IM_COL32(20, 20, 22, 255), IM_COL32(16, 16, 18, 255), IM_COL32(34, 32, 28, 255));
            if(press) dl->AddRectFilled(ja, jb, IM_COL32(255, 230, 160, 30));
            dl->AddRect(ja, jb, kNGoldDim, 4 * u, 0, 1.3f * u);
            dl->AddRectFilled(ImVec2(ja.x, ja.y), ImVec2(ja.x + 3 * u, jb.y), kNGold);
            const float jy = (ja.y + jb.y) * 0.5f;
            NeonTri(dl, ImVec2(ja.x + 46 * u, jy), 5 * u, IM_COL32(220, 220, 224, 255));
            const char* jt = connecting ? "CONNECTING" : "JOIN SERVER";
            float js = 27 * u; while(js > 14 * u && NeonTextW(cf, js, jt, 6 * u) > jb.x - ja.x - 130 * u) js -= 1 * u;
            NeonText(dl, cf, js, ImVec2(ja.x + 70 * u, jy - js * 0.62f), kNText, jt, 6 * u, true);
            NeonChevron(dl, ImVec2(jb.x - 26 * u, jy), 5 * u, IM_COL32(200, 200, 204, 255), 1.5f * u);
            if(tap && !connecting) NeonJoinRow(r);
        }
    }
    // hints
    {
        const float hy = 734 * u;
        float hx = X(1430);
        const char* h2 = "JOIN SERVER"; hx -= NeonTextW(uf, 10.5f * u, h2, 1.6f * u);
        NeonText(dl, uf, 10.5f * u, ImVec2(hx, hy), kNGrey, h2, 1.6f * u);
        dl->AddLine(ImVec2(hx - 17 * u, hy + 3 * u), ImVec2(hx - 17 * u, hy + 9 * u), kNGrey, 1.2f * u);
        dl->AddLine(ImVec2(hx - 17 * u, hy + 9 * u), ImVec2(hx - 25 * u, hy + 9 * u), kNGrey, 1.2f * u);
        NeonTri(dl, ImVec2(hx - 25 * u, hy + 9 * u), 2.5f * u, kNGrey, 3);
        hx -= 50 * u; const char* h1 = "NAVIGATE"; hx -= NeonTextW(uf, 10.5f * u, h1, 1.6f * u);
        NeonText(dl, uf, 10.5f * u, ImVec2(hx, hy), kNGrey, h1, 1.6f * u);
        NeonTri(dl, ImVec2(hx - 13 * u, hy + 8 * u), 3 * u, kNGrey, 1); NeonTri(dl, ImVec2(hx - 24 * u, hy + 5 * u), 3 * u, kNGrey, 2);
    }
    // dropdowns (on top)
    if(s_bNeonListOpen)
    {
        const ImVec2 a(X(66), tb.y + 4 * u), b(X(300), tb.y + 4 * u + 4 * 40 * u);
        dl->AddRectFilled(a, b, IM_COL32(12, 13, 16, 252), 5 * u); dl->AddRect(a, b, kNLine, 5 * u, 0, 1.1f * u);
        for(int k = 0; k < 4; ++k)
        {
            const ImVec2 ia(a.x, a.y + k * 40 * u), ib(b.x, ia.y + 40 * u);
            if(k == s_nNeonList) dl->AddRectFilled(ImVec2(ia.x + 2 * u, ia.y + 2 * u), ImVec2(ib.x - 2 * u, ib.y - 2 * u), IM_COL32(40, 36, 26, 255), 4 * u);
            NeonText(dl, uf, 11 * u, ImVec2(ia.x + 16 * u, ia.y + 13 * u), k == s_nNeonList ? kNGold : kNText, kLists[k], 2.6f * u);
            char id[16]; snprintf(id, sizeof(id), "##nbL%d", k);
            if(NeonHit(id, ia, ib)) { s_nNeonList = k; s_bNeonListOpen = false; builtAt = 0; }
        }
    }
    if(s_bNeonFilterOpen)
    {
        const ImVec2 a(X(1190), tb.y + 4 * u), b(X(1430), tb.y + 4 * u + 3 * 40 * u);
        dl->AddRectFilled(a, b, IM_COL32(12, 13, 16, 252), 5 * u); dl->AddRect(a, b, kNLine, 5 * u, 0, 1.1f * u);
        bool* vs[3] = { &s_bNeonHideFull, &s_bNeonHideEmpty, &s_bNeonHideLocked };
        static const char* const kF[3] = { "HIDE FULL", "HIDE EMPTY", "HIDE LOCKED" };
        for(int k = 0; k < 3; ++k)
        {
            const ImVec2 ia(a.x, a.y + k * 40 * u), ib(b.x, ia.y + 40 * u);
            const ImVec2 ca(ia.x + 14 * u, ia.y + 12 * u), cb(ca.x + 16 * u, ca.y + 16 * u);
            dl->AddRect(ca, cb, kNGoldDim, 2 * u, 0, 1.2f * u);
            if(*vs[k]) dl->AddRectFilled(ImVec2(ca.x + 4 * u, ca.y + 4 * u), ImVec2(cb.x - 4 * u, cb.y - 4 * u), kNGold, 1 * u);
            NeonText(dl, uf, 11 * u, ImVec2(ia.x + 42 * u, ia.y + 13 * u), kNText, kF[k], 2.2f * u);
            char id[16]; snprintf(id, sizeof(id), "##nbF%d", k);
            if(NeonHit(id, ia, ib)) { *vs[k] = !*vs[k]; builtAt = 0; }
        }
    }
}

// ---- settings / about / quit -----------------------------------------------------------------
static void NeonSettings(ImDrawList* dl, ImVec2 ds)
{
    const float u = ds.y / 804.0f;
    ImFont* uf = UI::Font(); ImFont* pd = UI::PricedownFont() ? UI::PricedownFont() : UI::GothicFont();
    NeonText(dl, uf, 10.5f * u, ImVec2(ds.x * 0.04f, 30 * u), kNGrey, "MP NEON NETWORK", 4.2f * u);
    NeonText(dl, pd, 60 * u, ImVec2(ds.x * 0.04f, 42 * u), IM_COL32(190, 192, 196, 255), "settings", 0, true);
    {
        const ImVec2 a(ds.x - 260 * u, 30 * u), b(ds.x - 40 * u, 70 * u);
        dl->AddRectFilled(a, b, IM_COL32(10, 10, 12, 240), 5 * u); dl->AddRect(a, b, IM_COL32(205, 182, 120, 255), 5 * u, 0, 1.6f * u);
        const float cy = (a.y + b.y) * 0.5f;
        dl->AddLine(ImVec2(a.x + 18 * u, cy), ImVec2(a.x + 31 * u, cy), kNText, 1.6f * u);
        dl->AddLine(ImVec2(a.x + 18 * u, cy), ImVec2(a.x + 24 * u, cy - 6 * u), kNText, 1.6f * u);
        dl->AddLine(ImVec2(a.x + 18 * u, cy), ImVec2(a.x + 24 * u, cy + 6 * u), kNText, 1.6f * u);
        NeonText(dl, uf, 10.5f * u, ImVec2(a.x + 44 * u, cy - 6.5f * u), kNText, "BACK TO MAIN MENU", 2.6f * u);
        if(NeonHit("##nsBack", a, b)) s_nNeonPage = NP_MAIN;
    }
    const int n = FloatMenu::SectionCount();
    if(s_nNeonSetSec < 0 || s_nNeonSetSec >= n)
    {   // Display first (the GTA HUD settings live there)
        s_nNeonSetSec = 0; for(int i = 0; i < n; ++i) if(!strcasecmp(FloatMenu::SectionTitle(i), "Display")) s_nNeonSetSec = i;
    }
    const float lx = ds.x * 0.04f, ly = 125 * u, lw = 250 * u, ih = 46 * u;
    for(int i = 0; i <= n; ++i)
    {
        const ImVec2 a(lx, ly + i * (ih + 4 * u)), b(lx + lw, a.y + ih);
        const bool on = i == s_nNeonSetSec;
        dl->AddRectFilled(a, b, on ? IM_COL32(30, 27, 20, 245) : IM_COL32(15, 16, 19, 235), 5 * u);
        dl->AddRect(a, b, on ? kNGold : kNLine, 5 * u, 0, 1.2f * u);
        if(on) dl->AddRectFilled(ImVec2(a.x + 1 * u, a.y + 7 * u), ImVec2(a.x + 4 * u, b.y - 7 * u), kNGold, 2 * u);
        char t[48]; NeonUpper(t, sizeof(t), i < n ? FloatMenu::SectionTitle(i) : "Game settings");
        NeonText(dl, uf, 12 * u, ImVec2(a.x + 18 * u, a.y + 15 * u), on ? kNGold : kNText, t, 2.2f * u);
        char id[16]; snprintf(id, sizeof(id), "##nsS%d", i);
        if(NeonHit(id, a, b))
        {
            if(i < n) s_nNeonSetSec = i;
            else { s_bNeonHidden = true; logger->Info("Neon menu: the game's own menu (Game settings)"); }
        }
    }
    const ImVec2 pa(lx + lw + 20 * u, ly), pb(ds.x - 40 * u, ds.y - 40 * u);
    dl->AddRectFilled(pa, pb, IM_COL32(12, 13, 16, 240), 7 * u); dl->AddRect(pa, pb, kNLine, 7 * u, 0, 1.1f * u);
    ImGui::SetCursorScreenPos(ImVec2(pa.x + 16 * u, pa.y + 14 * u));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    if(ImGui::BeginChild("##neonSet", ImVec2(pb.x - pa.x - 32 * u, pb.y - pa.y - 28 * u), false, ImGuiWindowFlags_NoBackground))
    {
        VPushWidgetStyle();
        FloatMenu::DrawSection(s_nNeonSetSec);
        VPopWidgetStyle();
        LDragScroll();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}
static void NeonDialog(ImDrawList* dl, ImVec2 ds, const char* title, const char* body, const char* yes, const char* no, bool* outYes, bool* outNo)
{
    const float u = ds.y / 804.0f;
    dl->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(0, 0, 0, 150));
    const float w = 640 * u, h = 300 * u; const ImVec2 a((ds.x - w) * 0.5f, (ds.y - h) * 0.5f), b(a.x + w, a.y + h);
    dl->AddRectFilled(a, b, IM_COL32(13, 14, 17, 250), 7 * u); dl->AddRect(a, b, kNGoldDim, 7 * u, 0, 1.4f * u);
    dl->AddRectFilled(ImVec2(a.x, a.y), ImVec2(a.x + 3 * u, b.y), kNGold);
    NeonText(dl, UI::GothicFont(), 40 * u, ImVec2(a.x + 30 * u, a.y + 20 * u), kNText, title, 0, true);
    dl->AddText(UI::Font(), 15 * u, ImVec2(a.x + 30 * u, a.y + 88 * u), IM_COL32(200, 203, 210, 255), body, nullptr, w - 60 * u);
    const float bw = (w - 60 * u - 16 * u) * (no ? 0.5f : 1.0f);
    for(int k = 0; k < (no ? 2 : 1); ++k)
    {
        const ImVec2 ba(a.x + 30 * u + k * (bw + 16 * u), b.y - 74 * u), bb(ba.x + bw, b.y - 24 * u);
        dl->AddRectFilled(ba, bb, IM_COL32(24, 23, 20, 255), 4 * u); dl->AddRect(ba, bb, k == 0 ? kNGold : kNLine, 4 * u, 0, 1.3f * u);
        const char* t = k == 0 ? yes : no;
        const float tw = NeonTextW(UI::Font(), 12 * u, t, 2.6f * u);
        NeonText(dl, UI::Font(), 12 * u, ImVec2((ba.x + bb.x - tw) * 0.5f, (ba.y + bb.y) * 0.5f - 7.5f * u), kNText, t, 2.6f * u);
        char id[16]; snprintf(id, sizeof(id), "##ndB%d", k);
        if(NeonHit(id, ba, bb)) { if(k == 0) *outYes = true; else *outNo = true; }
    }
}

// ---- the main menu -----------------------------------------------------------------------------
static bool NeonTitleCtx() { return Game::IsMainMenuShown() && Game::LocalPed() <= 0 && !s_bMenuMp.load(); }
// 4.13.23: the game draws its title menu (MobileMenu::Render) after MP's HUD, so in 4.13.22 its full-screen
// background covered the neon menu and only the game's menu showed. While the neon menu is up the game's menu
// is not drawn (main.cpp MobileMenu_Render) and gets no touches.
bool MP::NeonCoversGameMenu()
{
    if(!NeonOn()) return false;   // 4.13.25: ServerBrowserMode = mp / default: nothing is read
    if(!NeonPaint::Ready()) return false;      // 4.13.28: the native painter (PNG textures) is the only title drawing
    const bool on = true, menu = Game::IsMainMenuShown(), ped = Game::LocalPed() > 0, mp = s_bMenuMp.load();
    const bool r = on && !s_bNeonHidden && menu && !ped && !mp;
    if(r) s_nNeonUpMs = MP_NowMs();
    static int last = -1; const int st = (on << 0) | (s_bNeonHidden << 1) | (menu << 2) | (ped << 3) | (mp << 4) | (r << 5);
    if(st != last)
    {   // the MP log says why the neon menu is (not) up
        last = st;
        logger->Info("Neon menu: %s (mode %d, tile %d, api %d, game settings %d, game menu %d, game loaded %d, MP load %d)",
            r ? "on screen" : "not shown", s_nNeonMode, (int)s_bMenuMpOn, (int)s_bMenuMpViaApi, (int)s_bNeonHidden, (int)menu, (int)ped, (int)mp);
    }
    return r;
}
static void NeonResume()
{
    if(!NeonTitleCtx()) { s_bMenuMpExit = true; return; }   // the black screen: the game is loaded - back to it
    const int slot = Game::NewestSaveSlot();
    const bool ok = slot >= 0 ? Game::RequestLoadSlot(slot) : Game::RequestNewGame();
    logger->Info("Neon menu: Resume -> %s %d (%s)", slot >= 0 ? "load slot" : "new game", slot + 1, ok ? "ok" : "failed");
}
static void NeonNewGame()
{
    if(!NeonTitleCtx()) s_bMenuMpExit = true;
    const bool ok = Game::RequestNewGame();
    logger->Info("Neon menu: New game (%s)", ok ? "ok" : "failed");
}
static void NeonJoinRow(const LRow& r)
{
    s_bNeonListOpen = s_bNeonFilterOpen = false;
    if(NeonTitleCtx())
    {   // no game yet: the Multiplayer load first (newest save / new game), the join runs when it is done
        s_NeonPending = r; s_bNeonPendingJoin = true;
        logger->Info("Neon menu: join %s after the Multiplayer load", r.name);
        MenuMpTap(nullptr);
        return;
    }
    s_bNeonPendingJoin = false;
    logger->Info("Neon menu: join %s", r.name);
    LJoin(r);
}
static bool NeonBestRow(LRow& out)
{   // Quick connect with no address: the featured server with the most players (the default server when all are empty)
    std::vector<LRow> rows; const int keep = s_nNeonList; char q[64]; MP_CopyStr(q, s_szNeonSearch, sizeof(q));
    s_nNeonList = 0; s_szNeonSearch[0] = 0; NeonBuild(rows); s_nNeonList = keep; MP_CopyStr(s_szNeonSearch, q, sizeof(s_szNeonSearch));
    int best = -1;
    for(int i = 0; i < (int)rows.size(); ++i)
    {
        const LRow& r = rows[i]; if(r.locked || (r.maxp > 0 && r.players >= r.maxp)) continue;
        if(best < 0 || (NeonFeatured(r) && !NeonFeatured(rows[best])) || (NeonFeatured(r) == NeonFeatured(rows[best]) && r.players > rows[best].players)) best = i;
    }
    if(best < 0) return false;
    out = rows[best]; return true;
}
// 4.13.26: the server list, the lobby and the LAN search only run after a game is loaded (MP's network starts
// with the first game frame). On the title they are not touched: Browse servers / Quick connect / Play now
// start the Multiplayer load first and the browser opens on the black screen when the world is up.
static void NeonBrowseFromTitle()
{
    s_nNeonPage = NP_BROWSER;
    logger->Info("Neon menu: Browse servers from the title -> the Multiplayer load first, the browser opens after it");
    MenuMpTap(nullptr);
}
static void OnNeonQuick(const char* t, void*)
{
    if(t && *t && NeonJoinAddress(t)) return;
    if(t && *t) { MP_CopyStr(s_szNeonSearch, t, sizeof(s_szNeonSearch)); if(NeonTitleCtx()) NeonBrowseFromTitle(); else s_nNeonPage = NP_BROWSER; return; }   // a name: search it
    if(NeonTitleCtx()) { NeonBrowseFromTitle(); return; }
    LRow r; if(NeonBestRow(r)) NeonJoinRow(r); else s_nNeonPage = NP_BROWSER;
}
static const char* NeonLangName()
{
    static const struct { const char* c; const char* n; } k[] = { { "en", "ENGLISH" }, { "es", "ESPAÑOL" }, { "pt", "PORTUGUÊS" }, { "fr", "FRANÇAIS" }, { "de", "DEUTSCH" },
        { "ru", "РУССКИЙ" }, { "tr", "TÜRKÇE" }, { "id", "INDONESIA" }, { "fil", "FILIPINO" }, { "tl", "FILIPINO" }, { "vi", "TIẾNG VIỆT" }, { "it", "ITALIANO" }, { "pl", "POLSKI" } };
    for(auto& e : k) if(!strcasecmp(settings.uiLanguage, e.c)) return e.n;
    static char up[16]; NeonUpper(up, sizeof(up), settings.uiLanguage[0] ? settings.uiLanguage : "en"); return up;
}
static void NeonMain(ImDrawList* dl, ImVec2 ds)
{
    const float u = ds.y / 804.0f;
    const float L = ImMin(ds.x * 0.07f, 100 * u * (ds.x / ds.y) / (1430.0f / 804.0f));
    ImFont* uf = UI::Font(); ImFont* gf = UI::GothicFont(); ImFont* cf = UI::CapsFont();
    UI::TitleStep("neon logo");
    {   // the logo (title.png)
        ImVec2 tuv(1, 1);
        if(void* t = HUD::Texture("title.png", &tuv))
        {
            const float w = 490 * u, h = w * (601.0f / 1920.0f);
            dl->AddImage((ImTextureID)t, ImVec2(L, 52 * u), ImVec2(L + w, 52 * u + h), ImVec2(0, 0), tuv);
        }
    }
    UI::TitleStep("neon items");
    static const char* const kItems[] = { "Resume", "New game", "Browse servers", "Quick connect", "Settings", "About", "Quit game" };
    const int ni = 7; const float fs = 38 * u, step = 43 * u, y0 = 205 * u, ix = L + 24 * u;
    for(int i = 0; i < ni; ++i)
    {
        const float y = y0 + i * step;
        const float w = gf->CalcTextSizeA(fs, FLT_MAX, 0, kItems[i]).x;
        ImGui::SetCursorScreenPos(ImVec2(ix - 30 * u, y));
        char id[16]; snprintf(id, sizeof(id), "##nm%d", i);
        const bool tap = ImGui::InvisibleButton(id, ImVec2(w + 60 * u, step));
        const bool press = ImGui::IsItemActive();
        const bool on = i == s_nNeonSel || press;
        if(on) NeonTri(dl, ImVec2(ix - 14 * u, y + step * 0.5f), 3.6f * u, IM_COL32(126, 178, 226, 255));
        // the silver blackletter with a dark edge (like the reference)
        const ImVec2 p(ix, y + (step - fs) * 0.5f - 2 * u);
        for(int k = 0; k < 4; ++k) dl->AddText(gf, fs, ImVec2(p.x + ((k & 1) ? 1.4f : -1.4f) * u, p.y + ((k & 2) ? 1.4f : -1.4f) * u), IM_COL32(10, 12, 18, 230), kItems[i]);
        dl->AddText(gf, fs, ImVec2(p.x + 2 * u, p.y + 3 * u), IM_COL32(0, 0, 0, 160), kItems[i]);
        const int vb = dl->VtxBuffer.Size;
        dl->AddText(gf, fs, p, IM_COL32_WHITE, kItems[i]);
        {   // vertical silver gradient on the glyphs
            const ImU32 top = on ? IM_COL32(240, 243, 250, 255) : IM_COL32(196, 204, 220, 255), bot = on ? IM_COL32(170, 180, 205, 255) : IM_COL32(112, 122, 145, 255);
            for(int v = vb; v < dl->VtxBuffer.Size; ++v)
            {
                ImDrawVert& vt = dl->VtxBuffer[v]; float t = (vt.pos.y - p.y) / fs; t = t < 0 ? 0 : (t > 1 ? 1 : t);
                const ImVec4 a = ImGui::ColorConvertU32ToFloat4(top), b = ImGui::ColorConvertU32ToFloat4(bot);
                vt.col = ImGui::ColorConvertFloat4ToU32(ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, 1.0f));
            }
        }
        if(tap)
        {
            s_nNeonSel = i;
            switch(i)
            {
                case 0: NeonResume(); break;
                case 1: NeonNewGame(); break;
                case 2: if(NeonTitleCtx()) NeonBrowseFromTitle(); else s_nNeonPage = NP_BROWSER; break;
                case 3: VKeyboard::Open("Quick connect: ip:port (empty = the best featured server)", "", 63, OnNeonQuick); break;
                case 4: s_nNeonPage = NP_SETTINGS; break;
                case 5: s_nNeonPage = NP_ABOUT; break;
                case 6: s_nNeonPage = NP_QUIT; break;
            }
        }
    }
    {   // tagline
        const char* tg = "FIND A WORLD AND JOIN THE STREETS OF SAN\nANDREAS.";
        const float ty = y0 + ni * step + 22 * u;
        const char* nl = strchr(tg, '\n');
        std::string l1(tg, nl - tg);
        NeonText(dl, uf, 12.5f * u, ImVec2(ix + 10 * u, ty), IM_COL32(205, 208, 214, 255), l1.c_str(), 1.0f * u);
        NeonText(dl, uf, 12.5f * u, ImVec2(ix + 10 * u, ty + 16 * u), IM_COL32(205, 208, 214, 255), nl + 1, 1.0f * u);
    }
    // 5.01.12: no Neon Identity card here any more (only the "DISCORD CONNECTED" pop-up, see the painter path)
    UI::TitleStep("neon featured");
    {   // NEON FEATURED (bottom left)
        static LRow feat; static bool haveFeat = false; static uint64_t at = 0;
        const bool titleNow = NeonTitleCtx();
        if(titleNow) haveFeat = false;   // 4.13.26: no server list on the title (see NeonBrowseFromTitle)
        else if(MP_NowMs() - at > 3000) { at = MP_NowMs(); haveFeat = NeonBestRow(feat); }
        const ImVec2 a(L - 8 * u, 643 * u), b(L + 379 * u, 723 * u);
        dl->AddRectFilled(a, b, IM_COL32(12, 12, 14, 232), 2 * u);
        if(haveFeat)
        {   // the server's own art, faint, on the right
            ImVec2 uv(1, 1); void* t = LRowBg(feat, &uv);
            if(t) { dl->PushClipRect(ImVec2(a.x + 220 * u, a.y), b, true); LImageCover(dl, t, uv, ImVec2(a.x + 220 * u, a.y), b, IM_COL32(255, 255, 255, 40)); dl->PopClipRect(); }
        }
        dl->AddRect(a, b, IM_COL32(64, 60, 52, 255), 2 * u, 0, 1.0f * u);
        dl->AddRectFilled(ImVec2(a.x, a.y), ImVec2(a.x + 2.5f * u, b.y), IM_COL32(126, 178, 226, 255));
        if(haveFeat)
        {
            NeonRowIcon(dl, feat, ImVec2(a.x + 11 * u, a.y + 13 * u), 54 * u);
            NeonText(dl, cf, 7.5f * u, ImVec2(a.x + 77 * u, a.y + 23 * u), IM_COL32(150, 152, 158, 255), "Neon featured", 1.2f * u);
            char nm[64]; MP_CopyStr(nm, feat.name, sizeof(nm));
            dl->PushClipRect(a, ImVec2(b.x - 6 * u, b.y), true);
            NeonText(dl, cf, 13.5f * u, ImVec2(a.x + 77 * u, a.y + 34 * u), kNText, nm, 1.8f * u, true);
            char d[96]; snprintf(d, sizeof(d), "%s - %d / %d players", feat.mode[0] ? feat.mode : "Freeroam", feat.players < 0 ? 0 : feat.players, feat.maxp > 0 ? feat.maxp : 0);
            NeonText(dl, uf, 9 * u, ImVec2(a.x + 77 * u, a.y + 52 * u), IM_COL32(170, 172, 178, 255), d);
            NeonText(dl, cf, 7.5f * u, ImVec2(a.x + 77 * u, a.y + 64 * u), IM_COL32(200, 202, 208, 255), "Play now", 1.2f * u);
            dl->AddCircleFilled(ImVec2(a.x + 77 * u + NeonTextW(cf, 7.5f * u, "Play now", 1.2f * u) + 7 * u, a.y + 69 * u), 1.2f * u, IM_COL32(200, 202, 208, 255), 6);
            dl->PopClipRect();
            if(NeonHit("##nfPlay", a, b)) NeonJoinRow(feat);
        }
        else if(titleNow)
        {
            NeonText(dl, cf, 7.5f * u, ImVec2(a.x + 20 * u, a.y + 23 * u), IM_COL32(150, 152, 158, 255), "Neon featured", 1.2f * u);
            NeonText(dl, cf, 13.5f * u, ImVec2(a.x + 20 * u, a.y + 34 * u), kNText, "Browse servers", 1.8f * u, true);
            NeonText(dl, cf, 7.5f * u, ImVec2(a.x + 20 * u, a.y + 60 * u), IM_COL32(200, 202, 208, 255), "Play now", 1.2f * u);
            if(NeonHit("##nfPlay", a, b)) NeonBrowseFromTitle();
        }
        else NeonText(dl, cf, 9 * u, ImVec2(a.x + 20 * u, a.y + 34 * u), IM_COL32(150, 152, 158, 255), "Looking for featured servers...", 1.2f * u);
    }
    UI::TitleStep("neon footer");
    {   // version + hints + language
        char v[64]; snprintf(v, sizeof(v), "V%s \xE2\x80\x94 NEON PREVIEW", MP_VERSION_STR);
        NeonText(dl, uf, 9.5f * u, ImVec2(L - 8 * u, 761 * u), IM_COL32(100, 104, 112, 255), v, 2.6f * u);
        const float hy = 761 * u; float hx = ds.x * (998.0f / 1430.0f);
        if(NeonPadUI())
        {
            NeonTri(dl, ImVec2(hx + 2 * u, hy + 4 * u), 2.4f * u, IM_COL32(120, 124, 132, 255), 2);
            NeonTri(dl, ImVec2(hx + 8 * u, hy + 8 * u), 2.4f * u, IM_COL32(120, 124, 132, 255), 1);
        }
        if(NeonPadUI())
        {   // 5.01.12: controller hints only with a controller
            NeonText(dl, uf, 9 * u, ImVec2(hx + 16 * u, hy), IM_COL32(120, 124, 132, 255), "SELECT", 2.4f * u);
            NeonText(dl, uf, 9 * u, ImVec2(hx + 80 * u, hy), IM_COL32(170, 172, 178, 255), "A", 2.0f * u);
            NeonText(dl, uf, 9 * u, ImVec2(hx + 80 * u + NeonTextW(uf, 9 * u, "A", 2.0f * u) + 7 * u, hy), IM_COL32(120, 124, 132, 255), "CONFIRM", 2.4f * u);
        }
        const char* ln = NeonLangName();
        const float lx = ds.x - 160 * u;
        if(NeonPadUI())
        {
            dl->AddRect(ImVec2(lx, hy - 3 * u), ImVec2(lx + 15 * u, hy + 13 * u), IM_COL32(200, 202, 208, 255), 2 * u, 0, 1.1f * u);
            NeonText(dl, uf, 8 * u, ImVec2(lx + 3 * u, hy - 1 * u), IM_COL32(200, 202, 208, 255), "A");
        }
        NeonText(dl, uf, 9.5f * u, ImVec2(lx + 24 * u, hy), IM_COL32(220, 222, 228, 255), ln, 1.6f * u);
        NeonTri(dl, ImVec2(lx + 32 * u + NeonTextW(uf, 9.5f * u, ln, 1.6f * u), hy + 5 * u), 2.2f * u, IM_COL32(200, 202, 208, 255), 1);
        if(NeonHit("##nLang", ImVec2(lx - 6 * u, hy - 10 * u), ImVec2(ds.x - 10 * u, hy + 22 * u)))
        {
            s_nNeonPage = NP_SETTINGS;
            for(int i = 0; i < FloatMenu::SectionCount(); ++i) if(!strcasecmp(FloatMenu::SectionTitle(i), "Language")) s_nNeonSetSec = i;
        }
    }
}

// HUD, every frame (DrawMenuMpFront): true when the neon menu took the screen
static bool NeonDraw()
{
    if(!NeonOn()) return false;
    const bool title = NeonTitleCtx();
    const bool black = MenuMpBlack() && s_nMenuMpPct.load() < 0 && client.GetState() == NET_DISCONNECTED && !FloatMenu::IsModal() && !JoinScreenUp();
    if(!title && !black) { if(!Game::IsMainMenuShown()) s_bNeonHidden = false; return false; }
    if(title) return false;   // 4.13.28: the title menu is painted natively (MP::NeonPaintMenu), no ImGui there
    if(s_bNeonQuickAfterLoad && black && !VKeyboard::IsOpen())
    {   // Quick connect from the title: the address keyboard opens on the black screen
        s_bNeonQuickAfterLoad = false; s_nNeonPage = NP_MAIN;
        VKeyboard::Open("Quick connect: ip:port (empty = the best featured server)", "", 63, OnNeonQuick);
    }
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    if(s_bNeonHidden && title)
    {   // the game's own menu (Game settings): a small way back to the neon menu
        const float u = ds.y / 804.0f;
        const ImVec2 sz(230 * u, 46 * u), at(ds.x * 0.02f, ds.y - sz.y - 20 * u);
        ImGui::SetNextWindowPos(at); ImGui::SetNextWindowSize(sz);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        if(ImGui::Begin("##NeonBack", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground))
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(at, ImVec2(at.x + sz.x, at.y + sz.y), IM_COL32(10, 10, 12, 230), 5 * u);
            dl->AddRect(at, ImVec2(at.x + sz.x, at.y + sz.y), kNGold, 5 * u, 0, 1.4f * u);
            NeonText(dl, UI::Font(), 11 * u, ImVec2(at.x + 20 * u, at.y + 15 * u), kNText, "NEON MENU", 3.0f * u);
            if(NeonHit("##nbk", at, ImVec2(at.x + sz.x, at.y + sz.y))) s_bNeonHidden = false;
        }
        ImGui::End(); ImGui::PopStyleVar();
        return true;
    }
    if(black) s_bNeonHidden = false;
    ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(ds);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 1));
    if(ImGui::Begin("##NeonMenu", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                    ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus))
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        UI::TitleStep("neon backdrop");
        NeonBackdrop(dl, ds, s_nNeonPage == NP_MAIN || s_nNeonPage >= NP_ABOUT ? 70 : 200);
        if(s_nNeonPage == NP_BROWSER && !title) NeonBrowser(dl, ds);   // 4.13.26: on the title the Multiplayer load runs first
        else if(s_nNeonPage == NP_SETTINGS) NeonSettings(dl, ds);
        else
        {
            UI::TitleStep("neon main");
            NeonMain(dl, ds);
            UI::TitleStep("neon main done");
            if(s_nNeonPage == NP_ABOUT)
            {
                char body[400];
                snprintf(body, sizeof(body), "MP Multiplayer %s for GTA San Andreas (Android, AML).\nby ThirtyFoxMCx00E1 - mpsamp-open.github.io\nDiscord: %s\n\n"
                         "Neon menu art: San Andreas fan art (GTA V style). Fonts: UnifrakturMaguntia, Cinzel (SIL OFL 1.1).", MP_VERSION_STR, MP_AUTH_DISCORD_URL);
                bool y = false, n = false; NeonDialog(dl, ds, "About", body, "CLOSE", nullptr, &y, &n);
                if(y) s_nNeonPage = NP_MAIN;
            }
            else if(s_nNeonPage == NP_QUIT)
            {
                bool y = false, n = false;
                NeonDialog(dl, ds, "Quit game", "Close GTA San Andreas now? Unsaved progress is lost.", "QUIT", "CANCEL", &y, &n);
                if(n) s_nNeonPage = NP_MAIN;
                if(y) { logger->Info("Neon menu: Quit game"); MP::SaveConfig(); exit(0); }
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(); ImGui::PopStyleVar(2);
    return true;
}

// game thread (MenuMpProcess): the black screen is up after a join from the title menu
static bool NeonPendingJoin()
{
    if(!s_bNeonPendingJoin) return false;
    s_bNeonPendingJoin = false;
    logger->Info("Neon menu: the Multiplayer load is done -> join %s", s_NeonPending.name);
    LJoin(s_NeonPending);
    return true;
}

// ================================================================================================
// 4.13.28: the title menu is drawn like PCMenu - at the end of the game's own menu pass, with the game's sprite
// drawing (MobileMenu::DrawSprite) and PNG textures from files/TYFMC/MP/neon/ (tools/gen_neon_assets.py). No ImGui
// runs on the title screen any more (ServerBrowserMode = neon AND mp): the game quit by itself there in 4.13.22-27.
// The taps only queue an action; the action runs on the game thread (MobileMenu::Update -> MP::NeonMenuTick).
// ================================================================================================
enum { NA_NONE = 0, NA_RESUME, NA_NEWGAME, NA_BROWSE, NA_QUICK, NA_SETTINGS, NA_QUIT, NA_SIGNOUT, NA_MPTILE, NA_DISCORD };
static std::atomic<int> s_nNeonAct{NA_NONE};
static int  s_nNeonDlg = 0;              // 1 About, 2 Quit
static int  s_nNeonPaintFrames = 0;
static void (*s_fnOnSettings)() = nullptr;
static bool s_bOnSettingsLooked = false;

static NeonPaint::Col NC(int r, int g, int b, int a = 255) { return { (uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a }; }
static bool NeonPaintLoad()
{
    if(NeonPaint::Ready()) return true;
    if(NeonPaint::Failed())
    {   // 5.01.08: tried again every 5 s (3 times), e.g. while the game's file layer was not ready yet
        static uint64_t s_at = 0; static int s_n = 0;
        const uint64_t now = MP_NowMs();
        if(s_n >= 3 || now - s_at < 5000) return false;
        s_at = now; ++s_n; NeonPaint::Retry();
    }
    return NeonPaint::Load(MP::DataDir());
}
// what the game's menu pass gets painted with: 0 nothing, 1 the neon title menu, 2 the [Multiplayer] tile (mp), 3 the NEON MENU pill, 4 the loading cover (5.01.14)
static const char* s_szNeonWhy = "the title menu has not been drawn yet";   // 5.01.08: why it is (not) painted (neon_mode_status.txt)
static int s_nNeonWantCalls = 0;
static void NeonUnhide() { s_bNeonHidden = false; }
int MP::NeonPaintWanted()
{
    ++s_nNeonWantCalls;
    if(s_nNeonMode == NEON_MODE_DEFAULT) { s_szNeonWhy = "mode is default (no neon_mode.txt, ServerBrowserMode = default)"; return 0; }   // 4.13.30
    if(HUD::TitleMenuBroken()) { s_szNeonWhy = "the title menu crashed (caught) - see mp_log.txt \"CRASHED\""; return 0; }   // 4.13.35
    // 5.01.14: a server was joined (Browse servers / Quick connect / the API): the game's own title menu showed for a
    // few seconds before the loading screen (and its pause menu after the load) because the neon menu stood aside as
    // soon as the Multiplayer load began. Now a neon loading cover stays over the game's menus until the black screen.
    if(s_bMenuMp.load() && NeonOn() && !s_bMenuMpLoaded.load() && s_nMenuMpTapAt && MP_NowMs() - s_nMenuMpTapAt < 30000 && Game::IsFrontendMenu())
    {
        if(NeonPaint::Inited() && NeonPaintLoad()) { s_szNeonWhy = "painted (the Multiplayer loading cover)"; return 4; }
    }
    if(!Game::IsMainMenuShown()) { s_szNeonWhy = "the game's main menu is not on screen"; return 0; }
    if(Game::LocalPed() > 0) { s_szNeonWhy = "in game (not the title screen)"; return 0; }
    if(s_bMenuMp.load()) { s_szNeonWhy = "the MP server list is open"; return 0; }
    if(NeonOn())
    {
        if(!NeonPaint::Inited()) { s_szNeonWhy = "this game build lacks the drawing functions MP uses"; return 0; }
        if(!NeonPaintLoad()) { s_szNeonWhy = "files/TYFMC/MP/neon/ textures could not be read"; return 0; }
        s_szNeonWhy = s_bNeonHidden ? "painted (the NEON MENU pill - Game settings was tapped)" : "painted";
        return s_bNeonHidden ? 3 : 1;
    }
    // 4.13.35: ServerBrowserMode = mp is the 4.13.20 [Multiplayer] button again (ImGui, MP::DrawMenuMpFront):
    // no MobileMenu::Render hook, no native painting
    return 0;
}
bool MP::NeonNativeTitle() { return NeonPaint::Ready() && NeonPaintWanted() != 0; }

static void NeonPText(int f, const char* s, float x, float y, float px, NeonPaint::Col c, float tr = 0, bool shadow = false)
{
    if(shadow) NeonPaint::Text(f, s, x + px * 0.06f, y + px * 0.08f, px, NC(0, 0, 0, 170), tr);
    NeonPaint::Text(f, s, x, y, px, c, tr);
}
static void NeonPFrame(float x0, float y0, float x1, float y1, float t, NeonPaint::Col c)
{
    NeonPaint::Quad(x0, y0, x1, y0 + t, c); NeonPaint::Quad(x0, y1 - t, x1, y1, c);
    NeonPaint::Quad(x0, y0, x0 + t, y1, c); NeonPaint::Quad(x1 - t, y0, x1, y1, c);
}
// 5.01.14: the MP launcher look (mp_launcher.inc kV* colours): slate glass panels, thin light lines, white text,
// blue accent, green CONNECT / red QUIT, square corners, Fredoka; the backdrop is the MP LAN Server preview picture
static NeonPaint::Col LP_TEXT() { return NC(226, 229, 233); }
static NeonPaint::Col LP_DIM()  { return NC(150, 157, 166); }
static NeonPaint::Col LP_LINE() { return NC(255, 255, 255, 30); }
static NeonPaint::Col LP_BLUE(int a = 255) { return NC(126, 178, 226, a); }
static NeonPaint::Col LP_GREEN(){ return NC(76, 160, 48); }
static NeonPaint::Col LP_RED()  { return NC(222, 74, 60); }
static void NeonPGlass(float x0, float y0, float x1, float y1)
{   // VGlass: see-through slate, lighter at the top
    using namespace NeonPaint;
    if(const Tex* w = T("white.png")) ImgUV(w, x0, y0, x1, y1, 0, 0, 1, 1, NC(58, 66, 76, 150), NC(18, 21, 26, 205));
    else Quad(x0, y0, x1, y1, NC(30, 34, 40, 190));
}
static void NeonPBackdrop(float W, float H)
{
    using namespace NeonPaint;
    Quad(0, 0, W, H, NC(8, 10, 14));
    if(const Tex* b = T("lp_bg.png"))
    {   // cover the screen (the picture is 2:1)
        float w = W, h = W * 0.5f;
        if(h < H) { h = H; w = H * 2.0f; }
        const float x0 = (W - w) * 0.5f, y0 = (H - h) * 0.5f;
        Img(b, x0, y0, x0 + w, y0 + h);
    }
    else if(const Tex* a = T("art.png"))
    {
        const float iw = H * (1131.0f / 707.0f), x0 = fmaxf(W - iw, W * 0.30f);
        Img(a, x0, 0, x0 + iw, H);
    }
    Quad(0, 0, W, H, NC(8, 10, 14, 110));   // the launcher's dark tint
    Img(T("shade_bottom.png"), 0, H * 0.80f, W, H);
}
// the launcher's top bar: PLAY  MP Launcher vX.Y   ...   the Discord name
static void NeonPTopBar(float W, float u, float L)
{
    using namespace NeonPaint;
    const float h = 52 * u;
    Quad(0, 0, W, h, NC(12, 14, 18, 200));
    Quad(0, h - 1 * u, W, h, LP_LINE());
    NeonPText(F_CAPS, "PLAY", L - 8 * u, 13 * u, 22 * u, LP_TEXT(), 1.2f * u);
    Quad(L - 8 * u, h - 3 * u, L - 8 * u + TextW(F_CAPS, "PLAY", 22 * u, 1.2f * u), h, LP_RED());
    char v[64]; snprintf(v, sizeof(v), "MP Launcher v%s", MP_VERSION_STR);
    NeonPText(F_UI, v, L + 70 * u, 18 * u, 15 * u, LP_DIM());
}
static bool NeonPButtonC(const char* label, float x0, float y0, float x1, float y1, float u, NeonPaint::Col fill)
{
    using namespace NeonPaint;
    const bool held = Held(x0, y0, x1, y1);
    Quad(x0, y0, x1, y1, fill);
    if(held) Quad(x0, y0, x1, y1, NC(255, 255, 255, 40));
    NeonPFrame(x0, y0, x1, y1, 1.0f * u, LP_LINE());
    const float fs = 15 * u, w = TextW(F_CAPS, label, fs, 1.2f * u);
    NeonPText(F_CAPS, label, (x0 + x1 - w) * 0.5f, (y0 + y1 - fs) * 0.5f, fs, NC(255, 255, 255), 1.2f * u);
    return Tap(x0, y0, x1, y1);
}
static bool NeonPButton(const char* label, float x0, float y0, float x1, float y1, float u)
{
    return NeonPButtonC(label, x0, y0, x1, y1, u, NC(77, 87, 99, 185));
}
// ================================================================================================
// 5.01.11: the neon title menu's own pages, painted natively like the main menu (no ImGui on the title):
//   Browse servers  - the server list (all / featured / favourites / Wi-Fi), join = the Multiplayer load + the join
//   Settings        - PCMenu's PC settings: Controls / Game / Display / Audio / Mods (SAUtils) / Multiplayer, the
//                     game's settings table read and written directly (game/mobilesettings.cpp)
// + full controller support (D-pad / left stick, A, B, LB / RB, X / Y), the Discord connected pop-up (slides in from the
// right with notify.mp3, slides back out) and the menu music (Host/Media/neon_mainmenu.mp3, loops while the menu is up)
// ================================================================================================
enum { NT_MAIN = 0, NT_BROWSE, NT_SETTINGS };
static int s_nNT = NT_MAIN;
enum { NK_UP = 1, NK_DOWN = 2, NK_LEFT = 4, NK_RIGHT = 8, NK_A = 16, NK_B = 32, NK_LB = 64, NK_RB = 128, NK_X = 256, NK_Y = 512, NK_START = 1024 };
static std::atomic<int> s_nNeonKeysQ{0};
static int s_nNK = 0;                       // the keys of this paint
static std::atomic<uint64_t> s_nNeonPadAt{0};
static int s_nNeonDlgSel = 1;               // the focused dialog button (pad)
static bool NeonPadUI() { const uint64_t t = s_nNeonPadAt.load(); return t && MP_NowMs() - t < 20000; }
void MP::NeonPad(int keys)
{
    if(!keys) return;
    s_nNeonKeysQ.fetch_or(keys);
    s_nNeonPadAt = MP_NowMs();
}
static uint64_t s_nNeonTitleMs = 0;          // the last neon title paint (music, pop-up)

// ---- the Discord pop-up ----
static uint64_t s_nNeonToastAt = 0;
static char s_szNeonToast[2][96];
static int  s_nNeonLinkedSeen = -1;
static bool s_bNeonToastSound = false;
// 5.01.12: one Discord pop-up per link. Before, two came: the first when the menu showed up (the account file was
// read while the auto login rewrote it, so "linked" went true -> false -> true in the first seconds) and the second
// for the real connect. Now the state must stay the same for 1.5 s before it counts, the pop-up shows once a game
// run, and only SIGN OUT (then a new link) shows it again.
static int      s_nNeonLinkedCand = -1;
static uint64_t s_nNeonLinkedCandAt = 0;
static bool     s_bNeonToastShown = false;
static std::string s_NeonWhoCache;        // AuthAccounts() reads a file: once a second, not every paint
static uint64_t s_nNeonWhoAt = 0;
static void NeonToast(const char* a, const char* b)
{
    MP_CopyStr(s_szNeonToast[0], a, sizeof(s_szNeonToast[0])); MP_CopyStr(s_szNeonToast[1], b, sizeof(s_szNeonToast[1]));
    s_nNeonToastAt = MP_NowMs(); s_bNeonToastSound = true;
}
static void NeonPToast(float W, float H, float u)
{
    using namespace NeonPaint;
    if(!s_nNeonToastAt) return;
    const float kIn = 450, kHold = 3600, kOut = 450;
    const float t = (float)(MP_NowMs() - s_nNeonToastAt);
    if(t > kIn + kHold + kOut) { s_nNeonToastAt = 0; return; }
    float k = t < kIn ? t / kIn : (t < kIn + kHold ? 1.0f : 1.0f - (t - kIn - kHold) / kOut);
    k = k < 0 ? 0 : k; const float e = 1.0f - (1.0f - k) * (1.0f - k) * (1.0f - k);   // ease out
    const float w = 400 * u, h = 74 * u, x1 = W - 28 * u, x0r = x1 - w;
    const float x0 = x0r + (W - x0r + 10 * u) * (1.0f - e), y0 = 66 * u;
    // 5.01.14: the launcher notification card (mp_notify.inc): dark glass, the accent strip left, white title
    Quad(x0, y0, x0 + w, y0 + h, NC(16, 18, 22, 236));
    NeonPFrame(x0, y0, x0 + w, y0 + h, 1.0f * u, LP_LINE());
    Quad(x0, y0, x0 + 4 * u, y0 + h, NC(88, 101, 242));
    Img(T("discord.png"), x0 + 18 * u, y0 + 16 * u, x0 + 60 * u, y0 + 58 * u);
    NeonPText(F_CAPS, s_szNeonToast[0], x0 + 74 * u, y0 + 14 * u, 16 * u, LP_TEXT(), 1.0f * u);
    NeonPText(F_UI, s_szNeonToast[1], x0 + 74 * u, y0 + 42 * u, 12.5f * u, LP_DIM());
    const float p = t < kIn ? 1.0f : (t < kIn + kHold ? 1.0f - (t - kIn) / kHold : 0.0f);
    Quad(x0 + 4 * u, y0 + h - 3 * u, x0 + 4 * u + (w - 4 * u) * p, y0 + h, NC(88, 101, 242, 220));
}

// ---- the menu music ----
static int s_nNeonMus = 0;                   // 0 off, 1 playing, -1 failed (try again later)
static uint64_t s_nNeonMusAt = 0;
static std::atomic<int> s_nNeonMusDl{0};     // the local copy: 0 not asked, 1 downloading, 2 done, 3 failed
#define NEON_MUSIC_FILE "neon_mainmenu.mp3"
#define NEON_MUSIC_URL1 "https://raw.githubusercontent.com/ThirtyFoxMCx00E1/wagu/main/Host/Media/" NEON_MUSIC_FILE
#define NEON_MUSIC_URL2 "https://thirtyfoxmcx00e1.github.io/wagu/Host/Media/" NEON_MUSIC_FILE
static bool NeonMusicLocal(char* out, size_t n)
{   // files/TYFMC/MP/neon_mainmenu.mp3 (the user's own copy, or the one MP downloaded); "neon_mainmene.mp3" too
    static const char* const kNames[] = { NEON_MUSIC_FILE, "neon_mainmene.mp3", "neon_menu.mp3" };
    for(const char* nm : kNames) { snprintf(out, n, "%s/%s", MP::DataDir(), nm); if(access(out, R_OK) == 0) return true; }
    return false;
}
static void NeonMusicDownload()
{   // once: the track into files/TYFMC/MP so the next start plays it from the disk (no streaming)
    int z = 0; if(!s_nNeonMusDl.compare_exchange_strong(z, 1)) return;
    std::string dir = MP::DataDir();
    std::thread([dir]() {
        static const char* const kUrls[] = { NEON_MUSIC_URL1, NEON_MUSIC_URL2 };
        for(const char* u : kUrls)
        {
            std::string body, why;
            if(Online::HttpsFetch(u, body, 60000, why, 24u << 20) && body.size() > 100000)
            {
                const std::string dst = dir + "/" NEON_MUSIC_FILE, tmp = dst + ".part";
                if(FILE* f = fopen(tmp.c_str(), "wb")) { fwrite(body.data(), 1, body.size(), f); fclose(f); rename(tmp.c_str(), dst.c_str()); }
                logger->Info("Neon menu: menu music cached (%u bytes from %s)", (unsigned)body.size(), u);
                s_nNeonMusDl = 2; return;
            }
            logger->Info("Neon menu: menu music download from %s failed (%s)", u, why.c_str());
        }
        s_nNeonMusDl = 3;
    }).detach();
}
static void NeonMusicStop()
{
    if(s_nNeonMus == 1) { Sound::StreamStop(); logger->Info("Neon menu: menu music stopped"); }
    s_nNeonMus = 0;
}
static void NeonMusicInGame() { if(s_nNeonMus == 1 && Game::LocalPed() > 0) NeonMusicStop(); }   // MP::Process
static bool s_bNeonMusUrl = false, s_bNeonMusLoop = false;   // 5.01.14: streaming from the URL / a loop restart (no fade)
static void NeonMusicTick(bool want)   // game thread
{
    // 5.01.14: the music was late (streamed from the URL on every start until a copy was on the disk, + a 1.5 s fade)
    // or stayed silent (a URL / decoder failure left it "playing" forever with nothing playing, or waited 30 s).
    // Now: the local copy is downloaded as soon as the menu runs, a stuck / failed start is noticed within ~2 s and
    // started again (the local file first), the URL stream is swapped for the file once it is downloaded, a 0.4 s fade.
    if(s_nNeonMode == NEON_MODE_NEON && settings.neonMusic && s_nNeonMusDl.load() == 0)
    {   // once: a copy on the disk already, or download it now (before the first start needs it)
        char q[600];
        if(NeonMusicLocal(q, sizeof(q))) s_nNeonMusDl = 2; else NeonMusicDownload();
    }
    want = want && settings.neonMusic && settings.neonMusicVol > 0;
    if(!want) { NeonMusicStop(); s_bNeonMusLoop = false; return; }
    const uint64_t now = MP_NowMs();
    if(s_nNeonMus == -1 && now - s_nNeonMusAt < 5000) return;
    if(s_nNeonMus == 1)
    {
        Sound::StreamUpdate();
        const bool active = Sound::StreamActive(), ended = Sound::StreamEnded();
        const float pos = Sound::StreamPosition();
        char p[600];
        if(ended) { s_nNeonMus = 0; s_bNeonMusLoop = true; }   // the end of the track: again from the start (endless)
        else if(!active && now - s_nNeonMusAt > 1500)
        {   // it failed (no internet, decoder error): try again now, the file if there is one
            logger->Info("Neon menu: menu music stopped by itself (%s) - starting again", Sound::StreamInfo());
            Sound::StreamStop(); s_nNeonMus = 0; s_bNeonMusLoop = false;
        }
        else if(s_bNeonMusUrl && pos < 1.0f && s_nNeonMusDl.load() == 2 && NeonMusicLocal(p, sizeof(p)))
        {   // still connecting to the URL, but the downloaded copy is here: play that
            logger->Info("Neon menu: menu music -> the downloaded copy");
            Sound::StreamStop(); s_nNeonMus = 0; s_bNeonMusLoop = false;
        }
        else if(pos <= 0.0f && now - s_nNeonMusAt > (s_bNeonMusUrl ? 12000u : 4000u))
        {   // nothing came out: start it again
            logger->Info("Neon menu: menu music silent after %u ms (%s) - starting again", (unsigned)(now - s_nNeonMusAt), Sound::StreamInfo());
            Sound::StreamStop(); s_nNeonMus = -1; s_nNeonMusAt = now - 4000;
            return;
        }
    }
    if(s_nNeonMus != 1)
    {
        char p[600]; bool ok = false; const bool local = NeonMusicLocal(p, sizeof(p));
        if(local) ok = Sound::StreamPlay(p, s_bNeonMusLoop ? settings.neonMusicVol / 100.0f : 0.0f);
        s_bNeonMusUrl = false;
        if(!ok) { ok = Sound::StreamPlayUrl(NEON_MUSIC_URL1, 0.0f); s_bNeonMusUrl = ok; NeonMusicDownload(); }
        s_nNeonMus = ok ? 1 : -1; s_nNeonMusAt = now;
        if(!s_bNeonMusLoop || !ok) logger->Info("Neon menu: menu music %s (%s)", ok ? "playing" : "could not start", local ? p : NEON_MUSIC_URL1);
        if(!ok) return;
        Sound::StreamUpdate();
    }
    float k = s_bNeonMusLoop ? 1.0f : (now - s_nNeonMusAt) / 400.0f; if(k > 1) k = 1;   // a short fade in (none on a loop)
    Sound::StreamSetVolume(settings.neonMusicVol / 100.0f * k);
}

// ---- shared helpers ----
static std::string NeonFit(int font, const char* s, float px, float maxw, float track = 0)
{
    std::string t = s ? s : "";
    if(NeonPaint::TextW(font, t.c_str(), px, track) <= maxw) return t;
    while(!t.empty() && NeonPaint::TextW(font, (t + "...").c_str(), px, track) > maxw) t.pop_back();
    return t + "...";
}
static void NeonPAscii(char* o, size_t n, const char* s, bool upper)
{   // the atlases are ASCII: other letters are dropped
    size_t j = 0;
    for(size_t i = 0; s && s[i] && j + 1 < n; ++i)
    {
        unsigned char c = (unsigned char)s[i];
        if(c >= 32 && c < 127) o[j++] = upper ? (char)toupper(c) : (char)c;
    }
    o[j] = 0;
}
static void NeonPHeader(const char* title, const char* sub, float L, float u)
{   // 5.01.14: the launcher page title: bold caps, a red underline, the grey caption under it
    using namespace NeonPaint;
    NeonPTopBar(W(), u, L);
    char t[64]; NeonUpper(t, sizeof(t), title);
    const float fs = 30 * u, px = L - 8 * u, py = 66 * u;
    NeonPText(F_GOTHIC, t, px, py, fs, NC(255, 255, 255), 1.0f * u, true);
    Quad(px, py + fs + 6 * u, px + 46 * u, py + fs + 9 * u, LP_RED());
    if(sub) NeonPText(F_CAPS, sub, px + 56 * u, py + fs + 1 * u, 11 * u, LP_DIM(), 1.4f * u);
}
// the hints bottom right: { key, label } pairs
static void NeonPHints(const char* const* kv, int n, float W, float u, float L)
{
    using namespace NeonPaint;
    if(!NeonPadUI()) return;   // 5.01.12: pad hints only with a controller
    const float hy = 758 * u, fs = 11.5f * u;
    float x = W - L;
    for(int i = n - 1; i >= 0; --i)
    {
        const char* k = kv[i * 2]; const char* l = kv[i * 2 + 1];
        const float lw = TextW(F_UI, l, fs, 2.0f * u), kw = TextW(F_UI, k, fs, 1.2f * u) + 10 * u;
        x -= lw; NeonPText(F_UI, l, x, hy, fs, NC(120, 124, 132), 2.0f * u);
        x -= 8 * u + kw;
        NeonPFrame(x, hy - 3 * u, x + kw, hy + fs + 2 * u, 1.1f * u, NC(150, 152, 158));
        NeonPText(F_UI, k, x + 5 * u, hy, fs, NC(200, 202, 208), 1.2f * u);
        x -= 22 * u;
    }
}
static void NeonPPanel(float x0, float y0, float x1, float y1, float u)
{
    NeonPGlass(x0, y0, x1, y1);
    NeonPFrame(x0, y0, x1, y1, 1.0f * u, LP_LINE());
}
static bool NeonPBtn(const char* label, float x0, float y0, float x1, float y1, float u, bool focus, int kind = 0)
{   // kind: 0 slate, 1 green (CONNECT / JOIN), 2 red (QUIT)
    const NeonPaint::Col c = kind == 1 ? LP_GREEN() : (kind == 2 ? LP_RED() : NC(77, 87, 99, 185));
    const bool t = NeonPButtonC(label, x0, y0, x1, y1, u, c);
    if(focus) NeonPFrame(x0 - 2 * u, y0 - 2 * u, x1 + 2 * u, y1 + 2 * u, 1.6f * u, LP_BLUE());
    return t;
}

// ---- Browse servers ----
static std::vector<LRow> s_NTRows;
static uint64_t s_nNTBuiltAt = 0, s_nNTOpenAt = 0, s_nNTTapAt = 0;
static int   s_nNTSel = 0, s_nNTFocus = 0;     // focus: 0 the list, 1 BACK, 2 REFRESH, 3 JOIN
static float s_fNTScroll = 0;
static std::atomic<int> s_nNTJoinReq{-1};      // a row to join (game thread)
static LRow  s_NTJoin;
static void NeonTOpenBrowse() { s_nNT = NT_BROWSE; s_nNTBuiltAt = 0; s_nNTOpenAt = MP_NowMs(); s_nNTSel = 0; s_fNTScroll = 0; s_nNTFocus = 0; s_szNeonSearch[0] = 0; }
static void NeonPBrowse(float W, float H)
{
    using namespace NeonPaint;
    const float u = H / 804.0f;
    const float L = fminf(W * 0.07f, 100 * u * (W / H) / (1430.0f / 804.0f));
    NeonPBackdrop(W, H);
    char sub[96]; snprintf(sub, sizeof(sub), "%d SERVER%s - TAP A SERVER TWICE TO JOIN", (int)s_NTRows.size(), s_NTRows.size() == 1 ? "" : "S");
    NeonPHeader("Browse servers", sub, L, u);
    const int K = s_nNK;
    // the lists
    static const char* const kTabs[] = { "ALL", "FEATURED", "FAVOURITES", "WI-FI" };
    if(K & NK_LB) { s_nNeonList = (s_nNeonList + 3) % 4; s_nNTBuiltAt = 0; s_nNTSel = 0; s_fNTScroll = 0; }
    if(K & NK_RB) { s_nNeonList = (s_nNeonList + 1) % 4; s_nNTBuiltAt = 0; s_nNTSel = 0; s_fNTScroll = 0; }
    float tx = L; const float ty = 136 * u, th = 34 * u;
    for(int i = 0; i < 4; ++i)
    {
        const float fs = 13 * u, w = TextW(F_CAPS, kTabs[i], fs, 1.8f * u) + 34 * u;
        const bool on = s_nNeonList == i;
        if(on) Quad(tx, ty, tx + w, ty + th, NC(255, 255, 255, 18));
        if(Held(tx, ty, tx + w, ty + th)) Quad(tx, ty, tx + w, ty + th, NC(255, 255, 255, 30));
        NeonPText(F_CAPS, kTabs[i], tx + 17 * u, ty + (th - fs) * 0.5f, fs, on ? LP_BLUE() : NC(160, 164, 172), 1.8f * u);
        if(on) Quad(tx + 10 * u, ty + th - 2.4f * u, tx + w - 10 * u, ty + th, LP_BLUE());
        if(Tap(tx, ty, tx + w, ty + th) && !on) { s_nNeonList = i; s_nNTBuiltAt = 0; s_nNTSel = 0; s_fNTScroll = 0; }
        tx += w + 6 * u;
    }
    // the table
    const float x0 = L - 14 * u, x1 = W - L + 14 * u, tY0 = 182 * u, tY1 = 700 * u;
    NeonPPanel(x0, tY0, x1, tY1, u);
    const float cw = x1 - x0, cMode = x0 + cw * 0.50f, cPl = x0 + cw * 0.72f, cPing = x0 + cw * 0.86f;
    const float hy = tY0 + 12 * u;
    NeonPText(F_CAPS, "SERVER", x0 + 70 * u, hy, 10 * u, LP_DIM(), 1.6f * u);
    NeonPText(F_CAPS, "GAMEMODE", cMode, hy, 10 * u, LP_DIM(), 1.6f * u);
    NeonPText(F_CAPS, "PLAYERS", cPl, hy, 10 * u, LP_DIM(), 1.6f * u);
    NeonPText(F_CAPS, "PING", cPing, hy, 10 * u, LP_DIM(), 1.6f * u);
    Quad(x0 + 12 * u, hy + 20 * u, x1 - 12 * u, hy + 21 * u, LP_LINE());
    const float ly0 = hy + 26 * u, ly1 = tY1 - 6 * u, rh = 52 * u;
    const int n = (int)s_NTRows.size(), vis = (int)((ly1 - ly0) / rh);
    if(s_nNTSel >= n) s_nNTSel = n > 0 ? n - 1 : 0;
    // pad
    if(K & NK_UP)   { if(s_nNTFocus) s_nNTFocus = 0; else if(s_nNTSel > 0) --s_nNTSel; }
    if(K & NK_DOWN) { if(!s_nNTFocus && s_nNTSel < n - 1) ++s_nNTSel; else if(!s_nNTFocus) s_nNTFocus = 3; }
    if(s_nNTFocus && (K & NK_LEFT))  s_nNTFocus = s_nNTFocus > 1 ? s_nNTFocus - 1 : 1;
    if(s_nNTFocus && (K & NK_RIGHT)) s_nNTFocus = s_nNTFocus < 3 ? s_nNTFocus + 1 : 3;
    bool doJoin = false, doBack = (K & NK_B) != 0, doRefresh = (K & (NK_X | NK_Y)) != 0;
    if(K & NK_A) { if(s_nNTFocus == 1) doBack = true; else if(s_nNTFocus == 2) doRefresh = true; else doJoin = true; }
    if(K & (NK_UP | NK_DOWN))
    {   // keep the selection on screen
        const float top = s_nNTSel * rh;
        if(top < s_fNTScroll) s_fNTScroll = top;
        if(top + rh > s_fNTScroll + vis * rh) s_fNTScroll = top + rh - vis * rh;
    }
    // finger scroll (whole rows)
    s_fNTScroll -= DragDY(x0, ly0, x1, ly1);
    const float maxS = n > vis ? (n - vis) * rh : 0;
    if(s_fNTScroll < 0) s_fNTScroll = 0; if(s_fNTScroll > maxS) s_fNTScroll = maxS;
    const int first = (int)(s_fNTScroll / rh + 0.5f);
    for(int i = first; i < n && i < first + vis; ++i)
    {
        const LRow& r = s_NTRows[i];
        const float ry0 = ly0 + (i - first) * rh, ry1 = ry0 + rh - 4 * u;
        const bool sel = i == s_nNTSel;
        Quad(x0 + 8 * u, ry0, x1 - 8 * u, ry1, sel ? NC(255, 255, 255, 26) : NC(255, 255, 255, (i & 1) ? 7 : 3));
        if(Held(x0 + 8 * u, ry0, x1 - 8 * u, ry1) && !Dragging()) Quad(x0 + 8 * u, ry0, x1 - 8 * u, ry1, NC(255, 255, 255, 20));
        if(sel) { Quad(x0 + 8 * u, ry0, x0 + 11 * u, ry1, LP_BLUE()); if(!s_nNTFocus && NeonPadUI()) NeonPFrame(x0 + 8 * u, ry0, x1 - 8 * u, ry1, 1.2f * u, LP_BLUE()); }
        {   // the initials tile
            uint32_t hsh = 2166136261u; for(const char* p = r.name; *p; ++p) hsh = (hsh ^ (unsigned char)tolower((unsigned char)*p)) * 16777619u;
            static const NeonPaint::Col kc[5] = { {150, 52, 52, 255}, {52, 140, 72, 255}, {56, 92, 160, 255}, {160, 120, 50, 255}, {120, 70, 150, 255} };
            const NeonPaint::Col c = kc[hsh % 5];
            const float ix0 = x0 + 22 * u, iy0 = ry0 + 6 * u, sz = rh - 16 * u;
            Quad(ix0, iy0, ix0 + sz, iy0 + sz, NC(16, 16, 19, 255)); NeonPFrame(ix0, iy0, ix0 + sz, iy0 + sz, 1.4f * u, c);
            char ini[4] = { 0 }; int k = 0;
            for(const char* p = r.name; *p && k < 2; ++p) if(isalnum((unsigned char)*p) && (p == r.name || !isalnum((unsigned char)p[-1]))) ini[k++] = (char)toupper((unsigned char)*p);
            if(!k) { ini[0] = 'M'; ini[1] = 'P'; }
            const float fs = sz * 0.36f, w = TextW(F_CAPS, ini, fs);
            NeonPText(F_CAPS, ini, ix0 + (sz - w) * 0.5f, iy0 + (sz - fs) * 0.5f, fs, c);
        }
        char nm[64]; NeonPAscii(nm, sizeof(nm), r.name, false);
        float nx = x0 + 70 * u; const float nameMax = cMode - nx - 16 * u;
        if(NeonFeatured(r))
        {
            const float fw = TextW(F_CAPS, "FEATURED", 8.5f * u, 1.2f * u) + 12 * u;
            Quad(nx, ry0 + 9 * u, nx + fw, ry0 + 23 * u, LP_BLUE(40)); NeonPFrame(nx, ry0 + 9 * u, nx + fw, ry0 + 23 * u, 1.0f * u, LP_BLUE());
            NeonPText(F_CAPS, "FEATURED", nx + 6 * u, ry0 + 11.5f * u, 8.5f * u, LP_BLUE(), 1.2f * u);
            NeonPText(F_UI, NeonFit(F_UI, nm, 16 * u, nameMax - fw - 8 * u).c_str(), nx + fw + 8 * u, ry0 + 7 * u, 16 * u, NC(230, 232, 238));
        }
        else NeonPText(F_UI, NeonFit(F_UI, nm, 16 * u, nameMax).c_str(), nx, ry0 + 7 * u, 16 * u, NC(230, 232, 238));
        {
            char d[160], line[200];
            NeonPAscii(d, sizeof(d), r.desc[0] ? r.desc : (r.live ? "Online now" : "Saved server"), false);
            snprintf(line, sizeof(line), "%s%s%s", r.locked ? "LOCKED - " : "", r.internet ? "" : (r.kind == 1 ? "WI-FI - " : ""), d);
            NeonPText(F_UI, NeonFit(F_UI, line, 11 * u, nameMax).c_str(), nx, ry0 + 28 * u, 11 * u, LP_DIM());
        }
        char md[48]; NeonPAscii(md, sizeof(md), r.mode[0] ? r.mode : "Freeroam", true);
        NeonPText(F_CAPS, NeonFit(F_CAPS, md, 11.5f * u, cPl - cMode - 16 * u, 1.2f * u).c_str(), cMode, ry0 + 15 * u, 11.5f * u, NC(200, 202, 208), 1.2f * u);
        char pl[32]; if(r.maxp > 0) snprintf(pl, sizeof(pl), "%d / %d", r.players, r.maxp); else snprintf(pl, sizeof(pl), "%d", r.players);
        NeonPText(F_UI, pl, cPl, ry0 + 13 * u, 15 * u, r.players > 0 ? NC(226, 228, 232) : LP_DIM());
        char pg[16]; NeonPaint::Col pc = LP_DIM();
        if(r.ping > 0) { snprintf(pg, sizeof(pg), "%d MS", r.ping); pc = r.ping < 90 ? NC(110, 200, 120) : (r.ping < 180 ? NC(222, 200, 110) : NC(220, 100, 90)); }
        else snprintf(pg, sizeof(pg), "--");
        NeonPText(F_UI, pg, cPing, ry0 + 13 * u, 15 * u, pc, 1.0f * u);
        if(Tap(x0 + 8 * u, ry0, x1 - 8 * u, ry1))
        {
            const uint64_t now = MP_NowMs();
            if(sel && now - s_nNTTapAt < 700) doJoin = true;
            s_nNTSel = i; s_nNTTapAt = now; s_nNTFocus = 0;
        }
    }
    if(n > vis)
    {   // the scroll bar
        const float bh = (ly1 - ly0) * vis / n, by = ly0 + (ly1 - ly0 - bh) * (maxS > 0 ? s_fNTScroll / maxS : 0);
        Quad(x1 - 6 * u, by, x1 - 3 * u, by + bh, LP_BLUE(160));
    }
    if(!n)
    {
        const bool early = MP_NowMs() - s_nNTOpenAt < 4000;
        const char* m = early ? "LOOKING FOR SERVERS..." : (s_nNeonList == 2 ? "NO FAVOURITE SERVERS YET" : (s_nNeonList == 3 ? "NO SERVERS ON THIS WI-FI" : "NO SERVERS FOUND - CHECK YOUR INTERNET"));
        const float fs = 14 * u, w = TextW(F_CAPS, m, fs, 1.8f * u);
        NeonPText(F_CAPS, m, (x0 + x1 - w) * 0.5f, (ly0 + ly1) * 0.5f - fs, fs, NC(160, 164, 172), 1.8f * u);
    }
    // the buttons
    const float by0 = 712 * u, by1 = 748 * u;
    if(NeonPBtn("BACK", L - 14 * u, by0, L + 150 * u, by1, u, s_nNTFocus == 1 && NeonPadUI())) doBack = true;
    if(NeonPBtn("REFRESH", L + 164 * u, by0, L + 328 * u, by1, u, s_nNTFocus == 2 && NeonPadUI())) doRefresh = true;
    if(NeonPBtn("JOIN", W - L - 200 * u, by0, W - L + 14 * u, by1, u, s_nNTFocus == 3 && NeonPadUI(), 1)) doJoin = true;
    static const char* const kHints[] = { "A", "JOIN", "B", "BACK", "LB RB", "LIST", "Y", "REFRESH" };
    NeonPHints(kHints, 4, W, u, L);
    if(doRefresh) { ServerList::Refresh(); s_nNTBuiltAt = 0; s_nNTOpenAt = MP_NowMs(); logger->Info("Neon menu: Browse servers -> refresh"); }
    if(doJoin && s_nNTSel >= 0 && s_nNTSel < n) { s_NTJoin = s_NTRows[s_nNTSel]; s_nNTJoinReq = s_nNTSel; }
    if(doBack) s_nNT = NT_MAIN;
}

// ---- Settings ----
static std::vector<GameSettings::Item> s_NSItems;
static uint64_t s_nNSBuiltAt = 0;
static int   s_nNSTab = 0, s_nNSSel = 0, s_nNSFocus = 0;   // focus: 0 rows, 1 BACK, 2 GAME OPTIONS
static float s_fNSScroll = 0;
static bool  s_bNSDirty = false, s_bNSMpDirty = false;
enum { NS_MUSIC = -1, NS_MUSICVOL = -2, NS_REGION = -3 };
static const struct { int tab; const char* name; } kNSTabs[] = { { 0, "CONTROLS" }, { 1, "GAME" }, { 2, "DISPLAY" }, { 3, "AUDIO" }, { 5, "MODS" }, { 6, "MULTIPLAYER" } };
static const int kNSTabCount = 6;
static void NeonTOpenSettings() { s_nNT = NT_SETTINGS; s_nNSBuiltAt = 0; s_nNSSel = 0; s_fNSScroll = 0; s_nNSFocus = 0; }
static void NeonNSBuild()
{
    GameSettings::Items(s_NSItems);
    GameSettings::Item it;
    it.tab = 6;
    it.id = NS_MUSIC; it.kind = GameSettings::K_TOGGLE; MP_CopyStr(it.label, "Menu music", sizeof(it.label)); s_NSItems.push_back(it);
    it.id = NS_MUSICVOL; it.kind = GameSettings::K_SLIDER; MP_CopyStr(it.label, "Menu music volume", sizeof(it.label)); s_NSItems.push_back(it);
    it.id = NS_REGION; it.kind = GameSettings::K_TOGGLE; MP_CopyStr(it.label, "Server region", sizeof(it.label)); s_NSItems.push_back(it);
}
static std::string NeonNSValue(const GameSettings::Item& it)
{
    char b[64];
    switch(it.id)
    {
        case NS_MUSIC: return settings.neonMusic ? "ON" : "OFF";
        case NS_MUSICVOL: snprintf(b, sizeof(b), "%d%%", settings.neonMusicVol); return b;
        case NS_REGION:
            if(settings.lobbyAuto) return "AUTO";
            if(settings.region >= 0 && settings.region < REGION_COUNT) { NeonPAscii(b, sizeof(b), g_MPRegionNames[settings.region], true); return b; }
            return "AUTO";
    }
    if(it.kind == GameSettings::K_BUTTON || it.kind == GameSettings::K_HEADER || it.kind == GameSettings::K_INFO) return it.value;   // 5.01.13
    std::string v = GameSettings::ValueText(it.id);
    char a[64]; NeonPAscii(a, sizeof(a), v.c_str(), true);
    return a[0] ? a : "-";
}
static float NeonNSFrac(const GameSettings::Item& it)
{
    if(it.id == NS_MUSICVOL) return settings.neonMusicVol / 100.0f;
    GameSettings::View v; if(!GameSettings::Read(it.id, v) || v.vmax <= v.vmin) return 0;
    return (float)(v.value - v.vmin) / (float)(v.vmax - v.vmin);
}
static void NeonNSStep(const GameSettings::Item& it, int dir)
{
    if(it.kind == GameSettings::K_HEADER || it.kind == GameSettings::K_INFO) return;   // 5.01.13: titles / mods folder rows
    if(it.kind == GameSettings::K_BUTTON) { if(dir > 0) { GameSettings::Press(it.ref); s_nNSBuiltAt = 0; } return; }   // a mod's button: its action
    switch(it.id)
    {
        case NS_MUSIC: settings.neonMusic = !settings.neonMusic; s_bNSMpDirty = true; return;
        case NS_MUSICVOL: settings.neonMusicVol += dir < 0 ? -5 : 5; if(settings.neonMusicVol < 0) settings.neonMusicVol = 0; if(settings.neonMusicVol > 100) settings.neonMusicVol = 100; s_bNSMpDirty = true; return;
        case NS_REGION:
        {   // Auto -> Asia -> Europe -> NA -> Oceania -> Auto
            int cur = settings.lobbyAuto ? -1 : settings.region;
            cur += dir < 0 ? -1 : 1;
            if(cur >= REGION_COUNT) cur = -1; if(cur < -1) cur = REGION_COUNT - 1;
            LRegionChoose(cur); s_bNSMpDirty = true; return;
        }
    }
    GameSettings::Step(it.id, dir); s_bNSDirty = true;
}
static void NeonNSFraction(const GameSettings::Item& it, float f)
{
    if(f < 0) f = 0; if(f > 1) f = 1;
    if(it.id == NS_MUSICVOL) { const int v = (int)(f * 100 + 0.5f); if(v != settings.neonMusicVol) { settings.neonMusicVol = v; s_bNSMpDirty = true; } return; }
    GameSettings::SetFraction(it.id, f); s_bNSDirty = true;
}
static void NeonNSLeave()
{
    if(s_bNSDirty) { GameSettings::Save(); s_bNSDirty = false; }
    if(s_bNSMpDirty) { MP::SaveConfig(); s_bNSMpDirty = false; }
    s_nNT = NT_MAIN;
}
static std::atomic<bool> s_bNSGameOpts{false};
static void NeonPSettings(float W, float H)
{
    using namespace NeonPaint;
    const float u = H / 804.0f;
    const float L = fminf(W * 0.07f, 100 * u * (W / H) / (1430.0f / 804.0f));
    NeonPBackdrop(W, H);
    NeonPHeader("Settings", "CONTROLS - GAME - DISPLAY - AUDIO - MODS - MULTIPLAYER", L, u);
    const int K = s_nNK;
    if(K & NK_LB) { s_nNSTab = (s_nNSTab + kNSTabCount - 1) % kNSTabCount; s_nNSSel = 0; s_fNSScroll = 0; s_nNSFocus = 0; }
    if(K & NK_RB) { s_nNSTab = (s_nNSTab + 1) % kNSTabCount; s_nNSSel = 0; s_fNSScroll = 0; s_nNSFocus = 0; }
    // the tabs (left)
    const float tx0 = L - 14 * u, tx1 = L + 230 * u, ty0 = 150 * u, tstep = 52 * u;
    NeonPPanel(tx0, ty0, tx1, ty0 + kNSTabCount * tstep + 16 * u, u);
    for(int i = 0; i < kNSTabCount; ++i)
    {
        const float y0 = ty0 + 8 * u + i * tstep, y1 = y0 + tstep - 4 * u;
        const bool on = i == s_nNSTab;
        if(on) { Quad(tx0 + 6 * u, y0, tx1 - 6 * u, y1, NC(255, 255, 255, 24)); Quad(tx0 + 6 * u, y0, tx0 + 9 * u, y1, LP_BLUE()); }
        if(Held(tx0, y0, tx1, y1)) Quad(tx0 + 6 * u, y0, tx1 - 6 * u, y1, NC(255, 255, 255, 18));
        const float fs = 14 * u;
        NeonPText(F_CAPS, kNSTabs[i].name, tx0 + 26 * u, y0 + (y1 - y0 - fs) * 0.5f, fs, on ? LP_BLUE() : NC(170, 174, 182), 1.8f * u);
        if(Tap(tx0, y0, tx1, y1)) { s_nNSTab = i; s_nNSSel = 0; s_fNSScroll = 0; s_nNSFocus = 0; }
    }
    // the rows of the tab
    std::vector<int> rows;
    for(int i = 0; i < (int)s_NSItems.size(); ++i) if(s_NSItems[i].tab == kNSTabs[s_nNSTab].tab) rows.push_back(i);
    const float px0 = tx1 + 18 * u, px1 = W - L + 14 * u, py0 = 150 * u, py1 = 700 * u;
    NeonPPanel(px0, py0, px1, py1, u);
    {
        char t[48]; snprintf(t, sizeof(t), "%s", kNSTabs[s_nNSTab].name);
        NeonPText(F_CAPS, t, px0 + 22 * u, py0 + 14 * u, 12 * u, LP_DIM(), 2.0f * u);
        Quad(px0 + 12 * u, py0 + 36 * u, px1 - 12 * u, py0 + 37 * u, LP_LINE());
    }
    const float ly0 = py0 + 44 * u, ly1 = py1 - 8 * u, rh = 50 * u;
    const int n = (int)rows.size(), vis = (int)((ly1 - ly0) / rh);
    if(s_nNSSel >= n) s_nNSSel = n > 0 ? n - 1 : 0;
    if(K & NK_UP)   { if(s_nNSFocus) s_nNSFocus = 0; else if(s_nNSSel > 0) --s_nNSSel; }
    if(K & NK_DOWN) { if(!s_nNSFocus && s_nNSSel < n - 1) ++s_nNSSel; else if(!s_nNSFocus) s_nNSFocus = 1; }
    bool doBack = (K & NK_B) != 0, doGame = false;
    if(s_nNSFocus)
    {
        if(K & NK_LEFT) s_nNSFocus = 1; if(K & NK_RIGHT) s_nNSFocus = 2;
        if(K & NK_A) { if(s_nNSFocus == 1) doBack = true; else doGame = true; }
    }
    else if(n > 0)
    {
        const GameSettings::Item& it = s_NSItems[rows[s_nNSSel]];
        if(K & NK_LEFT) NeonNSStep(it, -1);
        if(K & (NK_RIGHT | NK_A)) NeonNSStep(it, +1);
    }
    if(K & (NK_UP | NK_DOWN))
    {
        const float top = s_nNSSel * rh;
        if(top < s_fNSScroll) s_fNSScroll = top;
        if(top + rh > s_fNSScroll + vis * rh) s_fNSScroll = top + rh - vis * rh;
    }
    const float vx1 = px1 - 20 * u, vx0 = vx1 - 320 * u;
    // finger scroll: only outside the value column (the sliders follow the finger there)
    s_fNSScroll -= DragDY(px0, ly0, vx0 - 8 * u, ly1);
    const float maxS = n > vis ? (n - vis) * rh : 0;
    if(s_fNSScroll < 0) s_fNSScroll = 0; if(s_fNSScroll > maxS) s_fNSScroll = maxS;
    const int first = (int)(s_fNSScroll / rh + 0.5f);
    for(int k = first; k < n && k < first + vis; ++k)
    {
        const GameSettings::Item& it = s_NSItems[rows[k]];
        const float ry0 = ly0 + (k - first) * rh, ry1 = ry0 + rh - 4 * u, mid = (ry0 + ry1) * 0.5f;
        const bool sel = k == s_nNSSel;
        Quad(px0 + 8 * u, ry0, px1 - 8 * u, ry1, sel ? NC(255, 255, 255, 22) : NC(255, 255, 255, (k & 1) ? 6 : 2));
        if(sel) { Quad(px0 + 8 * u, ry0, px0 + 11 * u, ry1, LP_BLUE()); if(!s_nNSFocus && NeonPadUI()) NeonPFrame(px0 + 8 * u, ry0, px1 - 8 * u, ry1, 1.2f * u, LP_BLUE()); }
        char lb[64]; NeonPAscii(lb, sizeof(lb), it.label[0] ? it.label : "Mod option", true);
        const std::string val = NeonNSValue(it);
        if(it.kind == GameSettings::K_HEADER)
        {   // 5.01.13: a mod's settings tab / the mods folder (PCMenu's Mods list)
            Quad(px0 + 12 * u, ry1 - 2 * u, px1 - 12 * u, ry1 - 1 * u, NC(255, 255, 255, 46));
            NeonPText(F_CAPS, NeonFit(F_CAPS, lb, 12.5f * u, vx0 - px0 - 40 * u, 2.0f * u).c_str(), px0 + 22 * u, mid - 6 * u, 12.5f * u, LP_BLUE(), 2.0f * u);
            const float w = TextW(F_UI, val.c_str(), 12 * u);
            NeonPText(F_UI, val.c_str(), vx1 - w, mid - 7 * u, 12 * u, LP_DIM());
            continue;
        }
        NeonPText(F_CAPS, NeonFit(F_CAPS, lb, 13.5f * u, vx0 - px0 - 40 * u, 1.4f * u).c_str(), px0 + 26 * u, mid - 7 * u, 13.5f * u, NC(226, 228, 232), 1.4f * u);
        if(it.kind == GameSettings::K_INFO)
        {
            const float w = TextW(F_UI, val.c_str(), 13 * u);
            NeonPText(F_UI, val.c_str(), vx1 - w, mid - 8 * u, 13 * u, NC(140, 144, 152));
            if(Tap(px0 + 8 * u, ry0, px1 - 8 * u, ry1)) { s_nNSSel = k; s_nNSFocus = 0; }
            continue;
        }
        if(it.kind == GameSettings::K_BUTTON)
        {
            const float bx0 = vx1 - 130 * u, hb = Held(bx0, ry0 + 6 * u, vx1, ry1 - 6 * u);
            Quad(bx0, ry0 + 8 * u, vx1, ry1 - 8 * u, hb ? LP_BLUE(90) : LP_BLUE(40));
            const float w = TextW(F_CAPS, val.c_str(), 12 * u, 1.6f * u);
            NeonPText(F_CAPS, val.c_str(), (bx0 + vx1 - w) * 0.5f, mid - 6 * u, 12 * u, NC(255, 255, 255), 1.6f * u);
            if(Tap(px0 + 8 * u, ry0, px1 - 8 * u, ry1)) { s_nNSSel = k; s_nNSFocus = 0; NeonNSStep(it, +1); }
            continue;
        }
        if(it.kind == GameSettings::K_SLIDER)
        {
            const float bx0 = vx0 + 14 * u, bx1 = vx1 - 78 * u, f = NeonNSFrac(it);
            Quad(bx0, mid - 3 * u, bx1, mid + 3 * u, NC(40, 42, 48));
            Quad(bx0, mid - 3 * u, bx0 + (bx1 - bx0) * f, mid + 3 * u, LP_BLUE());
            const float kx = bx0 + (bx1 - bx0) * f;
            Quad(kx - 5 * u, mid - 11 * u, kx + 5 * u, mid + 11 * u, NC(255, 255, 255));
            const float w = TextW(F_UI, val.c_str(), 14 * u);
            NeonPText(F_UI, val.c_str(), vx1 - w, mid - 8 * u, 14 * u, LP_BLUE());
            float fx = 0;
            if(HeldX(bx0 - 16 * u, ry0, bx1 + 16 * u, ry1, &fx)) { s_nNSSel = k; s_nNSFocus = 0; NeonNSFraction(it, (fx - bx0) / (bx1 - bx0)); }
        }
        else
        {
            const float aw = 34 * u;
            const bool hl = Held(vx0, ry0, vx0 + aw, ry1), hr = Held(vx1 - aw, ry0, vx1, ry1);
            NeonPText(F_CAPS, "<", vx0 + 10 * u, mid - 8 * u, 16 * u, hl ? NC(255, 255, 255) : NC(150, 152, 158));
            NeonPText(F_CAPS, ">", vx1 - 20 * u, mid - 8 * u, 16 * u, hr ? NC(255, 255, 255) : NC(150, 152, 158));
            const std::string fit = NeonFit(F_CAPS, val.c_str(), 13 * u, vx1 - vx0 - 2 * aw - 8 * u, 1.2f * u);
            const float w = TextW(F_CAPS, fit.c_str(), 13 * u, 1.2f * u);
            NeonPText(F_CAPS, fit.c_str(), (vx0 + vx1 - w) * 0.5f, mid - 7 * u, 13 * u, LP_BLUE(), 1.2f * u);
            if(Tap(vx0, ry0, (vx0 + vx1) * 0.5f, ry1)) { s_nNSSel = k; s_nNSFocus = 0; NeonNSStep(it, -1); }
            else if(Tap((vx0 + vx1) * 0.5f, ry0, vx1, ry1)) { s_nNSSel = k; s_nNSFocus = 0; NeonNSStep(it, +1); }
        }
        if(Tap(px0 + 8 * u, ry0, vx0 - 8 * u, ry1))
        {   // the label: select it (a toggle also goes to its next value, like PCMenu)
            s_nNSSel = k; s_nNSFocus = 0;
            if(it.kind == GameSettings::K_TOGGLE) NeonNSStep(it, +1);
        }
    }
    if(n > vis)
    {
        const float bh = (ly1 - ly0) * vis / n, by = ly0 + (ly1 - ly0 - bh) * (maxS > 0 ? s_fNSScroll / maxS : 0);
        Quad(px1 - 6 * u, by, px1 - 3 * u, by + bh, LP_BLUE(160));
    }
    if(!n)
    {
        const char* m = kNSTabs[s_nNSTab].tab == 5 ? (GameSettings::SAUtilsFound() ? "NO MOD SETTINGS - MODS ADD THEM THROUGH SAUTILS" : "NO MODS FOUND - INSTALL SAUTILS FOR MOD SETTINGS") : (GameSettings::Ready() ? "NOTHING TO SET HERE" : "THE GAME'S SETTINGS COULD NOT BE READ");
        const float fs = 13 * u, w = TextW(F_CAPS, m, fs, 1.6f * u);
        NeonPText(F_CAPS, m, (px0 + px1 - w) * 0.5f, (ly0 + ly1) * 0.5f - fs, fs, NC(160, 164, 172), 1.6f * u);
    }
    const float by0 = 712 * u, by1 = 748 * u;
    if(NeonPBtn("BACK", L - 14 * u, by0, L + 150 * u, by1, u, s_nNSFocus == 1 && NeonPadUI())) doBack = true;
    if(NeonPBtn("GAME OPTIONS", L + 164 * u, by0, L + 400 * u, by1, u, s_nNSFocus == 2 && NeonPadUI())) doGame = true;
    static const char* const kHints[] = { "LB RB", "TAB", "< >", "CHANGE", "B", "BACK" };
    NeonPHints(kHints, 3, W, u, L);
    if(doGame) { NeonNSLeave(); s_bNSGameOpts = true; }
    else if(doBack) NeonNSLeave();
}

static void NeonPDialog(float W, float H, float u)
{
    using namespace NeonPaint;
    Quad(0, 0, W, H, NC(0, 0, 0, 170));
    const float dw = 520 * u, dh = 230 * u, x0 = (W - dw) * 0.5f, y0 = (H - dh) * 0.5f;
    Quad(x0, y0, x0 + dw, y0 + dh, NC(36, 40, 46, 252));   // 5.01.14: the launcher's dialog panel
    NeonPFrame(x0, y0, x0 + dw, y0 + dh, 1.0f * u, LP_LINE());
    Quad(x0, y0, x0 + dw, y0 + 3 * u, LP_RED());
    const bool about = s_nNeonDlg == 1;
    NeonPText(F_GOTHIC, about ? "ABOUT" : "QUIT GAME", x0 + 28 * u, y0 + 20 * u, 28 * u, NC(255, 255, 255), 1.0f * u, true);
    char l[5][120]; int n = 0;
    if(about)
    {
        snprintf(l[n++], 120, "MP Multiplayer %s for GTA San Andreas (Android, AML).", MP_VERSION_STR);
        snprintf(l[n++], 120, "by ThirtyFoxMCx00E1 - mpsamp-open.github.io");
        snprintf(l[n++], 120, "Discord: %s", MP_AUTH_DISCORD_URL);
        snprintf(l[n++], 120, "Font: Fredoka (SIL OFL 1.1).");
    }
    else snprintf(l[n++], 120, "Close GTA San Andreas now? Unsaved progress is lost.");
    for(int i = 0; i < n; ++i) NeonPText(F_UI, l[i], x0 + 30 * u, y0 + (72 + i * 22) * u, 15 * u, LP_DIM());
    const float by0 = y0 + dh - 62 * u, by1 = by0 + 40 * u;
    const bool pad = NeonPadUI();   // 5.01.11: the controller: left / right = the button, A = press, B = close
    if(about)
    {
        if(NeonPBtn("CLOSE", x0 + dw - 180 * u, by0, x0 + dw - 30 * u, by1, u, pad) || (s_nNK & (NK_A | NK_B))) s_nNeonDlg = 0;
    }
    else
    {
        if(s_nNK & NK_LEFT) s_nNeonDlgSel = 0;
        if(s_nNK & NK_RIGHT) s_nNeonDlgSel = 1;
        if(NeonPBtn("CANCEL", x0 + dw - 340 * u, by0, x0 + dw - 190 * u, by1, u, pad && s_nNeonDlgSel == 0) || (s_nNK & NK_B) || ((s_nNK & NK_A) && s_nNeonDlgSel == 0)) s_nNeonDlg = 0;
        else if(NeonPBtn("QUIT", x0 + dw - 180 * u, by0, x0 + dw - 30 * u, by1, u, pad && s_nNeonDlgSel == 1, 2) || ((s_nNK & NK_A) && s_nNeonDlgSel == 1)) s_nNeonAct = NA_QUIT;
    }
    EatTap();   // nothing under the dialog gets the tap
}
static void NeonPTitle(float W, float H)
{
    using namespace NeonPaint;
    const float u = H / 804.0f;
    const float L = fminf(W * 0.07f, 100 * u * (W / H) / (1430.0f / 804.0f));
    NeonPBackdrop(W, H);
    NeonPTopBar(W, u, L);
    // 5.01.14: the launcher layout: the menu is a glass side panel (logo, PLAY / OPTIONS sections, rows with the
    // white selection strip + the blue bar), the bottom bar has the status and the green BROWSE SERVERS button
    const float px0 = L - 14 * u, px1 = L + 340 * u, py0 = 70 * u, py1 = 728 * u;
    NeonPPanel(px0, py0, px1, py1, u);
    if(const Tex* lg = T("logo.png"))
    {
        const float w = 300 * u, h = w * (601.0f / 1920.0f);   // title.png's shape (the texture is stretched to 1024x512)
        const float lx = (px0 + px1 - w) * 0.5f;
        Img(lg, lx, py0 + 16 * u, lx + w, py0 + 16 * u + h);
    }
    // the dialog takes the taps first (it is drawn last, on top)
    const bool dlg = s_nNeonDlg != 0;
    bool tapUsed = false;
    static const char* const kItems[] = { "Resume", "New game", "Browse servers", "Quick connect", "Settings", "About", "Quit game" };
    const int ni = 7; const float fs = 21 * u, step = 46 * u, ix = px0 + 30 * u;
    // 5.01.11: the items do what they say: Browse servers / Settings open their neon pages right here
    auto activate = [&](int i)
    {
        s_nNeonSel = i;
        switch(i)
        {
            case 0: s_nNeonAct = NA_RESUME; break;
            case 1: s_nNeonAct = NA_NEWGAME; break;
            case 2: NeonTOpenBrowse(); break;
            case 3: s_nNeonAct = NA_QUICK; break;
            case 4: NeonTOpenSettings(); break;
            case 5: s_nNeonDlg = 1; break;
            case 6: s_nNeonDlg = 2; s_nNeonDlgSel = 1; break;
            case 7: NeonTOpenBrowse(); break;            // the BROWSE SERVERS button
        }
    };
    if(!dlg && s_nNK)
    {   // 5.01.11: the controller (PCMenu's ControllerNav): up / down = the item (then the button), A = press, B = Quit game
        if(s_nNeonSel < 0 || s_nNeonSel > 7) s_nNeonSel = 0;
        if(s_nNK & NK_UP) s_nNeonSel = (s_nNeonSel + 7) % 8;
        if(s_nNK & NK_DOWN) s_nNeonSel = (s_nNeonSel + 1) % 8;
        if((s_nNK & NK_RIGHT) && s_nNeonSel < 7) s_nNeonSel = 7;
        if((s_nNK & NK_LEFT) && s_nNeonSel >= 7) s_nNeonSel = 2;
        if(s_nNK & (NK_A | NK_START)) activate(s_nNeonSel);
        else if(s_nNK & NK_B) { s_nNeonDlg = 2; s_nNeonDlgSel = 0; }
        if(s_nNK & NK_Y) NeonTOpenBrowse();
    }
    float y = py0 + 136 * u;
    for(int i = 0; i < ni; ++i)
    {
        if(i == 0 || i == 4)
        {   // the section titles
            NeonPText(F_CAPS, i == 0 ? "PLAY" : "OPTIONS", ix, y + 6 * u, 11 * u, LP_DIM(), 2.0f * u);
            y += 28 * u;
        }
        const float hx0 = px0 + 8 * u, hy0 = y, hx1 = px1 - 8 * u, hy1 = y + step - 4 * u;
        const bool held = !dlg && Held(hx0, hy0, hx1, hy1);
        const bool on = i == s_nNeonSel || held;
        if(on) { Quad(hx0, hy0, hx1, hy1, NC(255, 255, 255, held ? 34 : 22)); Quad(hx0, hy0, hx0 + 3 * u, hy1, LP_BLUE()); }
        if(on && i == s_nNeonSel && NeonPadUI()) NeonPFrame(hx0, hy0, hx1, hy1, 1.2f * u, LP_BLUE());
        const NeonPaint::Col c = i == 6 ? (on ? NC(255, 120, 104) : LP_RED()) : (on ? NC(255, 255, 255) : LP_TEXT());
        NeonPText(F_GOTHIC, kItems[i], ix, hy0 + (hy1 - hy0 - fs) * 0.5f - 1 * u, fs, c, 0.4f * u);
        if(on) NeonPText(F_CAPS, ">", hx1 - 26 * u, hy0 + (hy1 - hy0 - 16 * u) * 0.5f, 16 * u, LP_DIM());
        if(!dlg && !tapUsed && Tap(hx0, hy0, hx1, hy1)) { tapUsed = true; activate(i); }
        y += step;
    }
    {   // tagline
        const float ty = py1 - 52 * u;
        Quad(px0 + 14 * u, ty - 12 * u, px1 - 14 * u, ty - 11 * u, LP_LINE());
        NeonPText(F_UI, "Find a world and join the streets", ix, ty, 13 * u, LP_DIM());
        NeonPText(F_UI, "of San Andreas.", ix, ty + 17 * u, 13 * u, LP_DIM());
    }
    {   // Neon Identity: 5.01.12: no card on the menu any more. It showed "DISCORD CONNECTED" all the time right above the
        // pop-up (two notifications). Only the slide-in pop-up is left: once a run, when Discord is linked.
        const uint64_t nowMs = MP_NowMs();
        if(!s_AuthUser.empty()) { s_NeonWhoCache = s_AuthUser; s_nNeonWhoAt = nowMs; }
        else if(!s_nNeonWhoAt || nowMs - s_nNeonWhoAt > 1000)
        {
            std::vector<std::string> acc = AuthAccounts();
            if(!acc.empty()) s_NeonWhoCache = acc[0];
            else if(s_NeonWhoCache.empty() || nowMs - s_nNeonWhoAt > 3000) s_NeonWhoCache.clear();   // a file being rewritten reads empty for a moment
            s_nNeonWhoAt = nowMs;
        }
        const std::string who = s_NeonWhoCache;
        const bool linkedNow = !who.empty() || !s_Auth.discordName.empty();
        if((int)linkedNow != s_nNeonLinkedCand) { s_nNeonLinkedCand = (int)linkedNow; s_nNeonLinkedCandAt = nowMs; }
        if(s_nNeonLinkedSeen != s_nNeonLinkedCand && nowMs - s_nNeonLinkedCandAt >= 1500)
        {   // 5.01.11: Discord connected = the pop-up slides in from the right (notify.mp3) and back out
            if(s_nNeonLinkedCand == 1 && !s_bNeonToastShown)
            {
                char b[96]; char wa[48]; NeonPAscii(wa, sizeof(wa), who.c_str(), false);
                snprintf(b, sizeof(b), "Neon account connected%s%s", wa[0] ? " - " : "", wa);
                NeonToast("DISCORD CONNECTED", b);
                s_bNeonToastShown = true;
                logger->Info("Neon menu: Discord connected pop-up (%s)", wa);
            }
            s_nNeonLinkedSeen = s_nNeonLinkedCand;
        }
    }
    {   // 5.01.14: the launcher's bottom bar: status left, the green BROWSE SERVERS (= CONNECT) button right
        const float by0 = H - 64 * u;
        Quad(0, by0, W, H, NC(12, 14, 18, 205));
        Quad(0, by0, W, by0 + 1 * u, LP_LINE());
        const float ax = W - L - 300 * u, ay = by0 + 10 * u, bx = W - L + 14 * u, bby = H - 10 * u;
        const bool focus = s_nNeonSel == 7 && NeonPadUI();
        if(NeonPBtn("BROWSE SERVERS", ax, ay, bx, bby, u, focus, 1) && !dlg && !tapUsed) { tapUsed = true; activate(7); }
        const float hy = by0 + 16 * u;
        NeonPText(F_CAPS, "NOT CONNECTED", L - 8 * u, hy, 13 * u, LP_TEXT(), 1.2f * u);
        char v[64]; snprintf(v, sizeof(v), "V%s - SERVERS ON THE INTERNET AND YOUR WI-FI", MP_VERSION_STR);
        NeonPText(F_UI, v, L - 8 * u, hy + 19 * u, 11.5f * u, LP_DIM(), 1.0f * u);
        float hx = W * 0.42f;
        if(NeonPadUI())
        {   // 5.01.12: SELECT / A CONFIRM only while a controller is in use (touch = no pad hints)
            Img(T("arrow_down.png"), hx, hy + 6 * u, hx + 9 * u, hy + 15 * u, LP_DIM());
            NeonPText(F_UI, "SELECT", hx + 16 * u, hy + 4 * u, 12 * u, LP_DIM(), 2.0f * u);
            const float ex = hx + 84 * u;
            NeonPFrame(ex, hy + 1 * u, ex + 18 * u, hy + 18 * u, 1.1f * u, LP_DIM());
            NeonPText(F_UI, "A", ex + 5 * u, hy + 4 * u, 12 * u, LP_TEXT());
            NeonPText(F_UI, "CONFIRM", ex + 26 * u, hy + 4 * u, 12 * u, LP_DIM(), 2.0f * u);
        }
        const char* ln = "ENGLISH";
        {   // the atlas is ASCII: other languages show their code
            static char up[16]; const char* c = settings.uiLanguage[0] ? settings.uiLanguage : "en";
            if(strcasecmp(c, "en")) { NeonUpper(up, sizeof(up), c); ln = up; }
        }
        const float lw = TextW(F_UI, ln, 12 * u, 1.6f * u);
        NeonPText(F_UI, ln, W - L - 8 * u - lw, 20 * u, 12 * u, LP_DIM(), 1.6f * u);   // top bar, right
    }
    if(dlg) NeonPDialog(W, H, u);
}
// 5.01.14: the loading cover (paint 4): from the join tap until the loading screen, and over the pause menu after it
static void NeonPLoading(float W, float H)
{
    using namespace NeonPaint;
    const float u = H / 804.0f;
    const float L = fminf(W * 0.07f, 100 * u * (W / H) / (1430.0f / 804.0f));
    NeonPBackdrop(W, H);
    if(const Tex* lg = T("logo.png"))
    {
        const float w = fminf(W * 0.46f, 660 * u), h = w * (601.0f / 1920.0f);
        Img(lg, (W - w) * 0.5f, H * 0.40f - h * 0.5f, (W + w) * 0.5f, H * 0.40f + h * 0.5f);
    }
    const uint64_t now = MP_NowMs();
    char m[48]; snprintf(m, sizeof(m), "CONNECTING TO MULTIPLAYER%s", (now / 400) % 4 == 0 ? "" : (now / 400) % 4 == 1 ? "." : (now / 400) % 4 == 2 ? ".." : "...");
    const float fs = 16 * u, ty = H - 64 * u;
    NeonPText(F_CAPS, m, W - L - TextW(F_CAPS, "CONNECTING TO MULTIPLAYER...", fs, 1.4f * u), ty, fs, LP_TEXT(), 1.4f * u, true);
    {   // a moving bar (the launcher's busy line)
        const float bx0 = L - 8 * u, bx1 = W - L + 8 * u, by = H - 30 * u;
        Quad(bx0, by, bx1, by + 3 * u, NC(255, 255, 255, 36));
        const float f = (float)(now % 1600) / 1600.0f, seg = (bx1 - bx0) * 0.22f;
        const float a = bx0 + (bx1 - bx0 + seg) * f - seg;
        Quad(fmaxf(a, bx0), by, fminf(a + seg, bx1), by + 3 * u, LP_BLUE());
    }
    NeonPText(F_CAPS, "GAMEPLAY TIP", L - 8 * u, ty, 12 * u, LP_RED(), 1.6f * u);
    NeonPText(F_UI, "Find a world and join the streets of San Andreas.", L - 8 * u, ty + 17 * u, 13 * u, LP_DIM());
    EatTap();
}
static void NeonPPill(float W, float H)
{   // the game's own menu (Game settings): the way back to the neon menu
    using namespace NeonPaint;
    const float u = H / 804.0f, w = 230 * u, h = 46 * u, x0 = W * 0.02f, y0 = H - h - 20 * u;
    Quad(x0, y0, x0 + w, y0 + h, Held(x0, y0, x0 + w, y0 + h) ? NC(77, 87, 99, 230) : NC(26, 29, 34, 225));   // 5.01.14
    NeonPFrame(x0, y0, x0 + w, y0 + h, 1.0f * u, LP_LINE());
    Quad(x0, y0, x0 + 3 * u, y0 + h, LP_BLUE());
    NeonPText(F_CAPS, "MP MENU", x0 + 22 * u, y0 + (h - 15 * u) * 0.5f, 15 * u, LP_TEXT(), 1.6f * u);
    if(Tap(x0, y0, x0 + w, y0 + h)) s_bNeonHidden = false;
}
static void NeonPTile(float W, float H)
{   // ServerBrowserMode = mp: the [Multiplayer] tile on the game's title menu, bottom left
    using namespace NeonPaint;
    const float w = W * 0.11f, h = H * 0.17f, x0 = W * 0.13f, y0 = H * 0.70f;
    const bool held = Held(x0, y0, x0 + w, y0 + h);
    Quad(x0, y0, x0 + w, y0 + h, held ? NC(60, 60, 60, 230) : NC(12, 12, 12, 220));
    NeonPFrame(x0, y0, x0 + w, y0 + h, fmaxf(2.0f, H * 0.004f), NC(200, 200, 200, 230));
    const float d = h * 0.34f;
    Img(T("discord.png"), x0 + (w - d) * 0.5f, y0 + h * 0.18f, x0 + (w + d) * 0.5f, y0 + h * 0.18f + d);
    const float fs = h * 0.17f, tw = TextW(F_UI, "Multiplayer", fs);
    NeonPText(F_UI, "Multiplayer", x0 + (w - tw) * 0.5f, y0 + h * 0.66f, fs, NC(220, 220, 220));
    if(Tap(x0, y0, x0 + w, y0 + h)) s_nNeonAct = NA_MPTILE;
}
// render thread, after the game's MobileMenu::Render
void MP::NeonPaintMenu(int what)
{
    if(!NeonPaint::Ready() || what <= 0) return;
    const float W = NeonPaint::W(), H = NeonPaint::H();
    if(s_nNeonPaintFrames < 3) logger->Info("Neon menu: native frame %d (%s, %.0fx%.0f)", s_nNeonPaintFrames + 1, what == 1 ? "neon title" : what == 2 ? "mp tile" : what == 4 ? "loading cover" : "pill", W, H);
    NeonPaint::FrameInput();
    NeonPaint::Begin();
    s_nNK = s_nNeonKeysQ.exchange(0);   // 5.01.11: the controller keys since the last paint
    if(what == 1)
    {
        s_nNeonTitleMs = MP_NowMs();
        if(s_nNT == NT_BROWSE) NeonPBrowse(W, H);
        else if(s_nNT == NT_SETTINGS) NeonPSettings(W, H);
        else NeonPTitle(W, H);
        NeonPToast(W, H, H / 804.0f);
    }
    else if(what == 2) NeonPTile(W, H);
    else if(what == 4) NeonPLoading(W, H);
    else { if(s_nNK & (NK_A | NK_START)) s_bNeonHidden = false; NeonPPill(W, H); }
    s_nNK = 0;
    NeonPaint::End();
    if(s_nNeonPaintFrames < 3 && ++s_nNeonPaintFrames == 3) logger->Info("Neon menu: native frames ok");
}
void MP::NeonTouch(int kind, int finger, float x, float y)   // 4.13.30
{
    if(s_nNeonMode == NEON_MODE_DEFAULT) return;
    if(kind == 1) s_nNeonPadAt = 0;   // 5.01.12: a finger on the screen = touch mode: the controller hints and focus frames go
    NeonPaint::OnTouch(kind, finger, x, y);
}
// game thread (MobileMenu::Update)
void MP::NeonMenuTick()
{
    NeonFileTick(MP_NowMs());   // 5.01.07: neon_mode.txt added / removed while the title menu is up
    if(s_nNeonMode == NEON_MODE_DEFAULT) { NeonMusicStop(); return; }   // 4.13.30
    const uint64_t now = MP_NowMs();
    const bool titleUp = s_nNeonTitleMs && now - s_nNeonTitleMs < 600 && NeonTitleCtx();
    // 5.01.11: the menu music while the neon title menu is up; notify.mp3 for the Discord pop-up
    NeonMusicTick(titleUp);
    if(titleUp) NotifySoundFetch();
    if(s_bNeonToastSound && titleUp)
    {
        if(s_NfSnd.load() == 2) { Sound::PlayMem(0, 0.9f); s_bNeonToastSound = false; }
        else if(s_nNeonToastAt && now - s_nNeonToastAt > 4500) s_bNeonToastSound = false;   // the pop-up is gone, no late sound
    }
    if(titleUp && s_nNT == NT_BROWSE && now - s_nNTBuiltAt > 1000)
    {   // the server rows of the neon Browse servers page (the launcher's data: lobby, server list, favourites)
        s_nNTBuiltAt = now;
        NeonBuild(s_NTRows);
    }
    if(titleUp && s_nNT == NT_SETTINGS && now - s_nNSBuiltAt > 1500) { s_nNSBuiltAt = now; NeonNSBuild(); }
    if(!titleUp && s_nNT != NT_MAIN && !NeonTitleCtx()) s_nNT = NT_MAIN;
    {
        const int j = s_nNTJoinReq.exchange(-1);
        if(j >= 0 && NeonTitleCtx())
        {
            logger->Info("Neon menu: Browse servers -> join %s", s_NTJoin.name);
            s_nNT = NT_MAIN; NeonMusicStop();
            NeonJoinRow(s_NTJoin);
        }
    }
    if(s_bNSGameOpts.exchange(false)) s_nNeonAct = NA_SETTINGS;
    const int a = s_nNeonAct.exchange(NA_NONE);
    if(a == NA_NONE) return;
    if(a == NA_RESUME || a == NA_NEWGAME || a == NA_MPTILE || a == NA_QUIT) NeonMusicStop();
    switch(a)
    {
        case NA_RESUME:  NeonResume(); break;
        case NA_NEWGAME: NeonNewGame(); break;
        case NA_BROWSE:  if(NeonTitleCtx()) NeonTOpenBrowse(); break;
        case NA_QUICK:
            if(NeonTitleCtx())
            {   // 5.01.11: the best server (featured first, the most players) is joined: the Multiplayer load, then the join
                LRow r;
                if(NeonBestRow(r)) { logger->Info("Neon menu: Quick connect -> %s", r.name); NeonMusicStop(); NeonJoinRow(r); }
                else { logger->Info("Neon menu: Quick connect -> no server yet, Browse servers"); NeonTOpenBrowse(); }
            }
            break;
        case NA_MPTILE:  MenuMpTap(nullptr); break;
        case NA_DISCORD: logger->Info("Neon menu: Neon identity -> Discord"); Voice::OpenUrl(MP_AUTH_DISCORD_URL); break;
        case NA_SIGNOUT:
        {
            std::vector<std::string> acc = AuthAccounts();
            const std::string who = !s_AuthUser.empty() ? s_AuthUser : (!acc.empty() ? acc[0] : std::string());
            if(!who.empty()) AuthAccountsRemove(who);
            AuthLogout(nullptr, false);
            s_bNeonToastShown = false; s_NeonWhoCache.clear(); s_nNeonWhoAt = 0;   // 5.01.12: a new link shows the pop-up again
            logger->Info("Neon menu: signed out");
            break;
        }
        case NA_SETTINGS:
        {
            if(!s_bOnSettingsLooked) { s_bOnSettingsLooked = true; *(void**)&s_fnOnSettings = (void*)aml->GetSym(aml->GetLibHandle("libGTASA.so"), "_ZN14MainMenuScreen10OnSettingsEv"); }
            if(s_fnOnSettings) { logger->Info("Neon menu: Settings > GAME OPTIONS -> the game's own settings"); s_fnOnSettings(); }
            else { logger->Info("Neon menu: Settings -> no MainMenuScreen::OnSettings, the game's menu is shown"); s_bNeonHidden = true; }
            break;
        }
        case NA_QUIT: logger->Info("Neon menu: Quit game"); MP::SaveConfig(); exit(0); break;
    }
}

// 5.01.08: files/TYFMC/MP/neon_mode_status.txt - what MP did with neon_mode.txt, readable in a file manager (no log
// digging): where the file was found, whether the title hook is on, the textures, why it is (not) painted
static void NeonStatusWrite(bool force)
{
    static uint64_t s_at = 0; static std::string s_last;
    const char* dd = MP::DataDir();
    if(!dd || !dd[0]) return;
    const uint64_t now = MP_NowMs();
    if(!force && now - s_at < 4000) return;
    s_at = now;
    if(!s_bNeonFile && s_last.empty() && !force) return;   // never had the file: no report
    char b[2048];
    const char* err = NeonPaint::LastError();
    snprintf(b, sizeof(b),
        "MP %s - neon_mode.txt status (MP rewrites this file, delete it any time)\n"
        "neon_mode.txt   : %s%s\n"
        "menu mode       : %s\n"
        "title hook      : %s\n"
        "drawing         : %s\n"
        "textures        : %s%s%s\n"
        "title frames    : %d checked, %d painted\n"
        "now             : %s\n",
        MP_VERSION_STR,
        s_bNeonFile ? "found at " : "not found (put it in files/TYFMC/MP/)", s_bNeonFile ? s_szNeonFileAt.c_str() : "",
        s_nNeonMode == NEON_MODE_NEON ? "neon" : (s_nNeonMode == NEON_MODE_DEFAULT ? "default" : "mp"),
        s_bTitleHookOn ? "on" : "OFF (MobileMenu::Render could not be hooked - mp_log.txt \"Main menu:\" lines say why)",
        NeonPaint::Inited() ? "ok" : "game functions missing",
        NeonPaint::Ready() ? "loaded" : (NeonPaint::Failed() ? "FAILED" : "not loaded yet"),
        err && *err ? " - missing / unreadable: " : "", err ? err : "",
        s_nNeonWantCalls, s_nNeonPaintFrames, s_szNeonWhy);
    if(s_last == b) return;
    s_last = b;
    char p[600]; snprintf(p, sizeof(p), "%s/neon_mode_status.txt", dd);
    if(FILE* f = fopen(p, "wb")) { fputs(b, f); fclose(f); }
}
