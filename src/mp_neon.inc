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
// 5.01.15: the accent outlines are blue (were gold); the rest is the original neon look again
static const ImU32 kNGold = IM_COL32(88, 101, 242, 255), kNGoldDim = IM_COL32(64, 74, 180, 255);
static const ImU32 kNGrey = IM_COL32(141, 143, 149, 255), kNText = IM_COL32(226, 228, 232, 255);
static const ImU32 kNPanel = IM_COL32(14, 15, 18, 236), kNLine = IM_COL32(38, 40, 45, 255);
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
        dl->AddRect(a, b, IM_COL32(88, 101, 242, 255), 5 * u, 0, 1.6f * u);
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
        dl->AddRectFilled(a, b, IM_COL32(10, 10, 12, 240), 5 * u); dl->AddRect(a, b, IM_COL32(88, 101, 242, 255), 5 * u, 0, 1.6f * u);
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
// 5.02.01: Resume / New game / a save slot from the neon menu: after the game's loading screen the game put you on its
// pause menu (the load runs from the menu, and the menu comes back when it is done). Like the Multiplayer load
// (MenuMpMenuTick), the game's menu is now closed for you once the game is loaded: you land in the game.
static std::atomic<bool>     s_bNeonCloseMenu{false};
static std::atomic<uint64_t> s_nNeonCloseReqAt{0};
static uint64_t              s_nNeonPedSeenAt = 0;
static void NeonCloseAfterLoad() { s_nNeonPedSeenAt = 0; s_nNeonCloseReqAt = MP_NowMs(); s_bNeonCloseMenu = true; }
static void NeonCloseTick(uint64_t now)
{   // game thread (MobileMenu::Update)
    if(!s_bNeonCloseMenu.load()) return;
    if(now - s_nNeonCloseReqAt.load() > 180000) { s_bNeonCloseMenu = false; logger->Info("Neon menu: no game after 3 min - menu left alone"); return; }
    if(Game::IsLoadPending() || !Game::HasPlayerPed() || Game::LocalPed() <= 0) { s_nNeonPedSeenAt = 0; return; }   // still loading
    if(!s_nNeonPedSeenAt) { s_nNeonPedSeenAt = now; return; }
    if(now - s_nNeonPedSeenAt < 350) return;                     // the loading screen's fade
    if(!Game::IsFrontendMenu()) { s_bNeonCloseMenu = false; logger->Info("Neon menu: loaded straight into the game"); return; }
    s_bNeonCloseMenu = false;
    const bool ok = Game::ExitFrontendMenu();
    logger->Info("Neon menu: game loaded -> pause menu closed (%s)", ok ? "ok" : "no MobileMenu::Exit");
}
static void NeonResume()
{
    if(!NeonTitleCtx()) { s_bMenuMpExit = true; return; }   // the black screen: the game is loaded - back to it
    const int slot = Game::NewestSaveSlot();
    const bool ok = slot >= 0 ? Game::RequestLoadSlot(slot) : Game::RequestNewGame();
    if(ok) NeonCloseAfterLoad();
    logger->Info("Neon menu: Resume -> %s %d (%s)", slot >= 0 ? "load slot" : "new game", slot + 1, ok ? "ok" : "failed");
}
static void NeonNewGame()
{
    if(!NeonTitleCtx()) s_bMenuMpExit = true;
    const bool ok = Game::RequestNewGame();
    if(ok) NeonCloseAfterLoad();
    logger->Info("Neon menu: New game (%s)", ok ? "ok" : "failed");
}
static void NeonLoadSlot(int slot)
{
    if(!NeonTitleCtx()) return;
    const bool ok = Game::RequestLoadSlot(slot);
    if(ok) NeonCloseAfterLoad();
    logger->Info("Neon menu: Start game -> load slot %d (%s)", slot + 1, ok ? "ok" : "failed");
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
        if(on) NeonTri(dl, ImVec2(ix - 14 * u, y + step * 0.5f), 3.6f * u, IM_COL32(160, 170, 255, 255));
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
        dl->AddRect(a, b, IM_COL32(48, 54, 100, 255), 2 * u, 0, 1.0f * u);
        dl->AddRectFilled(ImVec2(a.x, a.y), ImVec2(a.x + 2.5f * u, b.y), IM_COL32(88, 101, 242, 255));
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
// 5.01.16: the blue copies of the gold-framed images (nb_button / nb_card / nb_dialog / nb_pill), the old ones as a fallback
static const NeonPaint::Tex* NeonPTex(const char* blue, const char* old) { const NeonPaint::Tex* t = NeonPaint::T(blue); return t ? t : NeonPaint::T(old); }
static void NeonPBackdrop(float W, float H)
{
    using namespace NeonPaint;
    Quad(0, 0, W, H, NC(0, 0, 0));
    if(const Tex* bg = NeonPTex("nb_bg1080.png", "nb_bg.png"))   // 5.01.16: 1080p (1920x1080 upscale, stored 2048x1024)
    {   // 5.01.15: the new background (Screenshots/images (15).jpeg, the Las Vegas night), cover-fitted (739 x 415)
        const float ar = 739.0f / 415.0f;
        float w = W, h = W / ar; if(h < H) { h = H; w = H * ar; }
        const float x0 = (W - w) * 0.5f, y0 = (H - h) * 0.5f;
        Img(bg, x0, y0, x0 + w, y0 + h);
        // 5.01.16: a soft vignette instead of the left shade: shade_left.png ends at alpha 60, so stretched over half
        // the screen it left a hard black half with an edge in the middle. Now the edges fade to dark all around
        // (smooth, nothing at the centre), the left side a bit more for the menu text.
        const int N = 32;
        for(int i = 0; i < N; ++i)
        {   // left / right: thin strips, the alpha falls off smoothly (quadratic) to 0
            const float f0 = (float)i / N, f1 = (float)(i + 1) / N, k = 1.0f - (f0 + f1) * 0.5f, a = k * k;
            const float wl = W * 0.34f, wr = W * 0.24f;
            Quad(wl * f0, 0, wl * f1, H, NC(0, 0, 0, (int)(215 * a)));
            Quad(W - wr * f1, 0, W - wr * f0, H, NC(0, 0, 0, (int)(170 * a)));
        }
        if(const Tex* wt = T("white.png"))
        {   // top / bottom
            ImgUV(wt, 0, 0, W, H * 0.24f, 0, 0, 1, 1, NC(0, 0, 0, 175), NC(0, 0, 0, 0));
            ImgUV(wt, 0, H * 0.70f, W, H, 0, 0, 1, 1, NC(0, 0, 0, 0), NC(0, 0, 0, 205));
        }
        return;
    }
    else if(const Tex* a = T("art.png"))
    {
        const float iw = H * (1131.0f / 707.0f), x0 = fmaxf(W - iw, W * 0.30f);
        Img(a, x0, 0, x0 + iw, H);
    }
    Img(T("shade_bottom.png"), 0, H * 0.78f, W, H);
    Img(T("shade_top.png"), 0, 0, W, H * 0.16f);
}
static bool NeonPButton(const char* label, float x0, float y0, float x1, float y1, float u)
{
    using namespace NeonPaint;
    const bool held = Held(x0, y0, x1, y1);
    Img(NeonPTex("nb_button.png", "button.png"), x0, y0, x1, y1, held ? NC(205, 212, 255) : NC(255, 255, 255));
    NeonPFrame(x0, y0, x1, y1, 1.3f * u, NC(88, 101, 242));   // 5.01.16: every neon button (BACK TO MAIN MENU too) has a blue outline
    const float fs = 13 * u, w = TextW(F_CAPS, label, fs, 1.6f * u);
    NeonPText(F_CAPS, label, (x0 + x1 - w) * 0.5f, (y0 + y1 - fs) * 0.5f, fs, NC(226, 228, 232), 1.6f * u);
    return Tap(x0, y0, x1, y1);
}
// ================================================================================================
// 5.01.11: the neon title menu's own pages, painted natively like the main menu (no ImGui on the title):
//   Browse servers  - the server list (all / featured / favourites / Wi-Fi), join = the Multiplayer load + the join
//   Settings        - PCMenu's PC settings: Controls / Game / Display / Audio / Mods (SAUtils) / Multiplayer, the
//                     game's settings table read and written directly (game/mobilesettings.cpp)
// + full controller support (D-pad / left stick, A, B, LB / RB, X / Y), the Discord connected pop-up (slides in from the
// right with notify.mp3, slides back out) and the menu music (Host/Media/neon_mainmenu.mp3, loops while the menu is up)
// ================================================================================================
enum { NT_MAIN = 0, NT_BROWSE, NT_SETTINGS, NT_START };   // 5.02.01: + Start game (New game / save slots)
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
    // 5.01.14: the notification card: dark glass, the blue strip left + a blue outline (kept in 5.01.15)
    Quad(x0, y0, x0 + w, y0 + h, NC(16, 18, 22, 236));
    NeonPFrame(x0, y0, x0 + w, y0 + h, 1.0f * u, NC(88, 101, 242, 120));
    Quad(x0, y0, x0 + 4 * u, y0 + h, NC(88, 101, 242));
    Img(T("discord.png"), x0 + 18 * u, y0 + 16 * u, x0 + 60 * u, y0 + 58 * u);
    NeonPText(F_CAPS, s_szNeonToast[0], x0 + 74 * u, y0 + 14 * u, 16 * u, NC(226, 228, 232), 1.0f * u);
    NeonPText(F_UI, s_szNeonToast[1], x0 + 74 * u, y0 + 42 * u, 12.5f * u, NC(150, 152, 158));
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
// 5.01.16: why it still stopped after ~3 s: the download (HttpsFetch, 60 s timeout) kept whatever had arrived when the
// time ran out - on a slow connection the first few hundred KB = a few seconds of the 5:46 track - and saved it as
// neon_mainmenu.mp3. Every start then played those seconds and "ended". Now: a cut download is never saved (the body
// must match its Content-Length, 10 min to download, >= 1 MB), a finished download writes neon_mainmenu.mp3.ok (its
// size), a copy without a matching .ok is downloaded again (it still plays meanwhile), and a file that ends in under
// 30 s is put aside (.bad) - the URL stream plays until the new copy is in.
static std::set<std::string> s_NeonMusBad;    // files that ended early this run
static long NeonFileSize(const char* p) { struct stat st; return stat(p, &st) == 0 ? (long)st.st_size : -1; }
static bool NeonMusicVerified()
{   // the downloaded copy is complete (its .ok has the same size)
    char p[600], q[620]; snprintf(p, sizeof(p), "%s/" NEON_MUSIC_FILE, MP::DataDir()); snprintf(q, sizeof(q), "%s.ok", p);
    const long sz = NeonFileSize(p); if(sz <= 0) return false;
    long want = -1; if(FILE* f = fopen(q, "r")) { if(fscanf(f, "%ld", &want) != 1) want = -1; fclose(f); }
    return want == sz;
}
static bool NeonMusicLocal(char* out, size_t n)
{   // files/TYFMC/MP/neon_mainmenu.mp3 (the user's own copy, or the one MP downloaded); "neon_mainmene.mp3" too
    static const char* const kNames[] = { NEON_MUSIC_FILE, "neon_mainmene.mp3", "neon_menu.mp3" };
    for(const char* nm : kNames)
    {
        snprintf(out, n, "%s/%s", MP::DataDir(), nm);
        if(access(out, R_OK) != 0 || s_NeonMusBad.count(out)) continue;
        if(NeonFileSize(out) < 300 * 1024) continue;   // a few seconds at most: a broken copy
        return true;
    }
    return false;
}
static void NeonMusicPutAside(const char* path)
{   // a copy that ended in seconds: not played again this run; MP's own copy is renamed and downloaded again
    s_NeonMusBad.insert(path);
    char own[600]; snprintf(own, sizeof(own), "%s/" NEON_MUSIC_FILE, MP::DataDir());
    if(!strcmp(path, own))
    {
        char bad[620]; snprintf(bad, sizeof(bad), "%s.bad", own); rename(own, bad);
        char ok[620]; snprintf(ok, sizeof(ok), "%s.ok", own); remove(ok);
        s_NeonMusBad.erase(path);
        if(s_nNeonMusDl.load() != 1) s_nNeonMusDl = 0;   // download it again
    }
    logger->Info("Neon menu: menu music %s is cut short - put aside", path);
}
static void NeonMusicDownload()
{   // once: the track into files/TYFMC/MP so the next start plays it from the disk (no streaming)
    int z = 0; if(!s_nNeonMusDl.compare_exchange_strong(z, 1)) { z = 3; if(!s_nNeonMusDl.compare_exchange_strong(z, 1)) return; }
    std::string dir = MP::DataDir();
    std::thread([dir]() {
        static const char* const kUrls[] = { NEON_MUSIC_URL1, NEON_MUSIC_URL2 };
        for(const char* u : kUrls)
        {
            std::string body, why;
            const bool ok = Online::HttpsFetch(u, body, 600000, why, 40u << 20);
            const bool cut = Online::HttpsLastShort();
            if(ok && !cut && body.size() >= (1u << 20))
            {
                const std::string dst = dir + "/" NEON_MUSIC_FILE, tmp = dst + ".part";
                bool w = false;
                if(FILE* f = fopen(tmp.c_str(), "wb")) { w = fwrite(body.data(), 1, body.size(), f) == body.size(); w = (fclose(f) == 0) && w; }
                if(w && rename(tmp.c_str(), dst.c_str()) == 0)
                {
                    if(FILE* f = fopen((dst + ".ok").c_str(), "w")) { fprintf(f, "%ld\n", (long)body.size()); fclose(f); }
                    logger->Info("Neon menu: menu music cached (%u bytes from %s)", (unsigned)body.size(), u);
                    s_nNeonMusDl = 2; return;
                }
                remove(tmp.c_str());
                why = "could not write the file (storage full?)";
            }
            else if(ok) why = cut ? "the download was cut off" : "too small";
            logger->Info("Neon menu: menu music download from %s failed (%s, %u bytes)", u, why.c_str(), (unsigned)body.size());
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
static float s_fNeonMusMax = 0.0f;   // 5.01.15: the furthest position heard (a track that "ends" early is resumed, not looped)
static uint64_t s_nNeonMusLastPos = 0;
static char s_szNeonMusPath[600] = "";   // 5.01.16: the file playing ("" = the URL stream)
static void NeonMusicTick(bool want)   // game thread
{
    // 5.01.15: the track (5:46) was cut after a few seconds and started again: the 5.01.14 "silent" check restarted it
    // whenever the position did not move for 4 s (the Android decoder / URL player reports 0 for a while), and a URL
    // player that stalls while buffering reports "end of track", which looped it from 0:00. Now a running track is
    // never restarted because of its position; an early "end" (before the real length) is a stall - the downloaded
    // copy (or the URL again) takes over; only a real end of the track loops it; a failed start is tried again.
    if(s_nNeonMode == NEON_MODE_NEON && settings.neonMusic && s_nNeonMusDl.load() == 0)
    {   // once: a copy on the disk already, or download it now (before the first start needs it)
        if(NeonMusicVerified()) s_nNeonMusDl = 2; else NeonMusicDownload();   // 5.01.16: an unverified copy is downloaded again
    }
    want = want && settings.neonMusic && settings.neonMusicVol > 0;
    if(!want) { NeonMusicStop(); s_bNeonMusLoop = false; s_fNeonMusMax = 0.0f; return; }
    const uint64_t now = MP_NowMs();
    if(s_nNeonMus == -1 && now - s_nNeonMusAt < 3000) return;
    if(s_nNeonMus == 1)
    {
        Sound::StreamUpdate();
        const bool active = Sound::StreamActive(), ended = Sound::StreamEnded();
        const float pos = Sound::StreamPosition(), dur = Sound::StreamDuration();
        if(pos > s_fNeonMusMax) { s_fNeonMusMax = pos; s_nNeonMusLastPos = now; }
        char p[600];
        if(ended || (!active && now - s_nNeonMusAt > 3000))
        {
            const bool full = ended && s_fNeonMusMax > 30.0f && (dur <= 0.0f || s_fNeonMusMax >= dur - 10.0f) && !s_bNeonMusUrl;
            if(ended && !s_bNeonMusUrl && s_fNeonMusMax < 30.0f && s_szNeonMusPath[0]) NeonMusicPutAside(s_szNeonMusPath);   // 5.01.16
            logger->Info("Neon menu: menu music %s at %.1f / %.1f s (%s)", full ? "reached the end - looping" : "stopped early - starting again",
                         s_fNeonMusMax, dur, Sound::StreamInfo());
            Sound::StreamStop(); s_nNeonMus = 0;
            s_bNeonMusLoop = full;
            if(full) s_fNeonMusMax = 0.0f;
            if(!full && !ended) { s_nNeonMus = -1; s_nNeonMusAt = now; return; }   // a failed start: wait 3 s
        }
        else if(s_bNeonMusUrl && s_nNeonMusDl.load() == 2 && (pos < 1.0f || now - s_nNeonMusLastPos > 3000) && NeonMusicLocal(p, sizeof(p)))
        {   // still connecting to the URL (or it stalled), and the downloaded copy is here: play that
            logger->Info("Neon menu: menu music -> the downloaded copy");
            Sound::StreamStop(); s_nNeonMus = 0; s_bNeonMusLoop = false; s_fNeonMusMax = 0.0f;
        }
    }
    if(s_nNeonMus != 1)
    {
        char p[600]; bool ok = false; const bool local = NeonMusicLocal(p, sizeof(p));
        if(local) ok = Sound::StreamPlay(p, s_bNeonMusLoop ? settings.neonMusicVol / 100.0f : 0.0f);
        if(local && !ok) s_NeonMusBad.insert(p);   // 5.01.16: unreadable: the next start takes another copy / the URL
        MP_CopyStr(s_szNeonMusPath, ok && local ? p : "", sizeof(s_szNeonMusPath));
        s_bNeonMusUrl = false;
        if(!ok) { ok = Sound::StreamPlayUrl(NEON_MUSIC_URL1, 0.0f); s_bNeonMusUrl = ok; NeonMusicDownload(); }
        s_nNeonMus = ok ? 1 : -1; s_nNeonMusAt = now; s_nNeonMusLastPos = now;
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
{
    using namespace NeonPaint;
    const float fs = 50 * u, px = L, py = 34 * u;
    for(int k = 0; k < 4; ++k) Text(F_GOTHIC, title, px + ((k & 1) ? 1.6f : -1.6f) * u, py + ((k & 2) ? 1.6f : -1.6f) * u, fs, NC(10, 12, 18, 230));
    Text(F_GOTHIC, title, px, py, fs, NC(240, 243, 250), NC(150, 160, 185));
    if(sub) NeonPText(F_CAPS, sub, L + 4 * u, py + 62 * u, 11 * u, NC(150, 152, 158), 1.6f * u);
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
    NeonPaint::Quad(x0, y0, x1, y1, NC(8, 9, 12, 222));
    NeonPFrame(x0, y0, x1, y1, 1.1f * u, NC(40, 46, 96));
}
static bool NeonPBtn(const char* label, float x0, float y0, float x1, float y1, float u, bool focus)
{
    const bool t = NeonPButton(label, x0, y0, x1, y1, u);
    if(focus) NeonPFrame(x0 - 2 * u, y0 - 2 * u, x1 + 2 * u, y1 + 2 * u, 1.6f * u, NC(88, 101, 242));
    return t;
}

// ---- Browse servers ----
// 5.01.15: the reference layout (docs/media/neon-server-browser.jpg): "Choose your server", the list tools (list, refresh,
// search = paste a server link, CONNECT, FILTERS), rows with flags / favourite star / players / ping, and on the right
// the SELECTED SERVER card (description, server link + COPY LINK, players / ping / mode, regions & languages, JOIN
// SERVER). The outlines are blue (the notification card's blue), not gold.
static std::vector<LRow> s_NTRows;
static uint64_t s_nNTBuiltAt = 0, s_nNTOpenAt = 0, s_nNTTapAt = 0, s_nNTCopiedAt = 0;
static int   s_nNTSel = 0, s_nNTFocus = 0;     // focus: 0 the list, 1 BACK, 2 REFRESH, 3 JOIN
static int   s_nNTFilter = 0;                  // 0 all, 1 with players, 2 not full, 3 low ping
static bool  s_bNTPasteWait = false;
static float s_fNTScroll = 0;
static std::atomic<int> s_nNTJoinReq{-1};      // a row to join (game thread)
static LRow  s_NTJoin;
static LRow  s_NTFav; static std::atomic<int> s_nNTFavReq{-1};   // a favourite star tapped (game thread: favorites.txt)
static void NeonTOpenBrowse() { s_nNT = NT_BROWSE; s_nNTBuiltAt = 0; s_nNTOpenAt = MP_NowMs(); s_nNTSel = 0; s_fNTScroll = 0; s_nNTFocus = 0; s_szNeonSearch[0] = 0; }
static NeonPaint::Col NB_BLUE(int a = 255) { return NC(88, 101, 242, a); }
static NeonPaint::Col NB_BLUE_LT() { return NC(160, 170, 255); }
static void NeonPFlag(int ci, float x0, float y0, float x1, float y1)
{   // nb_flags.png = assets/MP/flags.png (64 x 48 cells, 16 a row, slot 0 = unknown, slot i = kCountries[i - 1])
    using namespace NeonPaint;
    if(ci == kCountryGlobal)
    {
        Quad(x0, y0, x1, y1, NC(26, 86, 168));
        const float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f, r = (y1 - y0) * 0.34f, t = fmaxf(1.0f, (y1 - y0) * 0.07f);
        NeonPFrame(cx - r, cy - r, cx + r, cy + r, t, NC(255, 255, 255, 230));
        Quad(cx - r, cy - t * 0.5f, cx + r, cy + t * 0.5f, NC(255, 255, 255, 230)); Quad(cx - t * 0.5f, cy - r, cx + t * 0.5f, cy + r, NC(255, 255, 255, 230));
    }
    else if(const Tex* f = T("nb_flags.png"))
    {
        const int slot = ci + 1, cx = slot % MP_FLAG_COLS, cy = slot / MP_FLAG_COLS;
        const float fx = (float)MP_FLAG_CELL_W / MP_FLAG_ATLAS, fy = (float)MP_FLAG_CELL_H / MP_FLAG_ATLAS;
        ImgUV(f, x0, y0, x1, y1, cx * fx, cy * fy, (cx + 1) * fx, (cy + 1) * fy, NC(255, 255, 255), NC(255, 255, 255));
    }
    else Quad(x0, y0, x1, y1, NC(90, 95, 102));
    NeonPFrame(x0, y0, x1, y1, 1.0f, NC(0, 0, 0, 140));
}
static void NeonPIni(const LRow& r, float x0, float y0, float sz)
{   // the initials tile (coloured from the name)
    using namespace NeonPaint;
    uint32_t hsh = 2166136261u; for(const char* p = r.name; *p; ++p) hsh = (hsh ^ (unsigned char)tolower((unsigned char)*p)) * 16777619u;
    static const NeonPaint::Col kc[5] = { {150, 52, 52, 255}, {52, 140, 72, 255}, {56, 92, 160, 255}, {160, 120, 50, 255}, {120, 70, 150, 255} };
    const NeonPaint::Col c = kc[hsh % 5];
    Quad(x0, y0, x0 + sz, y0 + sz, NC(16, 16, 19, 255)); NeonPFrame(x0, y0, x0 + sz, y0 + sz, fmaxf(1.0f, sz * 0.03f), c);
    char ini[4] = { 0 }; int k = 0;
    for(const char* p = r.name; *p && k < 2; ++p) if(isalnum((unsigned char)*p) && (p == r.name || !isalnum((unsigned char)p[-1]))) ini[k++] = (char)toupper((unsigned char)*p);
    if(!k) { ini[0] = 'M'; ini[1] = 'P'; }
    const float fs = sz * 0.36f, w = TextW(F_CAPS, ini, fs);
    NeonPText(F_CAPS, ini, x0 + (sz - w) * 0.5f, y0 + (sz - fs) * 0.5f, fs, c);
}
static void NeonPWrap(int f, const char* s, float x, float y, float px, float maxw, float lh, int maxLines, NeonPaint::Col c)
{   // word wrap, the last line ends with "..." when the text is longer
    std::string line, word; int lines = 0; const char* p = s ? s : "";
    auto flush = [&](bool last) {
        if(line.empty()) return;
        NeonPText(f, (last ? NeonFit(f, line.c_str(), px, maxw) : line).c_str(), x, y + lines * lh, px, c);
        ++lines; line.clear();
    };
    for(;; ++p)
    {
        if(*p && *p != ' ' && *p != '\n') { word += *p; continue; }
        if(!word.empty())
        {
            std::string t = line.empty() ? word : line + " " + word;
            if(NeonPaint::TextW(f, t.c_str(), px) <= maxw || line.empty()) line = t;
            else
            {
                if(lines == maxLines - 1) { line += " " + word; while(*p) { line += *p == '\n' ? ' ' : *p; ++p; } flush(true); return; }
                flush(false); line = word;
            }
            word.clear();
        }
        if(*p == '\n' && lines < maxLines - 1) flush(false);
        if(!*p) break;
        if(lines >= maxLines) return;
    }
    flush(true);
}
static bool NeonPPass(const LRow& r)
{
    switch(s_nNTFilter)
    {
        case 1: return r.players > 0;
        case 2: return r.maxp <= 0 || r.players < r.maxp;
        case 3: return r.ping >= 0 && r.ping < 150;
    }
    return true;
}
static bool NeonPChip(const char* label, float x0, float y0, float x1, float y1, float u, bool on, bool focus)
{   // a toolbar box: dark glass, a blue outline when on / focused
    using namespace NeonPaint;
    const bool held = Held(x0, y0, x1, y1);
    Quad(x0, y0, x1, y1, held ? NC(40, 46, 96, 235) : NC(10, 11, 16, 225));
    NeonPFrame(x0, y0, x1, y1, (on || focus) ? 1.6f * u : 1.1f * u, (on || focus) ? NB_BLUE() : NC(64, 68, 82));
    const float fs = 12 * u, w = TextW(F_CAPS, label, fs, 1.4f * u);
    NeonPText(F_CAPS, label, (x0 + x1 - w) * 0.5f, (y0 + y1 - fs) * 0.5f, fs, on ? NB_BLUE_LT() : NC(226, 228, 232), 1.4f * u);
    return Tap(x0, y0, x1, y1);
}
static void NeonPBrowse(float W, float H)
{
    using namespace NeonPaint;
    const float u = H / 804.0f;
    const float L = fminf(W * 0.07f, 100 * u * (W / H) / (1430.0f / 804.0f));
    NeonPBackdrop(W, H);
    const int K = s_nNK;
    // the rows shown (the filter) + totals
    std::vector<int> idx; idx.reserve(s_NTRows.size()); int playersOn = 0;
    for(int i = 0; i < (int)s_NTRows.size(); ++i) { if(s_NTRows[i].players > 0) playersOn += s_NTRows[i].players; if(NeonPPass(s_NTRows[i])) idx.push_back(i); }
    const int n = (int)idx.size();
    if(s_nNTSel >= n) s_nNTSel = n > 0 ? n - 1 : 0;
    bool doJoin = false, doBack = (K & NK_B) != 0, doRefresh = (K & (NK_X | NK_Y)) != 0, doConnect = false;
    // ---- the header: "MP NEON NETWORK" / "Choose your server" / the counts / BACK TO MAIN MENU
    NeonPText(F_CAPS, "MP NEON NETWORK", L + 2 * u, 30 * u, 12 * u, NB_BLUE_LT(), 3.0f * u, true);
    {
        const float fs = 52 * u, px = L, py = 46 * u; const char* t = "Choose your server";
        for(int k = 0; k < 4; ++k) Text(F_GOTHIC, t, px + ((k & 1) ? 1.6f : -1.6f) * u, py + ((k & 2) ? 1.6f : -1.6f) * u, fs, NC(10, 12, 18, 230));
        Text(F_GOTHIC, t, px, py, fs, NC(240, 243, 250), NC(150, 160, 185));
    }
    const float bbx1 = W - L + 14 * u, bbx0 = bbx1 - 236 * u;
    if(NeonPBtn("BACK TO MAIN MENU", bbx0, 40 * u, bbx1, 80 * u, u, s_nNTFocus == 1 && NeonPadUI())) doBack = true;
    {
        char a[48], b[48]; snprintf(a, sizeof(a), "%d SERVER%s", (int)s_NTRows.size(), s_NTRows.size() == 1 ? "" : "S");
        snprintf(b, sizeof(b), "%d PLAYER%s ONLINE", playersOn, playersOn == 1 ? "" : "S");
        const float fs = 12 * u, wb = TextW(F_CAPS, b, fs, 1.6f * u), wa = TextW(F_CAPS, a, fs, 1.6f * u);
        float x = bbx0 - 26 * u - wb;
        NeonPText(F_CAPS, b, x, 54 * u, fs, NC(200, 202, 208), 1.6f * u, true);
        Quad(x - 15 * u, 57 * u, x - 9 * u, 63 * u, NC(110, 200, 120));   // the online dot
        x -= 30 * u + wa;
        NeonPText(F_CAPS, a, x, 54 * u, fs, NC(200, 202, 208), 1.6f * u, true);
    }
    // ---- the tools: list / refresh / search (paste) / CONNECT / FILTERS
    static const char* const kTabs[] = { "ALL SERVERS", "FEATURED", "FAVOURITES", "WI-FI" };
    static const char* const kFilt[] = { "FILTERS", "WITH PLAYERS", "NOT FULL", "LOW PING" };
    if(K & NK_LB) { s_nNeonList = (s_nNeonList + 3) % 4; s_nNTBuiltAt = 0; s_nNTSel = 0; s_fNTScroll = 0; }
    if(K & NK_RB) { s_nNeonList = (s_nNeonList + 1) % 4; s_nNTBuiltAt = 0; s_nNTSel = 0; s_fNTScroll = 0; }
    const float ty0 = 116 * u, ty1 = 152 * u, lx0 = L - 14 * u, lx1 = fmaxf(W * 0.635f, lx0 + 560 * u), rx0 = lx1 + 18 * u, rx1 = W - L + 14 * u;
    {
        char lab[40]; snprintf(lab, sizeof(lab), "%s  v", kTabs[s_nNeonList]);
        if(NeonPChip(lab, lx0, ty0, lx0 + 190 * u, ty1, u, false, false)) { s_nNeonList = (s_nNeonList + 1) % 4; s_nNTBuiltAt = 0; s_nNTSel = 0; s_fNTScroll = 0; }
        if(NeonPChip("REFRESH", lx0 + 200 * u, ty0, lx0 + 310 * u, ty1, u, false, s_nNTFocus == 2 && NeonPadUI())) doRefresh = true;
        const float sx0 = lx0 + 320 * u, sx1 = lx1 - 8 * u;
        Quad(sx0, ty0, sx1, ty1, NC(10, 11, 16, 225)); NeonPFrame(sx0, ty0, sx1, ty1, 1.1f * u, s_bNTPasteWait ? NB_BLUE() : NC(64, 68, 82));
        char sq[80]; NeonPAscii(sq, sizeof(sq), s_szNeonSearch, true);
        const char* sh = s_szNeonSearch[0] ? sq : "TAP TO PASTE A SERVER LINK OR ADDRESS";
        const float clr = s_szNeonSearch[0] ? 30 * u : 0;
        NeonPText(F_CAPS, NeonFit(F_CAPS, sh, 11 * u, sx1 - sx0 - 24 * u - clr, 1.0f * u).c_str(), sx0 + 12 * u, (ty0 + ty1 - 11 * u) * 0.5f, 11 * u,
                  s_szNeonSearch[0] ? NC(226, 228, 232) : NC(130, 134, 142), 1.0f * u);
        if(s_szNeonSearch[0])
        {
            NeonPText(F_CAPS, "X", sx1 - 22 * u, (ty0 + ty1 - 12 * u) * 0.5f, 12 * u, NC(200, 202, 208));
            if(Tap(sx1 - clr, ty0, sx1, ty1)) { s_szNeonSearch[0] = 0; s_nNTBuiltAt = 0; s_nNTSel = 0; s_fNTScroll = 0; }
            else if(Tap(sx0, ty0, sx1 - clr, ty1)) { Clipboard::RequestPaste(); s_bNTPasteWait = true; }
        }
        else if(Tap(sx0, ty0, sx1, ty1)) { Clipboard::RequestPaste(); s_bNTPasteWait = true; }
        if(s_bNTPasteWait)
        {
            std::string got;
            if(Clipboard::PollPaste(got))
            {
                s_bNTPasteWait = false;
                while(!got.empty() && got.back() == ' ') got.pop_back();
                size_t b0 = 0; while(b0 < got.size() && got[b0] == ' ') ++b0;
                MP_CopyStr(s_szNeonSearch, got.c_str() + b0, sizeof(s_szNeonSearch)); s_nNTBuiltAt = 0; s_nNTSel = 0; s_fNTScroll = 0;
            }
        }
        if(NeonPChip("CONNECT", rx0, ty0, rx0 + (rx1 - rx0) * 0.5f - 5 * u, ty1, u, NeonIsAddress(s_szNeonSearch), false)) doConnect = true;
        if(NeonPChip(kFilt[s_nNTFilter], rx0 + (rx1 - rx0) * 0.5f + 5 * u, ty0, rx1, ty1, u, s_nNTFilter != 0, false)) { s_nNTFilter = (s_nNTFilter + 1) % 4; s_nNTSel = 0; s_fNTScroll = 0; }
    }
    // ---- the list
    const float tY0 = 166 * u, tY1 = 716 * u;
    NeonPPanel(lx0, tY0, lx1, tY1, u);
    const float cPing = lx1 - 74 * u, cPl = lx1 - 160 * u, cStar = lx1 - 200 * u, cFlag = lx1 - 310 * u;
    const float hy = tY0 + 12 * u;
    NeonPText(F_CAPS, "SERVER", lx0 + 86 * u, hy, 10 * u, NC(130, 134, 142), 1.6f * u);
    NeonPText(F_CAPS, "REGION", cFlag, hy, 10 * u, NC(130, 134, 142), 1.6f * u);
    NeonPText(F_CAPS, "PLAYERS", cPl, hy, 10 * u, NC(130, 134, 142), 1.6f * u);
    NeonPText(F_CAPS, "PING", cPing, hy, 10 * u, NC(130, 134, 142), 1.6f * u);
    Quad(lx0 + 12 * u, hy + 20 * u, lx1 - 12 * u, hy + 21 * u, NC(40, 46, 96));
    const float ly0 = hy + 28 * u, ly1 = tY1 - 8 * u, rh = 64 * u;
    const int vis = (int)((ly1 - ly0) / rh);
    if(K & NK_UP)   { if(s_nNTFocus) s_nNTFocus = 0; else if(s_nNTSel > 0) --s_nNTSel; }
    if(K & NK_DOWN) { if(!s_nNTFocus && s_nNTSel < n - 1) ++s_nNTSel; else if(!s_nNTFocus) s_nNTFocus = 3; }
    if(s_nNTFocus && (K & NK_LEFT))  s_nNTFocus = s_nNTFocus > 1 ? s_nNTFocus - 1 : 1;
    if(s_nNTFocus && (K & NK_RIGHT)) s_nNTFocus = s_nNTFocus < 3 ? s_nNTFocus + 1 : 3;
    if(K & NK_A) { if(s_nNTFocus == 1) doBack = true; else if(s_nNTFocus == 2) doRefresh = true; else doJoin = true; }
    if(K & (NK_UP | NK_DOWN))
    {   // keep the selection on screen
        const float top = s_nNTSel * rh;
        if(top < s_fNTScroll) s_fNTScroll = top;
        if(top + rh > s_fNTScroll + vis * rh) s_fNTScroll = top + rh - vis * rh;
    }
    s_fNTScroll -= DragDY(lx0, ly0, lx1, ly1);
    const float maxS = n > vis ? (n - vis) * rh : 0;
    if(s_fNTScroll < 0) s_fNTScroll = 0; if(s_fNTScroll > maxS) s_fNTScroll = maxS;
    const int first = (int)(s_fNTScroll / rh + 0.5f);
    for(int j = first; j < n && j < first + vis; ++j)
    {
        const LRow& r = s_NTRows[idx[j]];
        const float ry0 = ly0 + (j - first) * rh, ry1 = ry0 + rh - 6 * u, rx = lx0 + 10 * u, rxe = lx1 - 10 * u;
        const bool sel = j == s_nNTSel;
        Quad(rx, ry0, rxe, ry1, sel ? NB_BLUE(34) : NC(255, 255, 255, (j & 1) ? 7 : 3));
        if(Held(rx, ry0, rxe, ry1) && !Dragging()) Quad(rx, ry0, rxe, ry1, NC(255, 255, 255, 18));
        if(sel) { NeonPFrame(rx, ry0, rxe, ry1, 1.6f * u, NB_BLUE()); Quad(rx, ry0, rx + 3 * u, ry1, NB_BLUE()); }
        NeonPIni(r, rx + 12 * u, ry0 + 7 * u, rh - 20 * u);
        // name (caps) + NEON FEATURED, the description under it
        char nm[64]; NeonPAscii(nm, sizeof(nm), r.name, true);
        const float nx = lx0 + 86 * u, nameMax = cFlag - nx - 14 * u;
        float bw = 0;
        if(NeonFeatured(r)) bw = TextW(F_CAPS, "NEON FEATURED", 8 * u, 1.2f * u) + 12 * u;
        std::string nf = NeonFit(F_CAPS, nm, 14 * u, nameMax - (bw > 0 ? bw + 10 * u : 0), 1.4f * u);
        NeonPText(F_CAPS, nf.c_str(), nx, ry0 + 10 * u, 14 * u, NC(236, 238, 244), 1.4f * u);
        if(bw > 0)
        {
            const float fx = nx + TextW(F_CAPS, nf.c_str(), 14 * u, 1.4f * u) + 10 * u;
            Quad(fx, ry0 + 10 * u, fx + bw, ry0 + 25 * u, NB_BLUE(46)); NeonPFrame(fx, ry0 + 10 * u, fx + bw, ry0 + 25 * u, 1.0f * u, NB_BLUE());
            NeonPText(F_CAPS, "NEON FEATURED", fx + 6 * u, ry0 + 13.5f * u, 8 * u, NB_BLUE_LT(), 1.2f * u);
        }
        {
            char d[160], line[220];
            NeonPAscii(d, sizeof(d), r.desc[0] ? r.desc : (r.live ? "Online now" : "Saved server"), false);
            snprintf(line, sizeof(line), "%s%s%s", r.locked ? "LOCKED - " : "", r.internet ? "" : (r.kind == 1 ? "WI-FI - " : ""), d);
            NeonPText(F_UI, NeonFit(F_UI, line, 11.5f * u, nameMax).c_str(), nx, ry0 + 32 * u, 11.5f * u, NC(140, 144, 152));
        }
        // flag + language
        {
            const int ci = LCountryIndex(r.cc[0] ? r.cc : MP_CC_GLOBAL);
            const float fh = 15 * u, fw = fh * 1.4f, fy = ry0 + (rh - 6 * u - fh) * 0.5f;
            NeonPFlag(ci, cFlag, fy, cFlag + fw, fy + fh);
            char lg[MP_LANG_LEN + 4]; NeonPAscii(lg, sizeof(lg), r.lang[0] ? r.lang : "ALL", true);
            NeonPText(F_CAPS, NeonFit(F_CAPS, lg, 9.5f * u, cStar - cFlag - fw - 14 * u, 1.0f * u).c_str(), cFlag + fw + 7 * u, fy + 3 * u, 9.5f * u, NC(170, 174, 182), 1.0f * u);
        }
        // the favourite star
        {
            const bool fav = r.saved || LIsFav(r.name, r.port);
            const float sy = ry0 + (rh - 6 * u - 16 * u) * 0.5f;
            NeonPText(F_CAPS, "*", cStar + 6 * u, sy, 22 * u, fav ? NB_BLUE_LT() : NC(90, 94, 104));
            if(Tap(cStar - 4 * u, ry0, cStar + 34 * u, ry1)) { s_NTFav = r; s_nNTFavReq = fav ? 0 : 1; }
        }
        char pl[32]; if(r.maxp > 0) snprintf(pl, sizeof(pl), "%d / %d", r.players < 0 ? 0 : r.players, r.maxp); else snprintf(pl, sizeof(pl), "%d", r.players < 0 ? 0 : r.players);
        NeonPText(F_UI, pl, cPl, ry0 + 18 * u, 15 * u, r.players > 0 ? NC(226, 228, 232) : NC(130, 134, 142));
        char pg[16]; NeonPaint::Col pc = NC(130, 134, 142);
        if(r.ping > 0) { snprintf(pg, sizeof(pg), "%d MS", r.ping); pc = r.ping < 90 ? NC(110, 200, 120) : (r.ping < 180 ? NC(222, 200, 110) : NC(220, 100, 90)); }
        else snprintf(pg, sizeof(pg), "--");
        NeonPText(F_UI, pg, cPing, ry0 + 18 * u, 15 * u, pc, 1.0f * u);
        if(Tap(rx, ry0, cStar - 4 * u, ry1) || Tap(cStar + 34 * u, ry0, rxe, ry1))
        {
            const uint64_t now = MP_NowMs();
            if(sel && now - s_nNTTapAt < 700) doJoin = true;
            s_nNTSel = j; s_nNTTapAt = now; s_nNTFocus = 0;
        }
    }
    if(n > vis)
    {   // the scroll bar
        const float bh = (ly1 - ly0) * vis / n, by = ly0 + (ly1 - ly0 - bh) * (maxS > 0 ? s_fNTScroll / maxS : 0);
        Quad(lx1 - 6 * u, by, lx1 - 3 * u, by + bh, NB_BLUE(170));
    }
    if(!n)
    {
        const bool early = MP_NowMs() - s_nNTOpenAt < 4000;
        const char* m = early ? "LOOKING FOR SERVERS..." : (s_szNeonSearch[0] ? "NO SERVER MATCHES THE SEARCH" : s_nNTFilter ? "NO SERVER MATCHES THE FILTER" :
                        (s_nNeonList == 2 ? "NO FAVOURITE SERVERS YET" : (s_nNeonList == 3 ? "NO SERVERS ON THIS WI-FI" : "NO SERVERS FOUND - CHECK YOUR INTERNET")));
        const float fs = 14 * u, w = TextW(F_CAPS, m, fs, 1.8f * u);
        NeonPText(F_CAPS, m, (lx0 + lx1 - w) * 0.5f, (ly0 + ly1) * 0.5f - fs, fs, NC(160, 164, 172), 1.8f * u);
    }
    // ---- the SELECTED SERVER card
    NeonPPanel(rx0, tY0, rx1, tY1, u);
    const float px = rx0 + 20 * u, pw = rx1 - rx0 - 40 * u;
    const float jy0 = tY1 - 66 * u, jy1 = tY1 - 18 * u;
    if(n > 0)
    {
        const LRow& r = s_NTRows[idx[s_nNTSel]];
        NeonPIni(r, px, tY0 + 18 * u, 60 * u);
        NeonPText(F_CAPS, "SELECTED SERVER", px + 74 * u, tY0 + 24 * u, 10 * u, NB_BLUE_LT(), 1.8f * u);
        char nm[64]; NeonPAscii(nm, sizeof(nm), r.name, true);
        float ns = 19 * u; while(ns > 11 * u && TextW(F_CAPS, nm, ns, 1.6f * u) > pw - 74 * u) ns -= 1 * u;
        NeonPText(F_CAPS, NeonFit(F_CAPS, nm, ns, pw - 74 * u, 1.6f * u).c_str(), px + 74 * u, tY0 + 46 * u, ns, NC(240, 242, 248), 1.6f * u, true);
        // the description (wrapped)
        char dsc[MP_DESC_LEN + 120], dd[MP_DESC_LEN + 120];
        if(r.desc[0]) MP_CopyStr(dsc, r.desc, sizeof(dsc));
        else snprintf(dsc, sizeof(dsc), "%s on MP. %s%s Tap JOIN SERVER to play.", r.mode[0] ? r.mode : "Freeroam", r.lang[0] ? "Language: " : "", r.lang[0] ? r.lang : "");
        NeonPAscii(dd, sizeof(dd), dsc, false);
        NeonPWrap(F_UI, dd, px, tY0 + 96 * u, 13 * u, pw, 18 * u, 5, NC(205, 208, 214));
        // the server link + COPY LINK
        const float sy0 = tY0 + 196 * u, sy1 = sy0 + 54 * u;
        Quad(px, sy0, px + pw, sy1, NC(6, 7, 10, 235)); NeonPFrame(px, sy0, px + pw, sy1, 1.1f * u, NC(48, 54, 100));
        NeonPText(F_CAPS, "SERVER LINK", px + 11 * u, sy0 + 9 * u, 9.5f * u, NC(130, 134, 142), 1.4f * u);
        char link[128];
        if(r.ip[0]) snprintf(link, sizeof(link), "mp://%s:%d", r.ip, r.port); else snprintf(link, sizeof(link), "mp://%s", r.name);
        char la[128]; NeonPAscii(la, sizeof(la), link, false);
        const float cbx1 = px + pw - 10 * u, cbx0 = cbx1 - 104 * u;
        NeonPText(F_UI, NeonFit(F_UI, la, 12.5f * u, cbx0 - px - 20 * u).c_str(), px + 11 * u, sy0 + 28 * u, 12.5f * u, NC(226, 228, 232));
        {
            const bool copied = MP_NowMs() - s_nNTCopiedAt < 1500;
            const bool held = Held(cbx0, sy0 + 11 * u, cbx1, sy1 - 11 * u);
            Quad(cbx0, sy0 + 11 * u, cbx1, sy1 - 11 * u, held ? NB_BLUE(70) : NB_BLUE(26));
            NeonPFrame(cbx0, sy0 + 11 * u, cbx1, sy1 - 11 * u, 1.2f * u, NB_BLUE());
            const char* cl = copied ? "COPIED" : "COPY LINK"; const float cw = TextW(F_CAPS, cl, 9.5f * u, 1.2f * u);
            NeonPText(F_CAPS, cl, (cbx0 + cbx1 - cw) * 0.5f, (sy0 + sy1 - 9.5f * u) * 0.5f, 9.5f * u, NB_BLUE_LT(), 1.2f * u);
            if(Tap(cbx0, sy0 + 11 * u, cbx1, sy1 - 11 * u)) { Clipboard::Copy(link); s_nNTCopiedAt = MP_NowMs(); }
        }
        // players / ping / mode
        const float bw = (pw - 16 * u) / 3.0f, by0 = sy1 + 14 * u, by1 = by0 + 54 * u;
        char v0[32], v1[24], v2[MP_MODE_LEN + 4];
        if(r.maxp > 0) snprintf(v0, sizeof(v0), "%d / %d", r.players < 0 ? 0 : r.players, r.maxp); else snprintf(v0, sizeof(v0), "%d", r.players < 0 ? 0 : r.players);
        if(r.ping > 0) snprintf(v1, sizeof(v1), "%d MS", r.ping); else MP_CopyStr(v1, "-", sizeof(v1));
        NeonPAscii(v2, sizeof(v2), r.mode[0] ? r.mode : "Freeroam", true);
        const char* hl[3] = { "PLAYERS", "PING", "MODE" }; const char* vl[3] = { v0, v1, v2 };
        for(int k = 0; k < 3; ++k)
        {
            const float bx0 = px + k * (bw + 8 * u), bx1 = bx0 + bw;
            Quad(bx0, by0, bx1, by1, NC(6, 7, 10, 235)); NeonPFrame(bx0, by0, bx1, by1, 1.1f * u, NC(48, 54, 100));
            NeonPText(F_CAPS, hl[k], bx0 + 10 * u, by0 + 9 * u, 9.5f * u, NC(130, 134, 142), 1.4f * u);
            NeonPText(F_UI, NeonFit(F_UI, vl[k], 13.5f * u, bw - 18 * u).c_str(), bx0 + 10 * u, by0 + 27 * u, 13.5f * u, NC(226, 228, 232));
        }
        // regions & languages
        {
            const float gy = by1 + 18 * u;
            NeonPText(F_CAPS, "REGIONS & LANGUAGES", px, gy, 10 * u, NB_BLUE_LT(), 1.6f * u);
            const int ci = LCountryIndex(r.cc[0] ? r.cc : MP_CC_GLOBAL);
            NeonPFlag(ci, px, gy + 22 * u, px + 24 * u, gy + 39 * u);
            const char* cn = LCountryNameAt(ci);
            char cu[64]; NeonPAscii(cu, sizeof(cu), cn ? cn : "Global", true);
            NeonPText(F_CAPS, NeonFit(F_CAPS, cu, 11 * u, pw - 34 * u, 1.0f * u).c_str(), px + 34 * u, gy + 25 * u, 11 * u, NC(220, 222, 228), 1.0f * u);
            static const char* const kReg[4] = { "ASIA", "EUROPE", "NORTH AMERICA", "OCEANIA" };
            char lu[MP_LANG_LEN + 4]; NeonPAscii(lu, sizeof(lu), r.lang[0] ? r.lang : "All languages", true);
            char rl[160]; snprintf(rl, sizeof(rl), "LANGUAGE: %s%s%s", lu, r.region >= 0 && r.region < 4 ? "   REGION: " : "", r.region >= 0 && r.region < 4 ? kReg[r.region] : "");
            NeonPText(F_CAPS, NeonFit(F_CAPS, rl, 10 * u, pw, 1.0f * u).c_str(), px, gy + 50 * u, 10 * u, NC(160, 164, 172), 1.0f * u);
        }
    }
    else
    {
        const char* m = "SELECT A SERVER"; const float fs = 13 * u, w = TextW(F_CAPS, m, fs, 1.8f * u);
        NeonPText(F_CAPS, m, (rx0 + rx1 - w) * 0.5f, (tY0 + jy0) * 0.5f, fs, NC(130, 134, 142), 1.8f * u);
    }
    {   // JOIN SERVER
        const bool held = Held(px, jy0, px + pw, jy1), foc = s_nNTFocus == 3 && NeonPadUI();
        Quad(px, jy0, px + pw, jy1, n ? (held ? NC(120, 132, 255) : NB_BLUE(230)) : NC(40, 44, 60, 220));
        if(foc) NeonPFrame(px - 3 * u, jy0 - 3 * u, px + pw + 3 * u, jy1 + 3 * u, 1.6f * u, NC(205, 212, 255));
        const char* jl = "JOIN SERVER"; const float fs = 16 * u, w = TextW(F_CAPS, jl, fs, 2.4f * u);
        NeonPText(F_CAPS, jl, px + (pw - w) * 0.5f, (jy0 + jy1 - fs) * 0.5f, fs, NC(255, 255, 255), 2.4f * u, true);
        if(Tap(px, jy0, px + pw, jy1)) doJoin = true;
    }
    {   // the hints (always: NAVIGATE / JOIN SERVER, like the reference)
        const bool pad = NeonPadUI();
        const char* kv[6] = { pad ? "D-PAD" : "SWIPE", "NAVIGATE", pad ? "A" : "DOUBLE TAP", "JOIN SERVER", pad ? "B" : "BACK", "MAIN MENU" };
        const float hy2 = 740 * u, fs = 11 * u; float x = L;
        for(int i = 0; i < 3; ++i)
        {
            const float kw = TextW(F_CAPS, kv[i * 2], fs, 1.2f * u) + 14 * u;
            Quad(x, hy2 - 4 * u, x + kw, hy2 + fs + 4 * u, NC(10, 11, 16, 220)); NeonPFrame(x, hy2 - 4 * u, x + kw, hy2 + fs + 4 * u, 1.1f * u, NB_BLUE());
            NeonPText(F_CAPS, kv[i * 2], x + 7 * u, hy2, fs, NC(226, 228, 232), 1.2f * u);
            x += kw + 9 * u;
            NeonPText(F_CAPS, kv[i * 2 + 1], x, hy2, fs, NC(170, 174, 182), 1.8f * u, true);
            x += TextW(F_CAPS, kv[i * 2 + 1], fs, 1.8f * u) + 30 * u;
        }
    }
    if(doRefresh) { ServerList::Refresh(); s_nNTBuiltAt = 0; s_nNTOpenAt = MP_NowMs(); logger->Info("Neon menu: Browse servers -> refresh"); }
    if(doConnect)
    {
        if(NeonIsAddress(s_szNeonSearch))
        {   // a pasted server link / address
            const char* s = s_szNeonSearch;
            char host[96]; MP_CopyStr(host, !strncasecmp(s, "mp://", 5) ? s + 5 : s, sizeof(host));
            int port = MP_DEFAULT_PORT; if(char* c = strrchr(host, ':')) { *c = 0; port = atoi(c + 1); if(port <= 0 || port > 65535) port = MP_DEFAULT_PORT; }
            LRow r; memset(&r, 0, sizeof(r)); r.kind = 1; MP_CopyStr(r.ip, host, sizeof(r.ip)); r.port = port; MP_CopyStr(r.name, host, sizeof(r.name));
            s_NTJoin = r; s_nNTJoinReq = 0;
        }
        else doJoin = true;
    }
    if(doJoin && s_nNTSel >= 0 && s_nNTSel < n) { s_NTJoin = s_NTRows[idx[s_nNTSel]]; s_nNTJoinReq = idx[s_nNTSel]; }
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
        if(on) { Quad(tx0 + 6 * u, y0, tx1 - 6 * u, y1, NC(205, 212, 255, 24)); Quad(tx0 + 6 * u, y0, tx0 + 9 * u, y1, NC(88, 101, 242)); }
        if(Held(tx0, y0, tx1, y1)) Quad(tx0 + 6 * u, y0, tx1 - 6 * u, y1, NC(255, 255, 255, 18));
        const float fs = 14 * u;
        NeonPText(F_CAPS, kNSTabs[i].name, tx0 + 26 * u, y0 + (y1 - y0 - fs) * 0.5f, fs, on ? NC(160, 170, 255) : NC(170, 174, 182), 1.8f * u);
        if(Tap(tx0, y0, tx1, y1)) { s_nNSTab = i; s_nNSSel = 0; s_fNSScroll = 0; s_nNSFocus = 0; }
    }
    // the rows of the tab
    std::vector<int> rows;
    for(int i = 0; i < (int)s_NSItems.size(); ++i) if(s_NSItems[i].tab == kNSTabs[s_nNSTab].tab) rows.push_back(i);
    const float px0 = tx1 + 18 * u, px1 = W - L + 14 * u, py0 = 150 * u, py1 = 700 * u;
    NeonPPanel(px0, py0, px1, py1, u);
    {
        char t[48]; snprintf(t, sizeof(t), "%s", kNSTabs[s_nNSTab].name);
        NeonPText(F_CAPS, t, px0 + 22 * u, py0 + 14 * u, 12 * u, NC(130, 134, 142), 2.0f * u);
        Quad(px0 + 12 * u, py0 + 36 * u, px1 - 12 * u, py0 + 37 * u, NC(40, 46, 96));
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
        Quad(px0 + 8 * u, ry0, px1 - 8 * u, ry1, sel ? NC(205, 212, 255, 22) : NC(255, 255, 255, (k & 1) ? 6 : 2));
        if(sel) { Quad(px0 + 8 * u, ry0, px0 + 11 * u, ry1, NC(88, 101, 242)); if(!s_nNSFocus && NeonPadUI()) NeonPFrame(px0 + 8 * u, ry0, px1 - 8 * u, ry1, 1.2f * u, NC(88, 101, 242)); }
        char lb[64]; NeonPAscii(lb, sizeof(lb), it.label[0] ? it.label : "Mod option", true);
        const std::string val = NeonNSValue(it);
        if(it.kind == GameSettings::K_HEADER)
        {   // 5.01.13: a mod's settings tab / the mods folder (PCMenu's Mods list)
            Quad(px0 + 12 * u, ry1 - 2 * u, px1 - 12 * u, ry1 - 1 * u, NC(52, 60, 130));
            NeonPText(F_CAPS, NeonFit(F_CAPS, lb, 12.5f * u, vx0 - px0 - 40 * u, 2.0f * u).c_str(), px0 + 22 * u, mid - 6 * u, 12.5f * u, NC(160, 170, 255), 2.0f * u);
            const float w = TextW(F_UI, val.c_str(), 12 * u);
            NeonPText(F_UI, val.c_str(), vx1 - w, mid - 7 * u, 12 * u, NC(130, 134, 142));
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
            Quad(bx0, ry0 + 8 * u, vx1, ry1 - 8 * u, hb ? NC(88, 101, 242, 90) : NC(88, 101, 242, 40));
            const float w = TextW(F_CAPS, val.c_str(), 12 * u, 1.6f * u);
            NeonPText(F_CAPS, val.c_str(), (bx0 + vx1 - w) * 0.5f, mid - 6 * u, 12 * u, NC(236, 226, 196), 1.6f * u);
            if(Tap(px0 + 8 * u, ry0, px1 - 8 * u, ry1)) { s_nNSSel = k; s_nNSFocus = 0; NeonNSStep(it, +1); }
            continue;
        }
        if(it.kind == GameSettings::K_SLIDER)
        {
            const float bx0 = vx0 + 14 * u, bx1 = vx1 - 78 * u, f = NeonNSFrac(it);
            Quad(bx0, mid - 3 * u, bx1, mid + 3 * u, NC(40, 42, 48));
            Quad(bx0, mid - 3 * u, bx0 + (bx1 - bx0) * f, mid + 3 * u, NC(88, 101, 242));
            const float kx = bx0 + (bx1 - bx0) * f;
            Quad(kx - 5 * u, mid - 11 * u, kx + 5 * u, mid + 11 * u, NC(236, 226, 196));
            const float w = TextW(F_UI, val.c_str(), 14 * u);
            NeonPText(F_UI, val.c_str(), vx1 - w, mid - 8 * u, 14 * u, NC(160, 170, 255));
            float fx = 0;
            if(HeldX(bx0 - 16 * u, ry0, bx1 + 16 * u, ry1, &fx)) { s_nNSSel = k; s_nNSFocus = 0; NeonNSFraction(it, (fx - bx0) / (bx1 - bx0)); }
        }
        else
        {
            const float aw = 34 * u;
            const bool hl = Held(vx0, ry0, vx0 + aw, ry1), hr = Held(vx1 - aw, ry0, vx1, ry1);
            NeonPText(F_CAPS, "<", vx0 + 10 * u, mid - 8 * u, 16 * u, hl ? NC(205, 212, 255) : NC(150, 152, 158));
            NeonPText(F_CAPS, ">", vx1 - 20 * u, mid - 8 * u, 16 * u, hr ? NC(205, 212, 255) : NC(150, 152, 158));
            const std::string fit = NeonFit(F_CAPS, val.c_str(), 13 * u, vx1 - vx0 - 2 * aw - 8 * u, 1.2f * u);
            const float w = TextW(F_CAPS, fit.c_str(), 13 * u, 1.2f * u);
            NeonPText(F_CAPS, fit.c_str(), (vx0 + vx1 - w) * 0.5f, mid - 7 * u, 13 * u, NC(160, 170, 255), 1.2f * u);
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
        Quad(px1 - 6 * u, by, px1 - 3 * u, by + bh, NC(88, 101, 242, 160));
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

static void NeonPDialog(float W, float H, float u);

// ---- Start game (5.02.01) ----
// New game on the neon title opens this page, like PCMenu's START GAME: NEW GAME (asks first) and your save slots with
// the last mission passed, the story progress (the game's own PROGRESS_MADE / TOTAL_PROGRESS stats from the save) and
// when it was saved. Tap a slot (or LOAD) to load it. Read on the game thread every 2 s while the page is open.
struct NGSlot { bool used = false; char title[48] = ""; float pct = -1; char when[32] = ""; };
static std::mutex s_NGMutex;
static NGSlot     s_NGSlots[8];
static uint64_t   s_nNGBuiltAt = 0;
static int        s_nNGSel = 0, s_nNGFocus = 0;   // focus: 0 slots, 1 BACK, 2 NEW GAME, 3 LOAD
static std::atomic<int> s_nNGLoadReq{-1};
static void NeonTOpenStart()
{
    s_nNT = NT_START; s_nNGBuiltAt = 0; s_nNGFocus = 0;
    const int nw = Game::NewestSaveSlot(); s_nNGSel = nw >= 0 ? nw : 0;
}
static bool NeonNGRead(int slot, NGSlot& o)
{
    char path[420]; if(!Game::SaveSlotPath(slot, path, sizeof(path))) return false;
    FILE* f = fopen(path, "rb"); if(!f) return false;
    std::vector<uint8_t> d; d.resize(1 << 20);
    const size_t n = fread(d.data(), 1, d.size(), f); fclose(f);
    if(n < 1024 || memcmp(d.data(), "BLOCK", 5)) return false;
    d.resize(n);
    o.used = true;
    {   // block 0 (simple variables): +4 version, +9 the save name = the GXT key of the last mission passed,
        // or 0xFC + the name itself
        char raw[101]; memcpy(raw, d.data() + 9, 100); raw[100] = 0;
        std::string t;
        if((uint8_t)raw[0] == 0xFC) t = raw + 1;
        else if(raw[0] && raw[1] == 0) { for(int i = 0; i < 100 && raw[i]; i += 2) t += raw[i]; }   // a 16-bit name
        else if(raw[0]) t = strlen(raw) <= 8 ? GameSettings::GxtText(raw) : std::string(raw);
        char a[48]; NeonPAscii(a, sizeof(a), t.c_str(), true);
        MP_CopyStr(o.title, a[0] ? a : "SAVED GAME", sizeof(o.title));
    }
    {   // block 16 (stats) starts with the float stats: [0] PROGRESS_MADE, [1] TOTAL_PROGRESS_IN_GAME
        std::vector<size_t> blk;
        for(size_t i = 0; i + 5 <= n && blk.size() < 40; ++i) if(d[i] == 'B' && !memcmp(&d[i], "BLOCK", 5)) blk.push_back(i);
        auto pctAt = [&](size_t b) -> float {
            if(b + 13 > n) return -1;
            float made, total; memcpy(&made, &d[b + 5], 4); memcpy(&total, &d[b + 9], 4);
            if(!(total >= 50.0f && total <= 10000.0f) || !(made >= 0.0f && made <= total * 1.001f)) return -1;
            return made / total * 100.0f;
        };
        if(blk.size() > 16) o.pct = pctAt(blk[16]);
        for(size_t k = 14; o.pct < 0 && k < blk.size() && k < 20; ++k) o.pct = pctAt(blk[k]);   // a build with one block more / less
    }
    struct stat st; if(stat(path, &st) == 0)
    {
        time_t t = st.st_mtime; struct tm tmv; localtime_r(&t, &tmv);
        strftime(o.when, sizeof(o.when), "%d %b %Y  %H:%M", &tmv);
        for(char* c = o.when; *c; ++c) if(*c >= 'a' && *c <= 'z') *c -= 32;
    }
    return true;
}
static void NeonNGBuild()
{
    NGSlot tmp[8];
    for(int i = 0; i < 8; ++i) if(!NeonNGRead(i, tmp[i])) tmp[i] = NGSlot();
    std::lock_guard<std::mutex> lk(s_NGMutex);
    for(int i = 0; i < 8; ++i) s_NGSlots[i] = tmp[i];
}
static void NeonPStart(float W, float H)
{
    using namespace NeonPaint;
    const float u = H / 804.0f;
    const float L = fminf(W * 0.07f, 100 * u * (W / H) / (1430.0f / 804.0f));
    NeonPBackdrop(W, H);
    NeonPHeader("Start game", "NEW GAME - LOAD GAME - YOUR STORY PROGRESS", L, u);
    NGSlot sl[8]; { std::lock_guard<std::mutex> lk(s_NGMutex); for(int i = 0; i < 8; ++i) sl[i] = s_NGSlots[i]; }
    const bool dlg = s_nNeonDlg != 0;
    const int K = dlg ? 0 : s_nNK;
    bool doBack = (K & NK_B) != 0, doNew = false, doLoad = false;
    if(K & NK_UP)   { if(s_nNGFocus) s_nNGFocus = 0; else if(s_nNGSel > 0) --s_nNGSel; }
    if(K & NK_DOWN) { if(!s_nNGFocus && s_nNGSel < 7) ++s_nNGSel; else if(!s_nNGFocus) s_nNGFocus = 3; }
    if(s_nNGFocus && (K & NK_LEFT))  s_nNGFocus = s_nNGFocus > 1 ? s_nNGFocus - 1 : 1;
    if(s_nNGFocus && (K & NK_RIGHT)) s_nNGFocus = s_nNGFocus < 3 ? s_nNGFocus + 1 : 3;
    if(K & (NK_A | NK_START)) { if(s_nNGFocus == 1) doBack = true; else if(s_nNGFocus == 2) doNew = true; else doLoad = true; }
    if(K & NK_X) doNew = true;
    // the slots
    const float px0 = L - 14 * u, px1 = W - L + 14 * u, py0 = 140 * u, py1 = 700 * u;
    NeonPPanel(px0, py0, px1, py1, u);
    NeonPText(F_CAPS, "SAVED GAMES", px0 + 22 * u, py0 + 14 * u, 12 * u, NC(130, 134, 142), 2.0f * u);
    NeonPText(F_CAPS, "STORY PROGRESS", px1 - 420 * u, py0 + 14 * u, 12 * u, NC(130, 134, 142), 2.0f * u);
    Quad(px0 + 12 * u, py0 + 36 * u, px1 - 12 * u, py0 + 37 * u, NC(40, 46, 96));
    const float ly0 = py0 + 46 * u, rh = (py1 - 8 * u - ly0) / 8.0f;
    for(int i = 0; i < 8; ++i)
    {
        const NGSlot& s = sl[i];
        const float ry0 = ly0 + i * rh, ry1 = ry0 + rh - 4 * u, mid = (ry0 + ry1) * 0.5f;
        const bool sel = i == s_nNGSel;
        Quad(px0 + 8 * u, ry0, px1 - 8 * u, ry1, sel ? NC(205, 212, 255, 22) : NC(255, 255, 255, (i & 1) ? 6 : 2));
        if(sel) { Quad(px0 + 8 * u, ry0, px0 + 11 * u, ry1, NB_BLUE()); if(!s_nNGFocus && NeonPadUI()) NeonPFrame(px0 + 8 * u, ry0, px1 - 8 * u, ry1, 1.2f * u, NB_BLUE()); }
        char nb[16]; snprintf(nb, sizeof(nb), "SLOT %d", i + 1);
        NeonPText(F_CAPS, nb, px0 + 26 * u, mid - 7 * u, 12 * u, NC(130, 134, 142), 1.6f * u);
        const float tx = px0 + 120 * u, bx0 = px1 - 420 * u, bx1 = px1 - 170 * u;
        if(!s.used) { NeonPText(F_CAPS, "EMPTY SLOT", tx, mid - 8 * u, 14 * u, NC(110, 114, 122), 1.4f * u); }
        else
        {
            NeonPText(F_CAPS, NeonFit(F_CAPS, s.title, 14 * u, bx0 - tx - 20 * u, 1.4f * u).c_str(), tx, mid - 15 * u, 14 * u, NC(226, 228, 232), 1.4f * u);
            NeonPText(F_UI, s.when, tx, mid + 4 * u, 11.5f * u, NC(140, 144, 152), 1.0f * u);
            if(s.pct >= 0)
            {
                const float f = s.pct > 100 ? 1.0f : s.pct / 100.0f;
                Quad(bx0, mid - 3 * u, bx1, mid + 3 * u, NC(40, 42, 48));
                Quad(bx0, mid - 3 * u, bx0 + (bx1 - bx0) * f, mid + 3 * u, NB_BLUE());
                char pc[16]; snprintf(pc, sizeof(pc), "%.1f%%", s.pct);
                NeonPText(F_UI, pc, bx1 + 16 * u, mid - 8 * u, 14 * u, NB_BLUE_LT());
            }
            else NeonPText(F_UI, "-", bx0, mid - 8 * u, 14 * u, NC(110, 114, 122));
        }
        if(!dlg && Tap(px0 + 8 * u, ry0, px1 - 8 * u, ry1))
        {   // tap = select, tap the selected one again = load it
            if(s_nNGSel == i && s.used) doLoad = true;
            s_nNGSel = i; s_nNGFocus = 0;
        }
    }
    const float by0 = 712 * u, by1 = 748 * u;
    const bool pad = NeonPadUI() && !dlg;
    if(NeonPBtn("BACK", L - 14 * u, by0, L + 150 * u, by1, u, pad && s_nNGFocus == 1) && !dlg) doBack = true;
    if(NeonPBtn("NEW GAME", L + 164 * u, by0, L + 380 * u, by1, u, pad && s_nNGFocus == 2) && !dlg) doNew = true;
    char ll[24]; snprintf(ll, sizeof(ll), "LOAD SLOT %d", s_nNGSel + 1);
    if(NeonPBtn(ll, L + 394 * u, by0, L + 640 * u, by1, u, pad && s_nNGFocus == 3) && !dlg) doLoad = true;
    static const char* const kHints[] = { "A", "LOAD", "X", "NEW GAME", "B", "BACK" };
    NeonPHints(kHints, 3, W, u, L);
    if(doLoad && !sl[s_nNGSel].used) { doLoad = false; NeonToast("EMPTY SLOT", "Pick a saved game, or NEW GAME"); }
    if(doLoad) s_nNGLoadReq = s_nNGSel;
    else if(doNew) { s_nNeonDlg = 3; s_nNeonDlgSel = 1; }
    else if(doBack) s_nNT = NT_MAIN;
    if(dlg) NeonPDialog(W, H, u);
}

static void NeonPDialog(float W, float H, float u)
{
    using namespace NeonPaint;
    Quad(0, 0, W, H, NC(0, 0, 0, 170));
    const float dw = 520 * u, dh = 230 * u, x0 = (W - dw) * 0.5f, y0 = (H - dh) * 0.5f;
    Img(NeonPTex("nb_dialog.png", "dialog.png"), x0, y0, x0 + dw, y0 + dh);
    const bool about = s_nNeonDlg == 1, ng = s_nNeonDlg == 3;   // 5.02.01: 3 = New game (Start game page)
    NeonPText(F_GOTHIC, about ? "About" : ng ? "New game" : "Quit game", x0 + 28 * u, y0 + 18 * u, 34 * u, NC(226, 228, 232), 0, true);
    char l[5][120]; int n = 0;
    if(about)
    {
        snprintf(l[n++], 120, "MP Multiplayer %s for GTA San Andreas (Android, AML).", MP_VERSION_STR);
        snprintf(l[n++], 120, "by ThirtyFoxMCx00E1 - mpsamp-open.github.io");
        snprintf(l[n++], 120, "Discord: %s", MP_AUTH_DISCORD_URL);
        snprintf(l[n++], 120, "Fonts: UnifrakturMaguntia, Cinzel, Fredoka (SIL OFL 1.1).");
    }
    else if(ng) { snprintf(l[n++], 120, "Start a new game from the first mission?"); snprintf(l[n++], 120, "Your saved games stay in their slots."); }
    else snprintf(l[n++], 120, "Close GTA San Andreas now? Unsaved progress is lost.");
    for(int i = 0; i < n; ++i) NeonPText(F_UI, l[i], x0 + 30 * u, y0 + (72 + i * 22) * u, 15 * u, NC(190, 194, 202));
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
        else if(NeonPBtn(ng ? "START" : "QUIT", x0 + dw - 180 * u, by0, x0 + dw - 30 * u, by1, u, pad && s_nNeonDlgSel == 1) || ((s_nNK & NK_A) && s_nNeonDlgSel == 1))
        { if(ng) { s_nNeonDlg = 0; s_nNT = NT_MAIN; s_nNeonAct = NA_NEWGAME; } else s_nNeonAct = NA_QUIT; }
    }
    EatTap();   // nothing under the dialog gets the tap
}
static void NeonPTitle(float W, float H)
{
    using namespace NeonPaint;
    const float u = H / 804.0f;
    const float L = fminf(W * 0.07f, 100 * u * (W / H) / (1430.0f / 804.0f));
    NeonPBackdrop(W, H);
    if(const Tex* lg = T("logo.png"))
    {
        const float w = 490 * u, h = w * (601.0f / 1920.0f);   // title.png's shape (the texture is stretched to 1024x512)
        Img(lg, L, 52 * u, L + w, 52 * u + h);
    }
    // the dialog takes the taps first (it is drawn last, on top)
    const bool dlg = s_nNeonDlg != 0;
    bool tapUsed = false;
    static const char* const kItems[] = { "Resume", "New game", "Browse servers", "Quick connect", "Settings", "About", "Quit game" };
    const int ni = 7; const float fs = 38 * u, step = 43 * u, y0 = 205 * u, ix = L + 24 * u;
    // 5.01.11: the items do what they say: Browse servers / Settings open their neon pages right here
    auto activate = [&](int i)
    {
        s_nNeonSel = i;
        switch(i)
        {
            case 0: s_nNeonAct = NA_RESUME; break;
            case 1: NeonTOpenStart(); break;   // 5.02.01: the Start game page (New game + your saves), like PCMenu
            case 2: NeonTOpenBrowse(); break;
            case 3: s_nNeonAct = NA_QUICK; break;
            case 4: NeonTOpenSettings(); break;
            case 5: s_nNeonDlg = 1; break;
            case 6: s_nNeonDlg = 2; s_nNeonDlgSel = 1; break;
            case 7: NeonTOpenBrowse(); break;            // NEON FEATURED
        }
    };
    if(!dlg && s_nNK)
    {   // 5.01.11: the controller (PCMenu's ControllerNav): up / down = the item (then the two cards), A = press, B = Quit game
        if(s_nNeonSel < 0 || s_nNeonSel > 7) s_nNeonSel = 0;
        if(s_nNK & NK_UP) s_nNeonSel = (s_nNeonSel + 7) % 8;   // 5.01.12: no identity card (8) any more
        if(s_nNK & NK_DOWN) s_nNeonSel = (s_nNeonSel + 1) % 8;
        if((s_nNK & NK_RIGHT) && s_nNeonSel < 7) s_nNeonSel = 7;
        if((s_nNK & NK_LEFT) && s_nNeonSel >= 7) s_nNeonSel = 2;
        if(s_nNK & (NK_A | NK_START)) activate(s_nNeonSel);
        else if(s_nNK & NK_B) { s_nNeonDlg = 2; s_nNeonDlgSel = 0; }
        if(s_nNK & NK_Y) NeonTOpenBrowse();
    }
    for(int i = 0; i < ni; ++i)
    {
        const float y = y0 + i * step, w = TextW(F_GOTHIC, kItems[i], fs);
        const float hx0 = ix - 30 * u, hy0 = y, hx1 = ix + w + 30 * u, hy1 = y + step;
        const bool held = !dlg && Held(hx0, hy0, hx1, hy1);
        const bool on = i == s_nNeonSel || held;
        if(on) { const float a = 9 * u; Img(T("arrow.png"), ix - 14 * u - a * 0.5f, y + step * 0.5f - a * 0.5f, ix - 14 * u + a * 0.5f, y + step * 0.5f + a * 0.5f, NC(160, 170, 255)); }
        const float px = ix, py = y + (step - fs) * 0.5f - 2 * u;
        for(int k = 0; k < 4; ++k) Text(F_GOTHIC, kItems[i], px + ((k & 1) ? 1.4f : -1.4f) * u, py + ((k & 2) ? 1.4f : -1.4f) * u, fs, NC(10, 12, 18, 230));
        Text(F_GOTHIC, kItems[i], px + 2 * u, py + 3 * u, fs, NC(0, 0, 0, 160));
        Text(F_GOTHIC, kItems[i], px, py, fs, on ? NC(240, 243, 250) : NC(196, 204, 220), on ? NC(170, 180, 205) : NC(112, 122, 145));
        if(!dlg && !tapUsed && Tap(hx0, hy0, hx1, hy1)) { tapUsed = true; activate(i); }
    }
    {   // tagline
        const float ty = y0 + ni * step + 22 * u;
        NeonPText(F_UI, "FIND A WORLD AND JOIN THE STREETS OF SAN", ix + 10 * u, ty, 12.5f * u, NC(205, 208, 214), 1.0f * u);
        NeonPText(F_UI, "ANDREAS.", ix + 10 * u, ty + 16 * u, 12.5f * u, NC(205, 208, 214), 1.0f * u);
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
    {   // Neon featured (bottom left): on the title it opens the server browser after the Multiplayer load
        const float ax = L - 8 * u, ay = 643 * u, bx = L + 379 * u, by = 723 * u;
        const bool held = !dlg && (Held(ax, ay, bx, by) || s_nNeonSel == 7);
        Img(NeonPTex("nb_card.png", "card_featured.png"), ax, ay, bx, by, held ? NC(205, 212, 255) : NC(255, 255, 255));
        NeonPFrame(ax, ay, bx, by, 1.4f * u, NC(88, 101, 242));   // 5.01.16: the NEON FEATURED card has a blue outline
        if(s_nNeonSel == 7 && NeonPadUI()) NeonPFrame(ax - 2 * u, ay - 2 * u, bx + 2 * u, by + 2 * u, 1.6f * u, NC(88, 101, 242));
        NeonPText(F_CAPS, "NEON FEATURED", ax + 20 * u, ay + 18 * u, 10 * u, NC(150, 152, 158), 1.2f * u);
        NeonPText(F_CAPS, "BROWSE SERVERS", ax + 20 * u, ay + 32 * u, 18 * u, NC(226, 228, 232), 1.8f * u, true);
        NeonPText(F_CAPS, "PLAY NOW", ax + 20 * u, ay + 57 * u, 10 * u, NC(200, 202, 208), 1.2f * u);
        if(!dlg && !tapUsed && Tap(ax, ay, bx, by)) { tapUsed = true; activate(7); }
    }
    {   // footer: version + hints + language
        char v[64]; snprintf(v, sizeof(v), "V%s - NEON PREVIEW", MP_VERSION_STR);
        const float hy = 758 * u;
        NeonPText(F_UI, v, L - 8 * u, hy, 12 * u, NC(100, 104, 112), 2.4f * u);
        float hx = W * (998.0f / 1430.0f);
        if(NeonPadUI())
        {   // 5.01.12: SELECT / A CONFIRM only while a controller is in use (touch = no pad hints)
            Img(T("arrow_down.png"), hx, hy + 2 * u, hx + 9 * u, hy + 11 * u, NC(120, 124, 132));
            NeonPText(F_UI, "SELECT", hx + 16 * u, hy, 12 * u, NC(120, 124, 132), 2.2f * u);
            const float ex = hx + 84 * u;   // 5.01.11: the controller's A (the menu takes the pad now)
            NeonPFrame(ex, hy - 3 * u, ex + 18 * u, hy + 14 * u, 1.1f * u, NC(150, 152, 158));
            NeonPText(F_UI, "A", ex + 5 * u, hy, 12 * u, NC(200, 202, 208));
            NeonPText(F_UI, "CONFIRM", ex + 26 * u, hy, 12 * u, NC(120, 124, 132), 2.2f * u);
        }
        const char* ln = "ENGLISH";
        {   // the atlas is ASCII: other languages show their code
            static char up[16]; const char* c = settings.uiLanguage[0] ? settings.uiLanguage : "en";
            if(strcasecmp(c, "en")) { NeonUpper(up, sizeof(up), c); ln = up; }
        }
        const float lx = W - 160 * u;
        if(NeonPadUI())
        {
            NeonPFrame(lx, hy - 2 * u, lx + 16 * u, hy + 14 * u, 1.2f * u, NC(200, 202, 208));
            NeonPText(F_UI, "A", lx + 4 * u, hy, 11 * u, NC(200, 202, 208));
        }
        NeonPText(F_UI, ln, lx + 24 * u, hy, 12 * u, NC(220, 222, 228), 1.6f * u);
    }
    if(dlg) NeonPDialog(W, H, u);
}
static void NeonPLoading(float W, float H)
{   // 5.01.14 (the 5.01.13 look again in 5.01.15): the cover over the game's menus while a server is being joined
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
    const float fs = 15 * u, ty = H - 64 * u;
    NeonPText(F_CAPS, m, W - L - TextW(F_CAPS, "CONNECTING TO MULTIPLAYER...", fs, 1.8f * u), ty, fs, NC(226, 228, 232), 1.8f * u, true);
    {   // a moving blue bar
        const float bx0 = L - 8 * u, bx1 = W - L + 8 * u, by = H - 30 * u;
        Quad(bx0, by, bx1, by + 3 * u, NC(255, 255, 255, 36));
        const float f = (float)(now % 1600) / 1600.0f, seg = (bx1 - bx0) * 0.22f;
        const float a = bx0 + (bx1 - bx0 + seg) * f - seg;
        Quad(fmaxf(a, bx0), by, fminf(a + seg, bx1), by + 3 * u, NC(88, 101, 242));
    }
    NeonPText(F_CAPS, "GAMEPLAY TIP", L - 8 * u, ty, 12 * u, NC(160, 170, 255), 1.8f * u, true);
    NeonPText(F_UI, "Find a world and join the streets of San Andreas.", L - 8 * u, ty + 17 * u, 13 * u, NC(170, 174, 182));
    EatTap();
}
static void NeonPPill(float W, float H)
{   // the game's own menu (Game settings): the way back to the neon menu
    using namespace NeonPaint;
    const float u = H / 804.0f, w = 230 * u, h = 46 * u, x0 = W * 0.02f, y0 = H - h - 20 * u;
    Img(NeonPTex("nb_pill.png", "pill.png"), x0, y0, x0 + w, y0 + h);
    NeonPText(F_UI, "NEON MENU", x0 + 20 * u, y0 + 14 * u, 14 * u, NC(226, 228, 232), 3.0f * u);
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
    if(s_nNeonPaintFrames < 3) logger->Info("Neon menu: native frame %d (%s, %.0fx%.0f)", s_nNeonPaintFrames + 1, what == 1 ? "neon title" : what == 2 ? "mp tile" : "pill", W, H);
    NeonPaint::FrameInput();
    NeonPaint::Begin();
    s_nNK = s_nNeonKeysQ.exchange(0);   // 5.01.11: the controller keys since the last paint
    if(what == 1)
    {
        s_nNeonTitleMs = MP_NowMs();
        if(s_nNT == NT_BROWSE) NeonPBrowse(W, H);
        else if(s_nNT == NT_SETTINGS) NeonPSettings(W, H);
        else if(s_nNT == NT_START) NeonPStart(W, H);   // 5.02.01
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
    NeonCloseTick(MP_NowMs());  // 5.02.01: Resume / New game / a slot: no pause menu after the load
    if(s_nNeonMode == NEON_MODE_DEFAULT) { NeonMusicStop(); return; }   // 4.13.30
    const uint64_t now = MP_NowMs();
    const bool titleUp = s_nNeonTitleMs && now - s_nNeonTitleMs < 600 && NeonTitleCtx();
    // 5.01.11: the menu music while the neon title menu is up; notify.mp3 for the Discord pop-up
    // 5.01.15: it keeps playing through a short gap (a sub-menu, a frame without the title) instead of restarting at 0:00
    NeonMusicTick(s_nNeonTitleMs && now - s_nNeonTitleMs < 4000 && NeonTitleCtx());
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
    if(titleUp && s_nNT == NT_START && now - s_nNGBuiltAt > 2000) { s_nNGBuiltAt = now; NeonNGBuild(); }   // 5.02.01
    { const int ls = s_nNGLoadReq.exchange(-1); if(ls >= 0 && titleUp) { s_nNT = NT_MAIN; NeonMusicStop(); NeonLoadSlot(ls); } }
    if(!titleUp && s_nNT != NT_MAIN && !NeonTitleCtx()) s_nNT = NT_MAIN;
    {
        const int fv = s_nNTFavReq.exchange(-1);   // 5.01.15: the favourite star (favorites.txt on this thread)
        if(fv >= 0) { LSetFav(s_NTFav.name, s_NTFav.port, fv == 1); s_nNTBuiltAt = 0; }
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
