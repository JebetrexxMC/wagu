// 3.21.6: login system screens (the logic is in net/authclient.cpp, the server side in net/server.cpp).
//
//  connect -> the server sends a challenge -> the < Spawn > buttons wait for this:
//    new phone:  CODE    "type /gen XXXXXXXX in #verification" + the Discord invite
//                SIGNUP  User / Password / Gender / Age  [Create]  -> "Account created successfully"
//    rejoin:     LOGIN   User / Password  [Login]
//    same game session (host change, reconnect): nothing to do, the ticket goes by itself
//  Brief Case War: the same system, its own look (briefcase icon, red + black, moving highlight).
//  The account (user, Discord) is only known by the MP bot and the server: never in the chat,
//  never in the player list. This phone keeps the user names (account.txt, accounts.txt, no password) and,
//  5.00.05, its saved login (auth_service.txt): a game restart, a rejoin, Moquil or MP Textdraw Editor stay
//  logged in. The files are kept the same in every MP folder of the phone (Android/data and
//  Android_unprotected/data, GTA SA and GTA VC: .../files/TYFMC/MP) and made again from the bot when deleted.

static AuthClient s_Auth;
static bool       s_bAuthReq = false;        // this connection's server wants a login
static int        s_nAuthStyle = AUTH_STYLE_NORMAL;
static char       s_szAuthChal[33] = "";
static uint64_t   s_nAuthDoneAt = 0, s_nAuthTicketAt = 0, s_nAuthJoinAt = 0;
static std::string s_AuthTicket; static uint8_t s_AuthSig[64];
static bool       s_bAuthTicket = false;
static std::string s_AuthUser;               // the account this game session is logged in with
static bool       s_bStatsOpen = false;
// 5.02.81: Settings > Account: log in / create an account / log out without joining a server ("alone" login)
static bool       s_bAuthAlone = false, s_bAuthAloneTop = false;
static char       s_szAloneChal[33] = "";
static uint64_t   s_nStatsAt = 0;

struct AuthField { char buf[40]; int max; bool secret; bool numeric; const char* title; };
// .max is the VKeyboard buffer size (it allows max-1 characters, room for the '\0'): 20 chars = 21, 32 = 33, 2 digits = 3
static AuthField s_AfUser = { "", 21, false, false, "User" };
static AuthField s_AfPass = { "", 33, true,  false, "Password" };
static AuthField s_AfAge  = { "", 3,  false, true,  "Age" };
static AuthField s_AfNewPass  = { "", 33, true, false, "New password" };    // 3.56: forgot password
static AuthField s_AfNewPass2 = { "", 33, true, false, "Confirm" };
static const int AUTH_AGE_MIN = 10, AUTH_AGE_MAX = 24;   // allowed sign-up ages (keep the hint text below in step)
static int       s_nAfGender = 0;            // 0 Male, 1 Female, 2 Other
static AuthField* s_pAfEditing = nullptr;
static const char* kAuthGenders[3] = { "Male", "Female", "Other" };

// ---- 5.00.05: the login files live in every MP folder of this phone --------------------------------
// s_szDataDir first; the others are copies: Android/data + Android_unprotected/data, gtasa + gtavc
// (only where that game's files/ folder exists). A missing file here is taken from a copy, a write goes to all.
static const char* kAuthFiles[] = { "auth_service.txt", "account.txt", "accounts.txt", "auth_device.txt" };
static std::vector<std::string> s_AuthDirs;
static bool AuthIsDir(const std::string& p) { struct stat st; return !stat(p.c_str(), &st) && S_ISDIR(st.st_mode); }
static const std::vector<std::string>& AuthDirs()
{
    if(!s_AuthDirs.empty() || !s_szDataDir[0]) return s_AuthDirs;
    s_AuthDirs.push_back(s_szDataDir);
    auto add = [](const std::string& d) {
        for(auto& x : s_AuthDirs) if(x == d) return;
        s_AuthDirs.push_back(d);
    };
    const char* roots[] = { "/storage/emulated/0/Android/data", "/storage/emulated/0/Android_unprotected/data" };
    const char* pkgs[]  = { "com.rockstargames.gtasa", "com.rockstargames.gtavc" };
    for(const char* r : roots) for(const char* k : pkgs)
    {
        std::string files = std::string(r) + "/" + k + "/files";
        if(!AuthIsDir(files)) continue;              // that game is not here (or this app can't see it)
        std::string t = files + "/TYFMC", mp = t + "/MP";
        if(!AuthIsDir(mp)) { mkdir(t.c_str(), 0775); mkdir(mp.c_str(), 0775); }
        if(AuthIsDir(mp)) add(mp);
    }
    std::string dd = s_szDataDir;                  // and the same path with Android/ <-> Android_unprotected/
    auto swap = [&](const char* a, const char* b) { size_t k = dd.find(a); if(k != std::string::npos) { std::string o = dd; o.replace(k, strlen(a), b); if(AuthIsDir(o)) add(o); } };
    swap("/Android_unprotected/", "/Android/");
    swap("/Android/", "/Android_unprotected/");
    return s_AuthDirs;
}
static std::string AuthFileRead(const char* dir, const char* name)
{
    char p[600]; snprintf(p, sizeof(p), "%s/%s", dir, name);
    std::string o;
    if(FILE* f = fopen(p, "r")) { char b[512]; size_t n; while((n = fread(b, 1, sizeof(b), f)) > 0 && o.size() < 8192) o.append(b, n); fclose(f); }
    return o;
}
static void AuthFileWrite(const char* name, const std::string& body)   // here + every copy (errors ignored)
{
    for(auto& d : AuthDirs())
    {
        char p[600]; snprintf(p, sizeof(p), "%s/%s", d.c_str(), name);
        if(FILE* f = fopen(p, "w")) { fwrite(body.data(), 1, body.size(), f); fclose(f); }
    }
}
static bool s_bAuthFilesUp = false;
static void AuthFilesInit()    // once: files missing in this MP folder come back from a copy
{
    if(s_bAuthFilesUp || !s_szDataDir[0]) return;
    s_bAuthFilesUp = true;
    const auto& dirs = AuthDirs();
    for(const char* name : kAuthFiles)
    {
        if(!AuthFileRead(s_szDataDir, name).empty()) continue;
        for(size_t i = 1; i < dirs.size(); ++i)
        {
            std::string b = AuthFileRead(dirs[i].c_str(), name);
            if(b.empty()) continue;
            char p[600]; snprintf(p, sizeof(p), "%s/%s", s_szDataDir, name);
            if(FILE* f = fopen(p, "w")) { fwrite(b.data(), 1, b.size(), f); fclose(f); }
            logger->Info("5.00.05: %s restored from %s", name, dirs[i].c_str());
            break;
        }
    }
    logger->Info("5.00.05: login files kept in %d MP folder(s)", (int)dirs.size());
}
static std::string AuthFileValue(const char* name, const char* key)
{
    AuthFilesInit();
    char p[512]; snprintf(p, sizeof(p), "%s/%s", s_szDataDir, name);
    std::string u; const size_t kl = strlen(key);
    if(FILE* f = fopen(p, "r"))
    {
        char line[600];
        while(fgets(line, sizeof(line), f))
            if(!strncmp(line, key, kl) && line[kl] == '=') { u = line + kl + 1; while(!u.empty() && (u.back() == '\n' || u.back() == '\r' || u.back() == ' ')) u.pop_back(); }
        fclose(f);
    }
    return u;
}
static bool AuthIsHex32(const std::string& t)
{
    if(t.size() != 32) return false;
    for(char ch : t) if(!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
    return true;
}
// auth_service.txt: dev= this phone's id, user= the account, login= its saved login (no password), discord=,
// users= the accounts of this phone (from the bot), updated= unix time
static void AuthServiceWrite(const std::string& user, const std::string& tok, const std::string& discord, const std::string& users, const std::string& dev)
{
    char head[160]; snprintf(head, sizeof(head), "updated=%lld\n", (long long)time(nullptr));
    std::string b = "# MP login service (5.00.05): keeps this phone logged in (game, Moquil, MP Textdraw Editor).\n"
                    "# No password in here. Don't share this file. Deleted? Open the game: it is made again.\n";
    b += "dev=" + dev + "\nuser=" + user + "\nlogin=" + (AuthIsHex32(tok) ? tok : std::string()) + "\ndiscord=" + discord + "\nusers=" + users + "\n" + head;
    AuthFileWrite("auth_service.txt", b);
}
static void AuthAccountPath(char* out, size_t n) { snprintf(out, n, "%s/account.txt", s_szDataDir); }
// account.txt: user=NAME (the login screen fills it in). 3.22 also kept a login=TOKEN "remember me" token
// there so the game logged in by itself after a restart / mod update. 3.35.2 turned that off (the login screen
// opened after every restart); 5.00.05 keeps the token again, in auth_service.txt + account.txt, so a
// restart / rejoin / mod update stays logged in. Logout (profile menu) ends it on the phone and at the bot.
// (A reconnect inside the same game session still needs no screen: its session lives in memory.)
static const bool AUTH_AUTOLOGIN_AT_START = true;    // 5.00.05: on again - restart / rejoin / mod update stay logged in (auth_service.txt); Logout ends it
static std::string s_szAuthSavedTok;
static std::string AuthAccountValue(const char* key) { return AuthFileValue("account.txt", key); }
static std::string AuthLastUser()
{
    std::string u = AuthAccountValue("user");
    return u.empty() ? AuthFileValue("auth_service.txt", "user") : u;
}
static std::string AuthSavedToken()   // 5.00.05: auth_service.txt login= (3.22 account.txt login= too)
{
    std::string t = AuthFileValue("auth_service.txt", "login");
    if(!AuthIsHex32(t)) t = AuthAccountValue("login");
    return AuthIsHex32(t) ? t : "";
}
// 3.21.8: a random id for this phone (no account data in it). The bot remembers a /gen for it for
// 24 hours, so closing / restarting the game after verifying goes straight to Create your account.
// 5.00.05: also in auth_service.txt and every MP folder copy, so the bot knows this phone after a reinstall.
static std::string s_AuthDevId;
static std::string AuthDeviceId()
{
    if(AuthIsHex32(s_AuthDevId)) return s_AuthDevId;
    std::string id = AuthFileValue("auth_device.txt", "dev");
    if(!AuthIsHex32(id)) id = AuthFileValue("auth_service.txt", "dev");
    if(!AuthIsHex32(id))
    {
        uint8_t r[16]; char h[33];
        if(!AuthCrypto::Random(r, sizeof(r))) return "";
        AuthCrypto::Hex(r, sizeof(r), h); id = h;
    }
    s_AuthDevId = id;
    std::string body = "# MP login: random id of this phone (keeps a Discord /gen across a game restart)\ndev=" + id + "\n";
    if(AuthFileRead(s_szDataDir, "auth_device.txt") != body) AuthFileWrite("auth_device.txt", body);
    return id;
}
static std::vector<std::string> AuthAccounts();
static std::string AuthAccountsCsv()
{
    std::string o; for(auto& x : AuthAccounts()) { if(!o.empty()) o += ","; o += x; }
    return o;
}
static void AuthAccountWrite(const std::string& u, const std::string& tok)
{
    std::string b = "# MP login: the account on this phone (no password is kept). Keep this file when you update the mod:\n"
                    "# the game logs you in by itself. Delete it (or log out) to use another account.\nuser=" + u + "\n";
    if(AuthIsHex32(tok)) b += "login=" + tok + "\n";
    AuthFileWrite("account.txt", b);
}
static void AuthSaveUser(const std::string& u)
{
    const std::string& tok = s_Auth.Session();
    const bool keep = AUTH_AUTOLOGIN_AT_START && AuthIsHex32(tok);
    AuthAccountWrite(u, keep ? tok : "");
    std::string disc = !s_Auth.discordName.empty() ? s_Auth.discordName : AuthFileValue("auth_service.txt", "discord");
    AuthServiceWrite(u, keep ? tok : "", disc, AuthAccountsCsv(), AuthDeviceId());
    s_szAuthSavedTok = keep ? tok : "";
}

// ---- 4.03: up to 3 accounts on this phone (profile menu: Switch account / Add account / Logout) ----
// accounts.txt: one user name a line, the most recent first. No passwords: switching opens the login screen.
static const int AUTH_MAX_ACCOUNTS = 3;
static void AuthAccountsPath(char* out, size_t n) { snprintf(out, n, "%s/accounts.txt", s_szDataDir); }
static std::vector<std::string> AuthAccounts()
{
    AuthFilesInit();
    std::vector<std::string> v; char p[512]; AuthAccountsPath(p, sizeof(p));
    if(FILE* f = fopen(p, "r"))
    {
        char line[96];
        while(fgets(line, sizeof(line), f) && (int)v.size() < AUTH_MAX_ACCOUNTS)
        {
            line[strcspn(line, "\r\n")] = 0;
            if(line[0] && line[0] != '#') { bool dup = false; for(auto& x : v) if(!strcasecmp(x.c_str(), line)) dup = true; if(!dup) v.push_back(line); }
        }
        fclose(f);
    }
    std::string last = AuthAccountValue("user");   // 4.02 and older: only account.txt
    if(!last.empty()) { bool dup = false; for(auto& x : v) if(!strcasecmp(x.c_str(), last.c_str())) dup = true; if(!dup && (int)v.size() < AUTH_MAX_ACCOUNTS) v.push_back(last); }
    return v;
}
static void AuthAccountsSave(const std::vector<std::string>& v)
{
    char h[96]; snprintf(h, sizeof(h), "# MP accounts on this phone (max %d, newest first, no passwords)\n", AUTH_MAX_ACCOUNTS);
    std::string b = h;
    for(int i = 0; i < (int)v.size() && i < AUTH_MAX_ACCOUNTS; ++i) b += v[i] + "\n";
    AuthFileWrite("accounts.txt", b);
}
static void AuthAccountsTouch(const std::string& u)   // logged in as u: to the top
{
    if(u.empty()) return;
    std::vector<std::string> v = AuthAccounts(), n{ u };
    for(auto& x : v) if(strcasecmp(x.c_str(), u.c_str())) n.push_back(x);
    if((int)n.size() > AUTH_MAX_ACCOUNTS) n.resize(AUTH_MAX_ACCOUNTS);
    AuthAccountsSave(n);
}
static void AuthAccountsRemove(const std::string& u)
{
    std::vector<std::string> v = AuthAccounts(), n;
    for(auto& x : v) if(strcasecmp(x.c_str(), u.c_str())) n.push_back(x);
    AuthAccountsSave(n);
}
static void AuthReset();
static void AuthLogout(const char* switchTo, bool addNew, bool quiet = false)   // switchTo: the login screen fills it in next time
{
    std::string was = s_AuthUser.empty() ? s_Auth.user : s_AuthUser;
    {   // 5.00.05: the saved login ends at the bot too (this phone is no longer listed for that account)
        std::string tok = s_Auth.Session(); if(!AuthIsHex32(tok)) tok = AuthSavedToken();
        if(s_Auth.device.empty()) s_Auth.device = AuthDeviceId();
        s_Auth.Logout(tok);
    }
    AuthReset();
    s_Auth.user.clear(); s_AuthUser.clear(); s_Auth.prefillUser = switchTo ? switchTo : "";
    s_Auth.wantNewAccount = addNew;
    s_AfUser.buf[0] = 0; s_AfPass.buf[0] = 0;
    if(switchTo && switchTo[0]) MP_CopyStr(s_AfUser.buf, switchTo, sizeof(s_AfUser.buf));
    AuthAccountWrite(switchTo ? switchTo : "", "");
    AuthServiceWrite(switchTo ? switchTo : "", "", AuthFileValue("auth_service.txt", "discord"), AuthAccountsCsv(), AuthDeviceId());
    if(quiet) return;   // 5.02.81: Settings > Account goes straight on to its login / new account window
    if(addNew) SysMsg(COL_GREEN, "Logged out. Join a login server and tap \"New account\" to make another account (max %d per Discord).", AUTH_MAX_ACCOUNTS);
    else if(switchTo && switchTo[0]) SysMsg(COL_GREEN, "Switched to %s. Join a server and log in.", switchTo);
    else SysMsg(COL_GREEN, "Logged out%s%s.", was.empty() ? "" : " of ", was.c_str());
}

// left the server / lost it / a new connection: the screen goes AND the session login is forgotten
// (3.35.2: leaving the server and joining again = the login screen again, like after a game restart)
static void AuthReset()
{
    s_Auth.ForgetSession(); s_szAuthSavedTok.clear();
    if(s_Auth.state != AuthClient::IDLE && s_Auth.state != AuthClient::DONE) s_Auth.Stop();
    else s_Auth.state = AuthClient::IDLE;
    s_bAuthReq = false; s_szAuthChal[0] = 0; s_bAuthTicket = false; s_nAuthDoneAt = 0;
    s_bAuthAlone = false;   // 5.02.81
    s_nAuthJoinAt = MP_NowMs();
    s_pAfEditing = nullptr;
    if(VKeyboard::IsOpen() && !VKeyboard::IsChat()) VKeyboard::Close();
}

// PKT_AUTH from the server (network thread -> queued by the caller to the game frame)
static void AuthOnPacket(const uint8_t* d, int len)
{
    if(len < 2) return;
    if(d[1] == AUTH_CHALLENGE && len >= (int)sizeof(PktAuthChal))
    {
        PktAuthChal c; memcpy(&c, d, sizeof(c)); c.chal[32] = 0; c.server[31] = 0;
        if(s_bAuthReq && !strcmp(c.chal, s_szAuthChal)) return;   // the same one again (it is repeated)
        s_bAuthAlone = false;   // 5.02.81: a server login replaces a Settings > Account window
        // 3.39: a new challenge = a new join (rejoin, game restart, relay switch): always the login screen
        s_Auth.ForgetSession(); s_szAuthSavedTok.clear();
        if(s_Auth.state != AuthClient::IDLE && s_Auth.state != AuthClient::DONE) s_Auth.Stop();
        s_Auth.state = AuthClient::IDLE; s_nAuthJoinAt = MP_NowMs();
        s_bAuthReq = true; s_nAuthStyle = c.style;
        MP_CopyStr(s_szAuthChal, c.chal, sizeof(s_szAuthChal));
        s_bAuthTicket = false; s_nAuthDoneAt = 0;
        const std::string last = AuthLastUser();
        if(s_Auth.device.empty()) s_Auth.device = AuthDeviceId();
        if(!s_Auth.LoggedIn())
        {   // a fresh game start: the login screen opens (3.22 auto login is off unless AUTH_AUTOLOGIN_AT_START)
            const std::string t = AuthSavedToken();
            if(!t.empty() && AUTH_AUTOLOGIN_AT_START) { s_Auth.RestoreSession(t); s_szAuthSavedTok = t; }
            else if(!t.empty() && !last.empty()) AuthSaveUser(last);   // drop the old login= token, keep the user name
        }
        s_Auth.Begin(c.chal, c.server, !last.empty(), last.c_str());
        if(!s_AfUser.buf[0] && !last.empty()) MP_CopyStr(s_AfUser.buf, last.c_str(), sizeof(s_AfUser.buf));
        SrvAuthPut(client.GetServerName(), client.GetPort(), true);   // a phone that takes it over keeps it on
    }
    else if(d[1] == AUTH_OFF)
    {   // 4.13.15: the server's login system was turned off (default server: by its admin / moderators, or the
        // last of them left): close the login screen, the player is in
        if(s_Auth.state != AuthClient::IDLE && s_Auth.state != AuthClient::DONE) s_Auth.Stop();
        s_Auth.state = AuthClient::IDLE;
        s_bAuthReq = false; s_bAuthTicket = false; s_szAuthChal[0] = 0;
        s_pAfEditing = nullptr;
        if(VKeyboard::IsOpen() && !VKeyboard::IsChat()) VKeyboard::Close();
        SrvAuthPut(client.GetServerName(), client.GetPort(), false);
        SysMsg(COL_SYSTEM, "The login system is off on this server.");
    }
    else if(d[1] == AUTH_RESULT && len >= (int)sizeof(PktAuthResult))
    {
        PktAuthResult r; memcpy(&r, d, sizeof(r)); r.user[23] = 0; r.msg[95] = 0;
        if(!s_bAuthReq || s_Auth.state != AuthClient::WAIT_SERVER) return;
        s_bAuthTicket = false;
        s_Auth.ServerResult(r.ok != 0, r.msg);
        if(r.ok)
        {
            s_nAuthDoneAt = MP_NowMs();
            s_AuthUser = r.user[0] ? r.user : s_Auth.user;
            AuthSaveUser(s_AuthUser);
            AuthAccountsTouch(s_AuthUser);   // 4.03
            s_AfPass.buf[0] = 0;
            if(s_Auth.createdNow) SysMsg(COL_GREEN, "Your account was created successfully. Welcome, %s.", s_AuthUser.c_str());
            else SysMsg(COL_GREEN, "Logged in as %s.", s_AuthUser.c_str());
        }
    }
}

// ---- 3.24: launcher profile (top left): your Discord profile picture + account name ----------
// MP/avatar.png + MP/profile.txt (user= / discord=) are written after a login: the bot sends the
// picture of the Discord account that made the /gen (64 px PNG). No account on this phone yet (never
// verified on Discord / never created one) = the blank default picture and "Not logged in".
static const char* kAvatarFile = "avatar.png";
static std::string s_ProfDiscord, s_ProfOwner;   // profile.txt: whose avatar.png it is
static bool        s_bProfRead = false;
static uint64_t    s_nAvatarAskAt = 0;
static void ProfilePath(char* out, size_t n) { snprintf(out, n, "%s/profile.txt", s_szDataDir); }
static void ProfileRead()
{
    if(s_bProfRead) return;
    s_bProfRead = true;
    char p[512]; ProfilePath(p, sizeof(p));
    if(FILE* f = fopen(p, "r"))
    {
        char line[160];
        while(fgets(line, sizeof(line), f))
        {
            std::string v = line; while(!v.empty() && (v.back() == '\n' || v.back() == '\r')) v.pop_back();
            if(!v.compare(0, 5, "user=")) s_ProfOwner = v.substr(5);
            else if(!v.compare(0, 8, "discord=")) s_ProfDiscord = v.substr(8);
        }
        fclose(f);
    }
}
static void ProfileFrame(uint64_t now)
{
    ProfileRead();
    if(s_Auth.avatarReady)
    {
        s_Auth.avatarReady = false;
        char p[512]; snprintf(p, sizeof(p), "%s/%s", s_szDataDir, kAvatarFile);
        if(FILE* f = fopen(p, "wb")) { fwrite(s_Auth.avatarPng.data(), 1, s_Auth.avatarPng.size(), f); fclose(f); }
        s_ProfOwner = s_AuthUser.empty() ? s_Auth.user : s_AuthUser;
        if(!s_Auth.avatarDiscord.empty()) s_ProfDiscord = s_Auth.avatarDiscord;
        ProfilePath(p, sizeof(p));
        if(FILE* f = fopen(p, "w")) { fprintf(f, "# MP launcher profile (your Discord picture is avatar.png)\nuser=%s\ndiscord=%s\n", s_ProfOwner.c_str(), s_ProfDiscord.c_str()); fclose(f); }
        HUD::ReloadTexture(kAvatarFile);
        s_Auth.avatarPng.clear();
        logger->Info("Profile: Discord avatar saved for %s", s_ProfOwner.c_str());
    }
    // logged in during this game session: get the picture (again every 30 min: a changed Discord pfp)
    if(s_Auth.LoggedIn() && s_nAuthDoneAt && !s_Auth.avatarPending &&
       (!s_nAvatarAskAt || now - s_nAvatarAskAt > 30 * 60 * 1000ull) && now - s_nAuthDoneAt > 3000)
    { s_nAvatarAskAt = now; s_Auth.RequestAvatar(); }
}
// the account shown in the launcher ("" = none on this phone: blank picture)
static std::string ProfileUser()
{
    if(!s_AuthUser.empty()) return s_AuthUser;
    return AuthLastUser();
}
// draws the round picture: Discord avatar if this account has one, else the blank default
static void ProfileDrawAvatar(ImDrawList* dl, ImVec2 c, float r)
{
    ProfileRead();
    const std::string u = ProfileUser();
    ImVec2 uv1;
    void* tex = (!u.empty() && !strcasecmp(u.c_str(), s_ProfOwner.c_str())) ? HUD::Texture(kAvatarFile, &uv1) : nullptr;
    if(tex)
    {
        dl->AddImageRounded((ImTextureID)tex, ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), ImVec2(0, 0), uv1, IM_COL32_WHITE, r);
        dl->AddCircle(c, r, IM_COL32(255, 255, 255, 90), 40, UI::S(2));
        return;
    }
    // blank default: grey disc + a plain head-and-shoulders silhouette
    dl->AddCircleFilled(c, r, IM_COL32(70, 72, 78, 255), 40);
    dl->PushClipRect(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), true);
    dl->AddCircleFilled(ImVec2(c.x, c.y - r * 0.18f), r * 0.36f, IM_COL32(150, 153, 160, 255), 28);
    dl->AddCircleFilled(ImVec2(c.x, c.y + r * 0.95f), r * 0.68f, IM_COL32(150, 153, 160, 255), 32);
    dl->PopClipRect();
    dl->AddCircle(c, r, IM_COL32(255, 255, 255, 60), 40, UI::S(2));
}

// ---- 5.00.05: saved login sync with the bot -----------------------------------------------------------
// ~8 s after the game starts (and every minute on a login screen) the phone asks the bot which accounts
// this auth_device id is registered to and for its saved login. The answer (also pushed after Discord
// /forgotpass, /forgotuser, /create) rewrites auth_service.txt, account.txt and accounts.txt in every MP folder:
// deleted files come back, a new password keeps the phone logged in.
static uint64_t s_nAuthSyncAt = 0, s_nAuthStartAt = 0;
static void AuthAdoptFiles(bool force);   // 5.02.81
static long long s_nAuthFileSig = -1;     // 5.02.81: account.txt / auth_service.txt size + time (changed on disk?)
static long long AuthFilesSig()
{
    long long sig = 0; struct stat st;
    for(const char* n : { "auth_service.txt", "account.txt" })
    { char p[600]; snprintf(p, sizeof(p), "%s/%s", s_szDataDir, n); if(!stat(p, &st)) sig = sig * 31 + (long long)st.st_mtime * 7 + (long long)st.st_size; }
    return sig;
}
static void AuthSyncFrame(uint64_t now)
{
    if(!s_szDataDir[0]) return;
    if(s_Auth.device.empty())
    {
        s_Auth.device = AuthDeviceId();
        s_Auth.app = strstr(s_szDataDir, "gtavc") ? "game-vc" : (strstr(s_szDataDir, "gtasa") ? "game-sa" : "game");
    }
    if(!s_nAuthStartAt) s_nAuthStartAt = now;
    if((!s_nAuthSyncAt && now - s_nAuthStartAt > 8000) ||
       (s_nAuthSyncAt && s_Auth.state == AuthClient::LOGIN && !s_Auth.LoggedIn() && now - s_nAuthSyncAt > 60000))
    { s_nAuthSyncAt = now; s_Auth.Sync(AuthSavedToken(), AuthAccountsCsv()); }
    if(!s_Auth.syncReady) return;
    s_Auth.syncReady = false;
    const std::string su = s_Auth.syncUser, st = s_Auth.syncTok;
    if(!s_Auth.syncGone.empty())
    {   // 5.02.81: accounts deleted at the bot (/delete) leave accounts.txt (they could be "switched to" and showed offline)
        std::vector<std::string> keep; const std::string g = "," + s_Auth.syncGone + ",";
        for(auto& x : AuthAccounts())
        {
            std::string k = "," + x + ","; bool gone = false;
            for(size_t i = 0; i + k.size() <= g.size() && !gone; ++i) gone = !strncasecmp(g.c_str() + i, k.c_str(), k.size());
            if(!gone) keep.push_back(x);
            else logger->Info("5.02.81: account %s was deleted at the bot - removed from accounts.txt", x.c_str());
        }
        AuthAccountsSave(keep);
        s_Auth.syncGone.clear();
    }
    if(s_Auth.syncDead && su.empty())
    {   // 5.02.81: the saved login is for an account that was deleted (and maybe made again with the same name and
        // password - a new account): forget the dead login, keep the name, show "log in" (not a stuck "offline")
        s_Auth.syncDead = false;
        const std::string tok = AuthSavedToken(), last = AuthLastUser();
        if(!s_bAuthReq && (s_Auth.Session() == tok || s_Auth.Session().empty())) { s_Auth.ForgetSession(); s_AuthUser.clear(); }
        s_szAuthSavedTok.clear(); s_bProfRead = false;
        AuthAccountWrite(last, "");
        AuthServiceWrite(last, "", "", AuthAccountsCsv(), AuthDeviceId());
        logger->Info("5.02.81: the saved login of %s ended at the bot (account deleted / made again): log in again", last.empty() ? "?" : last.c_str());
        if(!last.empty()) SysMsg(COL_YELLOW, "Your saved login of %s is no longer valid (the account was deleted or made again). Log in again: Settings > Account.", last.c_str());
        return;
    }
    s_Auth.syncDead = false;
    std::vector<std::string> bot;
    for(size_t p = 0; p <= s_Auth.syncUsers.size();)
    {
        size_t e = s_Auth.syncUsers.find(',', p); if(e == std::string::npos) e = s_Auth.syncUsers.size();
        if(e > p) bot.push_back(s_Auth.syncUsers.substr(p, e - p));
        p = e + 1;
    }
    // accounts.txt: the synced account first, then the ones already here, then the rest the bot knows
    std::vector<std::string> m;
    auto put = [&](const std::string& u) { if(u.empty() || (int)m.size() >= AUTH_MAX_ACCOUNTS) return; for(auto& x : m) if(!strcasecmp(x.c_str(), u.c_str())) return; m.push_back(u); };
    put(su); for(auto& x : AuthAccounts()) put(x); for(auto& x : bot) put(x);
    if(!m.empty()) AuthAccountsSave(m);
    const std::string cur = !s_AuthUser.empty() ? s_AuthUser : AuthLastUser();
    const bool other = s_Auth.LoggedIn() && !s_AuthUser.empty() && !su.empty() && strcasecmp(s_AuthUser.c_str(), su.c_str());
    std::string disc = !s_Auth.syncDiscord.empty() ? s_Auth.syncDiscord : AuthFileValue("auth_service.txt", "discord");
    if(!su.empty() && AuthIsHex32(st) && !other)
    {   // a saved login for this phone: keep it (and use it now on a login screen)
        AuthAccountWrite(su, st);
        AuthServiceWrite(su, st, disc, AuthAccountsCsv(), AuthDeviceId());
        s_szAuthSavedTok = st;
        if(s_bAuthReq) s_Auth.UseSession(st);
        if(!s_bAuthReq && (s_AuthUser.empty() || !strcasecmp(s_AuthUser.c_str(), su.c_str())))
        {   // 5.02.81: online with the saved login (top right of the launcher / neon menu, Settings > Account)
            if(s_Auth.Session() != st) { s_Auth.ForgetSession(); s_Auth.RestoreSession(st); }
            if(s_AuthUser.empty()) { s_AuthUser = su; s_nAuthDoneAt = now; s_bProfRead = false; }
        }
        if(!s_AfUser.buf[0] || strcasecmp(s_AfUser.buf, su.c_str())) MP_CopyStr(s_AfUser.buf, su.c_str(), sizeof(s_AfUser.buf));
        logger->Info("5.00.05: saved login of %s synced from the bot", su.c_str());
    }
    else if(su.empty() && !s_Auth.LoggedIn() && !AuthSavedToken().empty())
    {   // the saved login ended at the bot (password changed with logout, 180 days): keep only the name
        AuthAccountWrite(cur, "");
        AuthServiceWrite(cur, "", disc, AuthAccountsCsv(), AuthDeviceId());
        s_szAuthSavedTok.clear();
    }
    else AuthServiceWrite(cur, AuthSavedToken(), disc, AuthAccountsCsv(), AuthDeviceId());   // files back / up to date
}

// every game frame
static void AuthFrame(uint64_t now)
{
    if(client.GetState() == NET_DISCONNECTED) { if(s_bAuthReq) AuthReset(); }
    s_Auth.Tick(now);    // (returns at once when nothing is going on)
    {   // 5.02.81: account.txt / auth_service.txt changed by someone else (another MP folder, AML Multiplayer app)
        static uint64_t s_nFileAt = 0;
        if(!s_nFileAt || now - s_nFileAt > 3000) { s_nFileAt = now; AuthAdoptFiles(false); }
    }
    AuthSyncFrame(now);  // 5.00.05
    ProfileFrame(now);   // 3.24
    if(!s_szAuthSavedTok.empty() && !s_Auth.LoggedIn())
    {   // 3.22: the bot said the saved login ended (180 days unused / account gone): forget it, keep the name
        s_szAuthSavedTok.clear();
        const std::string last = AuthLastUser();
        if(!last.empty()) AuthSaveUser(last);
    }
    {   // 4.12.4: a server remembered as "login on" that never asked for a login (8 s connected, no challenge)
        // has it off now: forget it, so this phone doesn't wait for a login there / turn it on when it hosts it
        static uint64_t s_nConnAt = 0; static bool s_bChecked = false;
        if(client.GetState() != NET_CONNECTED) { s_nConnAt = 0; s_bChecked = false; }
        else if(!s_nConnAt) s_nConnAt = now;
        else if(!s_bChecked && !s_bAuthReq && now - s_nConnAt > 8000)
        {
            s_bChecked = true;
            const char* sn = client.GetServerName();
            if(sn[0] && !MP::IsDefaultServer(sn, client.GetPort()) && !MP_IsBcwServerName(sn) && !MP_IsMw3ServerName(sn) && !MP_IsRpServerName(sn) && !MP_IsDzServerName(sn) && !MP_IsLgServerName(sn) && SrvAuthGet(sn, client.GetPort()) > 0)
            { SrvAuthPut(sn, client.GetPort(), false); logger->Info("4.12.4: %s has no login system any more - forgotten", sn); }
        }
    }
    if(s_bAuthAlone)
    {   // 5.02.81: Settings > Account login: the bot said yes (its ticket is for no server - not needed)
        std::string tp; uint8_t tsig[64];
        if(s_Auth.TakeTicket(tp, tsig))
        {
            s_Auth.ServerResult(true, "");
            s_nAuthDoneAt = now; s_nAvatarAskAt = 0; s_bProfRead = false;
            s_AuthUser = !s_Auth.user.empty() ? s_Auth.user : std::string(s_AfUser.buf);
            AuthSaveUser(s_AuthUser);          // account.txt + auth_service.txt (every MP folder): the new login
            AuthAccountsTouch(s_AuthUser);
            s_szAuthSavedTok = s_Auth.Session();
            s_nAuthFileSig = AuthFilesSig();   // our own write: not "changed by someone else"
            s_AfPass.buf[0] = 0;
            if(s_Auth.createdNow) SysMsg(COL_GREEN, "Your account was created successfully. Welcome, %s.", s_AuthUser.c_str());
            else SysMsg(COL_GREEN, "Logged in as %s.", s_AuthUser.c_str());
            logger->Info("5.02.81: Settings > Account: logged in as %s (account.txt / auth_service.txt written)", s_AuthUser.c_str());
        }
        if(s_Auth.state == AuthClient::DONE && now - s_nAuthDoneAt > 2600) s_bAuthAlone = false;
        if(s_Auth.state == AuthClient::IDLE) s_bAuthAlone = false;
    }
    if(!s_bAuthReq) return;
    std::string p; uint8_t sig[64];
    if(s_Auth.TakeTicket(p, sig)) { s_AuthTicket = p; memcpy(s_AuthSig, sig, 64); s_bAuthTicket = true; s_nAuthTicketAt = 0; }
    if(s_bAuthTicket && s_Auth.state == AuthClient::WAIT_SERVER && now - s_nAuthTicketAt > 1500 && client.GetState() == NET_CONNECTED)
    {   // UDP: again until the server answers
        s_nAuthTicketAt = now;
        uint8_t buf[sizeof(PktAuthTicket) + MP_AUTH_TICKET_MAX];
        PktAuthTicket h; h.type = PKT_AUTH; h.op = AUTH_TICKET; h.plen = (uint16_t)std::min<size_t>(s_AuthTicket.size(), MP_AUTH_TICKET_MAX);
        memcpy(h.sig, s_AuthSig, 64);
        memcpy(buf, &h, sizeof(h)); memcpy(buf + sizeof(h), s_AuthTicket.data(), h.plen);
        client.Send(buf, (int)(sizeof(h) + h.plen));
    }
}

// the < Spawn > buttons (and the Brief Case War teams) wait for the login
static bool RegHoldsJoin(uint64_t now);   // 5.02.35 (mp_register.inc)
static bool AuthHoldsJoin(uint64_t now)
{
    if(RegHoldsJoin(now)) return true;   // 5.02.35: Broker MQQ register / login on the connection screen first
    // 4.12.8: the spawn picker waits for a save load (default server: the 100% save; another server right
    // after the default one: your own save back). Guests have no login to wait for, so their picker used to
    // open first and the load then moved them away from the spot they picked. Capped at 30 s.
    if((Progress::Pending() || (Progress::IsActive() && !MP::IsOnDefault())) && now - s_nAuthJoinAt < 30000) return true;
    if(!s_bAuthReq)
    {   // the challenge comes right after the welcome: a short wait on servers known to use it
        const char* sn = client.GetServerName();
        const bool expected = MP_IsBcwServerName(sn) || SrvAuthGet(sn, client.GetPort()) > 0;   // 4.13.15: the default server's login is off by default
        return expected && now - s_nAuthJoinAt < 3500;
    }
    if(s_Auth.state != AuthClient::DONE) return true;
    return s_Auth.createdNow && now - s_nAuthDoneAt < 2600;   // "Account created successfully" first
}
// 3.21.8: the login pop-up waits for "Connected successfully!" (the Connecting.... / resources feed done
// and the black join screen slid away); the login itself already runs in the background
// 3.22.1: on the default server the 100% progress save loads ~4 s after the join; the login pop-up waits
// for that load (it used to open first and the reload threw it away). Capped at 30 s so it never hangs.
static bool AuthJoinSettled()
{
    if(s_nFeedStep >= 0 || s_nFeedRes >= 0) return false;
    if(Progress::Pending() && MP_NowMs() - s_nAuthJoinAt < 30000) return false;
    return s_nJoinPhase == JP_NONE || s_nJoinPhase >= JP_WAIT;
}
static bool AuthScreenUp()
{
    if(s_bAuthAlone && !s_bAuthReq)
    {   // 5.02.81: Settings > Account window
        if(s_Auth.state == AuthClient::IDLE) return false;
        if(s_Auth.state == AuthClient::DONE) return s_Auth.createdNow && MP_NowMs() - s_nAuthDoneAt < 2600;
        return true;
    }
    if(!s_bAuthReq) return false;
    if(!AuthJoinSettled()) return false;
    if(s_Auth.state == AuthClient::IDLE) return false;
    if(s_Auth.state == AuthClient::DONE) return s_Auth.createdNow && MP_NowMs() - s_nAuthDoneAt < 2600;
    return true;
}
bool MP::AuthScreenShown() { return AuthScreenUp(); }

// ---- drawing ------------------------------------------------------------------------------------
struct AuthLook
{
    ImU32 panel, header, accent, accentHeld, text, dim, field, fieldBorder, btn, btnHeld, btnText, err, ok;
};
static AuthLook AuthStyleLook(bool bcw, bool mw3 = false, bool rp = false, bool dz = false)
{
    AuthLook l;
    if(dz)
    {   // 3.58: Project Day Z Legacy - the MW3 layout + gfx, in yellow
        l.panel = IM_COL32(12, 10, 4, 238); l.header = IM_COL32(161, 120, 8, 245); l.accent = IM_COL32(250, 204, 21, 255);
        l.accentHeld = IM_COL32(254, 240, 138, 255); l.text = IM_COL32(255, 255, 255, 255); l.dim = IM_COL32(234, 222, 180, 255);
        l.field = IM_COL32(255, 255, 255, 22); l.fieldBorder = IM_COL32(250, 204, 21, 210); l.btn = IM_COL32(202, 150, 10, 240);
        l.btnHeld = IM_COL32(255, 255, 255, 240); l.btnText = IM_COL32(20, 16, 4, 255);
    }
    else if(rp)
    {   // 3.25: Mega Vil Futuristic - the same layout, in sky blue
        l.panel = IM_COL32(4, 12, 24, 238); l.header = IM_COL32(14, 116, 168, 245); l.accent = IM_COL32(56, 189, 248, 255);
        l.accentHeld = IM_COL32(186, 230, 253, 255); l.text = IM_COL32(255, 255, 255, 255); l.dim = IM_COL32(186, 222, 245, 255);
        l.field = IM_COL32(255, 255, 255, 22); l.fieldBorder = IM_COL32(56, 189, 248, 210); l.btn = IM_COL32(14, 150, 215, 240);
        l.btnHeld = IM_COL32(255, 255, 255, 240); l.btnText = IM_COL32(255, 255, 255, 255);
    }
    else if(mw3)
    {   // 3.23: Modern Warfare 3 - the same layout as Brief Case War, in green
        l.panel = IM_COL32(6, 10, 7, 236); l.header = IM_COL32(18, 110, 40, 245); l.accent = IM_COL32(60, 255, 106, 255);
        l.accentHeld = IM_COL32(170, 255, 190, 255); l.text = IM_COL32(255, 255, 255, 255); l.dim = IM_COL32(190, 215, 195, 255);
        l.field = IM_COL32(255, 255, 255, 22); l.fieldBorder = IM_COL32(60, 255, 106, 200); l.btn = IM_COL32(30, 150, 58, 240);
        l.btnHeld = IM_COL32(255, 255, 255, 240); l.btnText = IM_COL32(255, 255, 255, 255);
    }
    else if(bcw)
    {
        l.panel = IM_COL32(8, 8, 10, 236); l.header = IM_COL32(150, 12, 18, 245); l.accent = IM_COL32(230, 30, 40, 255);
        l.accentHeld = IM_COL32(255, 80, 80, 255); l.text = IM_COL32(255, 255, 255, 255); l.dim = IM_COL32(205, 190, 190, 255);
        l.field = IM_COL32(255, 255, 255, 22); l.fieldBorder = IM_COL32(230, 30, 40, 200); l.btn = IM_COL32(200, 20, 30, 240);
        l.btnHeld = IM_COL32(255, 255, 255, 240); l.btnText = IM_COL32(255, 255, 255, 255);
    }
    else
    {
        l.panel = IM_COL32(0, 0, 0, 215); l.header = IM_COL32(25, 25, 30, 240); l.accent = IM_COL32(169, 196, 228, 255);
        l.accentHeld = IM_COL32(220, 235, 255, 255); l.text = IM_COL32(255, 255, 255, 255); l.dim = IM_COL32(169, 196, 228, 255);
        l.field = IM_COL32(255, 255, 255, 30); l.fieldBorder = IM_COL32(169, 196, 228, 160); l.btn = IM_COL32(70, 70, 78, 230);
        l.btnHeld = IM_COL32(169, 196, 228, 140); l.btnText = IM_COL32(255, 255, 255, 255);
    }
    l.err = IM_COL32(255, 90, 90, 255); l.ok = IM_COL32(80, 220, 110, 255);
    return l;
}

// Brief Case War: 3.22.1 no red background at all - the same plain dim layer as the normal login screen
// (the red VFX, the red corner tint and the glowing red frame around the panel are gone).
static void AuthBcwBackdrop(ImDrawList* dl, ImVec2 ds, float /*t*/)
{
    dl->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(0, 0, 0, 120));
}

static void OnAuthFieldText(const char* text, void* data)
{
    AuthField* f = (AuthField*)data;
    if(f) MP_CopyStr(f->buf, text ? text : "", sizeof(f->buf));
    s_pAfEditing = nullptr;
}

struct AuthPanel
{
    ImDrawList* dl; ImFont* f; AuthLook L; ImVec2 p0; float w, pad, fs, y; int ids;
    void Text(const char* t, ImU32 c, float size = 0, bool centre = false)
    {
        float s = size > 0 ? size : fs;
        ImVec2 ts = f->CalcTextSizeA(s, FLT_MAX, w - pad * 2, t);
        float x = centre ? p0.x + (w - ts.x) * 0.5f : p0.x + pad;
        dl->AddText(f, s, ImVec2(x, y), c, t, nullptr, w - pad * 2);
        y += ts.y + UI::S(8);
    }
    bool Button(const char* lab, float bx, float bw, float bh, bool primary)
    {
        ImVec2 a(bx, y), b(bx + bw, y + bh);
        ImGui::SetCursorScreenPos(a); ImGui::PushID(ids++);
        bool hit = ImGui::InvisibleButton("##ab", ImVec2(bw, bh)); bool held = ImGui::IsItemActive();
        ImGui::PopID();
        dl->AddRectFilled(a, b, held ? L.btnHeld : primary ? L.btn : IM_COL32(60, 60, 66, 220), UI::S(5));
        if(primary) dl->AddRect(a, b, L.accent, UI::S(5), 0, UI::S(2));
        ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0, lab);
        dl->AddText(f, fs, ImVec2(a.x + (bw - ts.x) * 0.5f, a.y + (bh - ts.y) * 0.5f), held && primary ? IM_COL32(0, 0, 0, 255) : L.btnText, lab);
        return hit;
    }
    void Field(AuthField& fd)
    {
        const float rowH = UI::S(62), lw = UI::S(170);
        ImVec2 a(p0.x + pad + lw, y), b(p0.x + w - pad, y + rowH);
        dl->AddText(f, fs, ImVec2(p0.x + pad, y + (rowH - fs) * 0.5f), L.dim, fd.title);
        ImGui::SetCursorScreenPos(a); ImGui::PushID(ids++);
        if(ImGui::InvisibleButton("##af", ImVec2(b.x - a.x, rowH)))
        {
            s_pAfEditing = &fd;
            VKeyboard::Open(fd.title, fd.buf, fd.max, OnAuthFieldText, &fd, fd.numeric);
        }
        ImGui::PopID();
        const bool editing = s_pAfEditing == &fd && VKeyboard::IsOpen() && !VKeyboard::IsChat();
        dl->AddRectFilled(a, b, L.field, UI::S(5));
        dl->AddRect(a, b, editing ? L.accentHeld : L.fieldBorder, UI::S(5), 0, editing ? UI::S(3) : UI::S(1.5f));
        std::string shown = editing ? VKeyboard::GetText() : fd.buf;
        if(fd.secret) shown.assign(shown.size(), '*');
        if(shown.empty() && !editing) dl->AddText(f, fs, ImVec2(a.x + UI::S(14), a.y + (rowH - fs) * 0.5f), IM_COL32(255, 255, 255, 90), "tap to type");
        else dl->AddText(f, fs, ImVec2(a.x + UI::S(14), a.y + (rowH - fs) * 0.5f), L.text, shown.c_str());
        y += rowH + UI::S(12);
    }
};

static void AuthTakeField(AuthField& fd)   // a button pressed while the keyboard is still open
{
    if(s_pAfEditing == &fd && VKeyboard::IsOpen() && !VKeyboard::IsChat()) { MP_CopyStr(fd.buf, VKeyboard::GetText(), sizeof(fd.buf)); VKeyboard::Close(); s_pAfEditing = nullptr; }
}

// ---- 5.02.81: Settings > Account ---------------------------------------------------------------------------
static void RenderAuth();
static void AuthAloneClose()
{
    if(s_Auth.state != AuthClient::IDLE && s_Auth.state != AuthClient::DONE) s_Auth.Stop();
    else s_Auth.state = AuthClient::IDLE;
    s_bAuthAlone = false; s_pAfEditing = nullptr;
    if(VKeyboard::IsOpen() && !VKeyboard::IsChat()) VKeyboard::Close();
}
// create = Discord code -> Create your account; else the Login screen (user filled in). A new login replaces the
// old one on this phone: the old saved login is ended first, then account.txt / auth_service.txt get the new one.
static void AuthAloneBegin(bool create, const char* user)
{
    if(s_bAuthReq) { SysMsg(COL_YELLOW, "Finish the login screen of this server first (or leave the server)."); return; }
    if(s_Auth.state != AuthClient::IDLE && s_Auth.state != AuthClient::DONE) s_Auth.Stop();
    const std::string want = user && *user ? std::string(user) : (create ? std::string() : AuthLastUser());
    if(s_Auth.LoggedIn() || !AuthSavedToken().empty() || !s_AuthUser.empty()) AuthLogout(want.c_str(), create, true);
    uint8_t r[16];
    if(!AuthCrypto::Random(r, sizeof(r))) for(int i = 0; i < 16; ++i) r[i] = (uint8_t)(lrand48() >> 3);
    AuthCrypto::Hex(r, sizeof(r), s_szAloneChal);
    if(s_Auth.device.empty()) s_Auth.device = AuthDeviceId();
    s_Auth.ForgetSession(); s_Auth.recover = 0;
    s_Auth.wantNewAccount = create; s_Auth.prefillUser = want;
    s_AfUser.buf[0] = 0; s_AfPass.buf[0] = 0;
    if(!want.empty()) MP_CopyStr(s_AfUser.buf, want.c_str(), sizeof(s_AfUser.buf));
    s_bAuthAlone = true; s_bAuthAloneTop = true;
    s_Auth.Begin(s_szAloneChal, "MP account", !create, want.c_str());
    logger->Info("5.02.81: Settings > Account: %s", create ? "new account" : "login");
}
// account.txt / auth_service.txt changed on disk: use the login in them (the "file reader")
static void AuthAdoptFiles(bool force)
{
    if(!s_szDataDir[0]) return;
    const long long sig = AuthFilesSig();
    if(!force && sig == s_nAuthFileSig) return;
    const bool first = s_nAuthFileSig < 0;
    s_nAuthFileSig = sig;
    if(first && !force) return;
    if(s_bAuthReq || s_bAuthAlone) return;   // a login window is up: it writes them itself
    const std::string tok = AuthSavedToken(), u = AuthLastUser();
    if(!force && tok == s_szAuthSavedTok && (u.empty() || !strcasecmp(u.c_str(), ProfileUser().c_str()))) return;
    logger->Info("5.02.81: the account files changed (%s): reading them again", u.empty() ? "no account" : u.c_str());
    s_Auth.ForgetSession(); s_AuthUser.clear(); s_bProfRead = false;
    if(AuthIsHex32(tok)) { s_Auth.RestoreSession(tok); s_szAuthSavedTok = tok; }
    else s_szAuthSavedTok.clear();
    s_nAuthSyncAt = 0; s_nAuthStartAt = MP_NowMs() - 7000;   // ask the bot in ~1 s: whose login is this? (-> online)
}
static void SectionAccount(void*)
{
    const std::string u = ProfileUser(), saved = AuthSavedToken();
    const bool on = s_Auth.LoggedIn() && !s_AuthUser.empty();
    const std::string disc = !s_Auth.syncDiscord.empty() ? s_Auth.syncDiscord : AuthFileValue("auth_service.txt", "discord");
    ImGui::TextDisabled("Your MP account (Discord verified). It is kept in account.txt and auth_service.txt in every MP folder of this phone.");
    ImGui::Dummy(ImVec2(0, UI::S(6)));
    {
        ImDrawList* dl = ImGui::GetWindowDrawList(); const float r = UI::S(34);
        const ImVec2 c0 = ImGui::GetCursorScreenPos();
        ProfileDrawAvatar(dl, ImVec2(c0.x + r, c0.y + r), r);
        dl->AddCircleFilled(ImVec2(c0.x + r * 1.72f, c0.y + r * 1.72f), UI::S(8), on ? IM_COL32(67, 181, 129, 255) : IM_COL32(116, 127, 141, 255), 16);
        ImGui::SetCursorScreenPos(ImVec2(c0.x + r * 2 + UI::S(16), c0.y + UI::S(4)));
        ImGui::BeginGroup();
        ImGui::TextUnformatted(u.empty() ? "Guest - not logged in" : u.c_str());
        if(on) ImGui::TextColored(ImVec4(0.37f, 0.83f, 0.37f, 1), "Online%s%s", disc.empty() ? "" : "  |  Discord @", disc.c_str());
        else if(!u.empty() && AuthIsHex32(saved)) ImGui::TextColored(ImVec4(0.98f, 0.75f, 0.3f, 1), "Saved login - checking it with the MP bot...");
        else if(!u.empty()) ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1), "Offline - log in again");
        else ImGui::TextDisabled("No account on this phone yet");
        ImGui::EndGroup();
        ImGui::SetCursorScreenPos(ImVec2(c0.x, c0.y + r * 2 + UI::S(12)));
        ImGui::Dummy(ImVec2(1, 1));
    }
    if(s_bAuthReq) ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "This server has its own login screen open: finish it there.");
    if(s_bAuthAlone)
    {
        ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "The account window is open.");
        if(FloatMenu::BigButton("Cancel")) AuthAloneClose();
    }
    if(s_bAuthReq || s_bAuthAlone) ImGui::BeginDisabled();
    if(FloatMenu::BigButton(on ? "Log in to another account" : "Log in")) AuthAloneBegin(false, on ? "" : u.c_str());
    if(FloatMenu::BigButton("Create a new account")) AuthAloneBegin(true, "");
    if(s_bAuthReq || s_bAuthAlone) ImGui::EndDisabled();
    if(!u.empty() || !saved.empty())
        if(FloatMenu::BigButton("Log out")) { AuthLogout("", false); s_bProfRead = false; s_nAuthFileSig = AuthFilesSig(); }
    const std::vector<std::string> accs = AuthAccounts();
    bool others = false;
    for(auto& a : accs) if(strcasecmp(a.c_str(), u.c_str())) others = true;
    if(others && !s_bAuthReq && !s_bAuthAlone)
    {
        ImGui::Dummy(ImVec2(0, UI::S(6)));
        ImGui::TextUnformatted("Switch account (accounts on this phone)");
        for(size_t i = 0; i < accs.size(); ++i)
        {
            if(!strcasecmp(accs[i].c_str(), u.c_str())) continue;
            char b[96]; snprintf(b, sizeof(b), "%s##acc%d", accs[i].c_str(), (int)i);
            if(FloatMenu::BigButton(b)) AuthAloneBegin(false, accs[i].c_str());
        }
        ImGui::TextDisabled("Deleted accounts (Discord /delete) leave this list by themselves.");
    }
    ImGui::Dummy(ImVec2(0, UI::S(6)));
    if(FloatMenu::BigButton("Reload the account files")) { AuthAdoptFiles(true); SysMsg(COL_SYSTEM, "Reading account.txt / auth_service.txt again..."); }
    ImGui::TextDisabled("A new account.txt / auth_service.txt (copied in, or written by the AML Multiplayer app) is read again by itself within 3 s.");
    { char p[600]; snprintf(p, sizeof(p), "%s/auth_service.txt", s_szDataDir); ImGui::TextDisabled("%s", p); }
    RenderAuth();   // the account window (also in the main menu, where the game frame does not draw it)
}

static void RenderAuth()
{
    if(!AuthScreenUp() || (!Game::IsReady() && !s_bAuthAlone)) return;
    {   // 5.02.81: drawn by the game frame AND by Settings > Account (main menu): once a frame
        static int s_nDrawn = -1;
        if(ImGui::GetFrameCount() == s_nDrawn) return;
        s_nDrawn = ImGui::GetFrameCount();
    }
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const bool autoLogin = s_Auth.LoggedIn() && (s_Auth.state == AuthClient::LINKING || s_Auth.state == AuthClient::TICKET ||
                           (s_Auth.state == AuthClient::WAIT_SERVER && !s_Auth.createdNow));
    if((s_Auth.state == AuthClient::LINKING || autoLogin) && s_Auth.err.empty() && !s_bAuthAlone)
    {   // 3.21.7: the login screen opens only once the login server is connected; until then a small line
        // 3.22: a saved login (account.txt) never opens the pop-up at all: "Logging in as NAME..."
        static const char* dots[4] = { "", ".", "..", "..." };
        std::string who = s_Auth.prefillUser; if(who.empty()) who = "your account";
        char base[96];
        if(autoLogin) snprintf(base, sizeof(base), "Logging in as %s", who.c_str());
        else snprintf(base, sizeof(base), "Connecting to the login server");
        char b[112]; snprintf(b, sizeof(b), "%s%s", base, dots[(MP_NowMs() / 400) % 4]);
        char full[112]; snprintf(full, sizeof(full), "%s...", base);
        ImDrawList* fg = ImGui::GetForegroundDrawList(); ImFont* f = UI::Font(); const float fs = UI::S(28);
        ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0, full);
        ImVec2 a0((ds.x - ts.x) * 0.5f - UI::S(18), UI::S(14)), a1((ds.x + ts.x) * 0.5f + UI::S(18), UI::S(14) + ts.y + UI::S(16));
        fg->AddRectFilled(a0, a1, IM_COL32(0, 0, 0, 170), UI::S(8));
        fg->AddText(f, fs, ImVec2(a0.x + UI::S(18), a0.y + UI::S(8)), IM_COL32(255, 255, 255, 230), b);
        return;
    }
    const bool dz  = s_nAuthStyle == AUTH_STYLE_DZ || MP::IsOnDz();    // 3.58: Project Day Z = yellow (MW3 gfx)
    const bool rp  = !dz && (s_nAuthStyle == AUTH_STYLE_RP || MP::IsOnRp());    // 3.25: Mega Vil Futuristic = sky blue
    const bool mw3 = !rp && !dz && (s_nAuthStyle == AUTH_STYLE_MW3 || MP::IsOnMw3());   // 3.23
    const bool bcw = s_nAuthStyle == AUTH_STYLE_BCW || mw3 || rp || dz;
    const float t = (float)(MP_NowMs() % 3600000ull) / 1000.0f;
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    if(bcw) AuthBcwBackdrop(bg, ds, t);
    else bg->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(0, 0, 0, 120));

    AuthPanel P; P.f = UI::Font(); P.L = AuthStyleLook(bcw, mw3, rp, dz); P.pad = UI::S(28); P.fs = UI::S(30); P.ids = 0;
    P.w = fminf(fmaxf(UI::S(760), ds.x * 0.46f), ds.x * 0.94f);
    const AuthClient::State st = s_Auth.state;
    float h = UI::S(st == AuthClient::SIGNUP ? 700 : st == AuthClient::LOGIN ? 640 : st == AuthClient::CODE ? 680 : st == AuthClient::RESET ? 600 : 380);
    if(bcw) h += UI::S(90);
    h = fminf(h, ds.y * 0.96f);
    P.p0 = ImVec2((ds.x - P.w) * 0.5f, (ds.y - h) * 0.5f);
    if(VKeyboard::IsOpen() && !VKeyboard::IsChat()) P.p0.y = fmaxf(UI::S(6), fminf(P.p0.y, ds.y - VKeyboard::Height() - h - UI::S(6)));

    ImGui::SetNextWindowPos(P.p0); ImGui::SetNextWindowSize(ImVec2(P.w, h));
    if(s_bAuthAlone && s_bAuthAloneTop) { ImGui::SetNextWindowFocus(); s_bAuthAloneTop = false; }   // 5.02.81: over the settings
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    if(ImGui::Begin("##MPLogin", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | (s_bAuthAlone ? 0 : ImGuiWindowFlags_NoBringToFrontOnFocus)))
    {
        P.dl = ImGui::GetWindowDrawList();
        ImDrawList* dl = P.dl; const AuthLook& L = P.L;
        const ImVec2 p1(P.p0.x + P.w, P.p0.y + h);
        dl->AddRectFilled(P.p0, p1, L.panel, UI::S(8));
        if(rp) { dl->AddRect(P.p0, p1, L.accent, UI::S(8), 0, UI::S(3)); dl->AddRect(ImVec2(P.p0.x - UI::S(4), P.p0.y - UI::S(4)), ImVec2(p1.x + UI::S(4), p1.y + UI::S(4)), IM_COL32(56, 189, 248, 90), UI::S(11), 0, UI::S(4)); }   // 3.25: sky blue glow
        const float capH = UI::S(64);
        dl->AddRectFilled(P.p0, ImVec2(p1.x, P.p0.y + capH), L.header, UI::S(8), ImDrawFlags_RoundCornersTop);
        if(bcw)
        {   // sweeping shine over the header
            float sx = P.p0.x + fmodf(t * 0.5f, 1.0f) * (P.w + UI::S(200)) - UI::S(100);
            dl->PushClipRect(P.p0, ImVec2(p1.x, P.p0.y + capH), true);
            dl->AddQuadFilled(ImVec2(sx, P.p0.y), ImVec2(sx + UI::S(60), P.p0.y), ImVec2(sx + UI::S(20), P.p0.y + capH), ImVec2(sx - UI::S(40), P.p0.y + capH), IM_COL32(255, 255, 255, 60));
            dl->PopClipRect();
        }
        const char* title = st == AuthClient::SIGNUP ? "Create your account" : st == AuthClient::LOGIN ? "Login" :
                            st == AuthClient::CODE ? (s_Auth.recover == 1 ? "Forgot username" : s_Auth.recover == 2 ? "Forgot password" : "Code authentication") :
                            st == AuthClient::RESET ? "New password" : st == AuthClient::DONE ? "Welcome" : "Login";
        char cap[96]; snprintf(cap, sizeof(cap), "%s%s", dz ? "PROJECT DAY Z  |  " : rp ? "MEGA VIL FUTURISTIC  |  " : mw3 ? "MODERN WARFARE 3  |  " : (bcw ? "BRIEF CASE WAR  |  " : "MP  |  "), title);
        dl->AddText(P.f, UI::S(32), ImVec2(P.p0.x + P.pad, P.p0.y + (capH - UI::S(32)) * 0.5f), IM_COL32_WHITE, cap);
        P.y = P.p0.y + capH + UI::S(18);
        if(bcw)
        {
            ImVec2 uv(1, 1); const float is = UI::S(84);
            float bob = sinf(t * 2.2f) * UI::S(4);
            ImVec2 c(P.p0.x + P.w * 0.5f, P.y + is * 0.5f + bob);
            dl->AddCircleFilled(c, is * 0.62f, dz ? IM_COL32(250, 204, 21, (int)(70 + 50 * sinf(t * 3.0f))) : rp ? IM_COL32(56, 189, 248, (int)(70 + 50 * sinf(t * 3.0f))) : mw3 ? IM_COL32(60, 255, 106, (int)(70 + 50 * sinf(t * 3.0f))) : IM_COL32(230, 30, 40, (int)(70 + 50 * sinf(t * 3.0f))), 40);
            if(dz) DzDrawSymbol(dl, c, is * 0.5f, t);   // 3.58: the human print + zombie hand symbol
            else if(void* tex = HUD::Texture(rp ? "rp_phone.png" : mw3 ? "mw3_guns.png" : "bcw_briefcase.png", &uv))
                dl->AddImage((ImTextureID)tex, ImVec2(c.x - is * 0.5f, c.y - is * 0.5f), ImVec2(c.x + is * 0.5f, c.y + is * 0.5f), ImVec2(0, 0), uv);
            else
            {   // no texture: a drawn briefcase
                dl->AddRectFilled(ImVec2(c.x - is * 0.45f, c.y - is * 0.25f), ImVec2(c.x + is * 0.45f, c.y + is * 0.35f), IM_COL32(20, 20, 20, 255), UI::S(6));
                dl->AddRect(ImVec2(c.x - is * 0.15f, c.y - is * 0.40f), ImVec2(c.x + is * 0.15f, c.y - is * 0.25f), IM_COL32(230, 30, 40, 255), UI::S(3), 0, UI::S(4));
                dl->AddRect(ImVec2(c.x - is * 0.45f, c.y - is * 0.25f), ImVec2(c.x + is * 0.45f, c.y + is * 0.35f), IM_COL32(230, 30, 40, 255), UI::S(6), 0, UI::S(3));
            }
            P.y += is + UI::S(10);
        }

        const float bh = UI::S(70), gap = UI::S(20);
        const float fullW = P.w - P.pad * 2, halfW = (fullW - gap) * 0.5f;
        switch(st)
        {
        case AuthClient::LINKING: case AuthClient::BUSY: case AuthClient::TICKET: case AuthClient::WAIT_SERVER: case AuthClient::IDLE:
        {
            static const char* dots[4] = { "", ".", "..", "..." };
            char b[96]; snprintf(b, sizeof(b), "%s%s", st == AuthClient::LINKING ? "Connecting to the login server" : "Logging in", dots[(MP_NowMs() / 400) % 4]);
            P.y += UI::S(30); P.Text(b, L.text, UI::S(34), true);
            if(!s_Auth.err.empty()) P.Text(s_Auth.err.c_str(), L.err, 0, true);
            break;
        }
        case AuthClient::CODE:
        {
            if(s_Auth.recover) P.Text(s_Auth.recover == 1 ? "Forgot your username? Prove the Discord of your account:" : "Forgot your password? Prove the Discord of your account:", L.dim);
            else P.Text(s_bAuthAlone ? "New MP account. Verify with Discord first:" : "This server uses accounts. Verify with Discord first:", L.dim);
            P.Text("1.  Join the Discord server:", L.text);
            P.Text(MP_AUTH_DISCORD_URL, L.accent, UI::S(32), true);
            P.Text("2.  In the #verification channel type:", L.text);
            char cmd[48]; snprintf(cmd, sizeof(cmd), "/gen %s", s_Auth.code[0] ? s_Auth.code : "........");
            {   // the code, big, in a box
                const float cs = UI::S(56); ImVec2 ts = P.f->CalcTextSizeA(cs, FLT_MAX, 0, cmd);
                ImVec2 a(P.p0.x + (P.w - ts.x) * 0.5f - UI::S(24), P.y), b(P.p0.x + (P.w + ts.x) * 0.5f + UI::S(24), P.y + ts.y + UI::S(20));
                dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, bcw ? 18 : 26), UI::S(6));
                dl->AddRect(a, b, L.accent, UI::S(6), 0, UI::S(2.5f));
                dl->AddText(P.f, cs, ImVec2(a.x + UI::S(24), a.y + UI::S(10)), IM_COL32_WHITE, cmd);
                P.y = b.y + UI::S(10);
            }
            int left = s_Auth.codeAt ? 600 - (int)((MP_NowMs() - s_Auth.codeAt) / 1000) : 600; if(left < 0) left = 0;
            char w[96]; snprintf(w, sizeof(w), "Waiting for /gen...  (code works %d:%02d more)", left / 60, left % 60);
            P.Text(w, L.dim, UI::S(26), true);
            if(!s_Auth.err.empty()) P.Text(s_Auth.err.c_str(), L.err, UI::S(26), true);
            if(s_Auth.recover) P.Text("Or on Discord: /forgotuser shows it, /resetpass sets a new password.", L.dim, UI::S(24), true);
            P.y = P.p0.y + h - P.pad - bh * 2 - gap;
            if(P.Button("Open Discord", P.p0.x + P.pad, halfW, bh, true)) Voice::OpenUrl(MP_AUTH_DISCORD_URL);
            if(P.Button("New code", P.p0.x + P.pad + halfW + gap, halfW, bh, false)) s_Auth.NewCode();
            P.y += bh + gap;
            if(P.Button(s_Auth.recover ? "Back to Login" : "I have an account: Login", P.p0.x + P.pad, halfW, bh, false)) s_Auth.WantLogin();
            if(P.Button(s_bAuthAlone ? "Close" : "Leave server", P.p0.x + P.pad + halfW + gap, halfW, bh, false)) { ImGui::End(); ImGui::PopStyleColor(); ImGui::PopStyleVar(2); if(s_bAuthAlone) AuthAloneClose(); else LeaveServer("Left the server."); return; }
            break;
        }
        case AuthClient::SIGNUP:
        {
            char v[128]; snprintf(v, sizeof(v), "Discord verified: %s", s_Auth.discordName.empty() ? "ok" : s_Auth.discordName.c_str());
            P.Text(v, L.ok, UI::S(26));
            P.Field(s_AfUser); P.Field(s_AfPass);
            {   // gender: 3 buttons
                const float rowH = UI::S(62), lw = UI::S(170);
                P.dl->AddText(P.f, P.fs, ImVec2(P.p0.x + P.pad, P.y + (rowH - P.fs) * 0.5f), L.dim, "Gender");
                float gx = P.p0.x + P.pad + lw, gw = (P.w - P.pad * 2 - lw - UI::S(12) * 2) / 3;
                for(int i = 0; i < 3; ++i)
                {
                    ImVec2 a(gx + i * (gw + UI::S(12)), P.y), b(a.x + gw, P.y + rowH);
                    ImGui::SetCursorScreenPos(a); ImGui::PushID(500 + i);
                    if(ImGui::InvisibleButton("##g", ImVec2(gw, rowH))) s_nAfGender = i;
                    ImGui::PopID();
                    const bool on = s_nAfGender == i;
                    dl->AddRectFilled(a, b, on ? L.accent : L.field, UI::S(5));
                    dl->AddRect(a, b, L.fieldBorder, UI::S(5));
                    ImVec2 ts = P.f->CalcTextSizeA(P.fs, FLT_MAX, 0, kAuthGenders[i]);
                    dl->AddText(P.f, P.fs, ImVec2(a.x + (gw - ts.x) * 0.5f, a.y + (rowH - ts.y) * 0.5f), on ? IM_COL32(0, 0, 0, 255) : L.text, kAuthGenders[i]);
                }
                P.y += rowH + UI::S(12);
            }
            P.Field(s_AfAge);
            P.Text("User 3-20 letters / numbers / _   Password 6-32   Age 10-24", L.dim, UI::S(24));
            if(!s_Auth.err.empty()) P.Text(s_Auth.err.c_str(), L.err, UI::S(26));
            P.y = P.p0.y + h - P.pad - bh;
            if(P.Button("Create", P.p0.x + P.pad, halfW, bh, true))
            {
                AuthTakeField(s_AfUser); AuthTakeField(s_AfPass); AuthTakeField(s_AfAge);
                const int age = atoi(s_AfAge.buf);
                if(age < AUTH_AGE_MIN || age > AUTH_AGE_MAX) s_Auth.err = "Age must be 10-24.";
                else s_Auth.Signup(s_AfUser.buf, s_AfPass.buf, kAuthGenders[s_nAfGender], age);
            }
            if(P.Button(s_bAuthAlone ? "Close" : "Leave server", P.p0.x + P.pad + halfW + gap, halfW, bh, false)) { ImGui::End(); ImGui::PopStyleColor(); ImGui::PopStyleVar(2); if(s_bAuthAlone) AuthAloneClose(); else LeaveServer("Left the server."); return; }
            break;
        }
        case AuthClient::LOGIN:
        {
            P.Text(s_bAuthAlone ? "Log in to your MP account (it replaces the one on this phone)." : "Welcome back. Log in to play.", L.dim);
            if(!s_Auth.info.empty()) P.Text(s_Auth.info.c_str(), L.ok, UI::S(26));
            if(!s_Auth.prefillUser.empty() && !s_AfUser.buf[0]) MP_CopyStr(s_AfUser.buf, s_Auth.prefillUser.c_str(), sizeof(s_AfUser.buf));
            P.Field(s_AfUser); P.Field(s_AfPass);
            if(!s_Auth.err.empty()) P.Text(s_Auth.err.c_str(), L.err, UI::S(26));
            P.y = P.p0.y + h - P.pad - bh * 3 - gap * 2;
            // 3.56: forgot username / password (a /gen in #verification proves it is your Discord)
            if(P.Button("Forgot username?", P.p0.x + P.pad, halfW, bh, false)) { s_AfUser.buf[0] = 0; s_Auth.prefillUser.clear(); s_Auth.WantRecover(1); }
            if(P.Button("Forgot password?", P.p0.x + P.pad + halfW + gap, halfW, bh, false)) { s_AfPass.buf[0] = 0; s_Auth.WantRecover(2); }
            P.y += bh + gap;
            if(P.Button("Login", P.p0.x + P.pad, fullW, bh, true))
            {
                AuthTakeField(s_AfUser); AuthTakeField(s_AfPass);
                s_Auth.Login(s_AfUser.buf, s_AfPass.buf);
            }
            P.y += bh + gap;
            if(P.Button("New account (Discord code)", P.p0.x + P.pad, halfW, bh, false)) s_Auth.WantNewAccount();
            if(P.Button(s_bAuthAlone ? "Close" : "Leave server", P.p0.x + P.pad + halfW + gap, halfW, bh, false)) { ImGui::End(); ImGui::PopStyleColor(); ImGui::PopStyleVar(2); if(s_bAuthAlone) AuthAloneClose(); else LeaveServer("Left the server."); return; }
            break;
        }
        case AuthClient::RESET:   // 3.56: forgot password -> Discord verified -> new password
        {
            if(!s_Auth.info.empty()) P.Text(s_Auth.info.c_str(), L.ok, UI::S(26));
            P.Field(s_AfNewPass); P.Field(s_AfNewPass2);
            P.Text("Password 6-32. Your other phones get logged out.", L.dim, UI::S(24));
            if(!s_Auth.err.empty()) P.Text(s_Auth.err.c_str(), L.err, UI::S(26));
            P.y = P.p0.y + h - P.pad - bh;
            if(P.Button("Save & Login", P.p0.x + P.pad, halfW, bh, true))
            {
                AuthTakeField(s_AfNewPass); AuthTakeField(s_AfNewPass2);
                const size_t n = strlen(s_AfNewPass.buf);
                if(n < 6 || n > 32) s_Auth.err = "Password must be 6-32 characters.";
                else if(strcmp(s_AfNewPass.buf, s_AfNewPass2.buf)) s_Auth.err = "The passwords do not match.";
                else
                {
                    if(!s_Auth.prefillUser.empty()) MP_CopyStr(s_AfUser.buf, s_Auth.prefillUser.c_str(), sizeof(s_AfUser.buf));
                    MP_CopyStr(s_AfPass.buf, s_AfNewPass.buf, sizeof(s_AfPass.buf));
                    s_Auth.ResetPassword(s_AfNewPass.buf);
                    s_AfNewPass.buf[0] = s_AfNewPass2.buf[0] = 0;
                }
            }
            if(P.Button("Back to Login", P.p0.x + P.pad + halfW + gap, halfW, bh, false)) { s_AfNewPass.buf[0] = s_AfNewPass2.buf[0] = 0; s_Auth.WantLogin(); }
            break;
        }
        case AuthClient::DONE:
        {
            P.y += UI::S(40);
            P.Text("Your account was created successfully", L.ok, UI::S(38), true);
            char u[64]; snprintf(u, sizeof(u), "Welcome, %s", s_AuthUser.c_str());
            P.Text(u, L.text, UI::S(32), true);
            break;
        }
        case AuthClient::FAILED:
        {
            P.y += UI::S(20);
            P.Text(s_Auth.err.empty() ? "Login failed." : s_Auth.err.c_str(), L.err, UI::S(30), true);
            P.y = P.p0.y + h - P.pad - bh;
            if(P.Button("Try again", P.p0.x + P.pad, halfW, bh, true))
            { const std::string last = AuthLastUser(); if(s_bAuthAlone) s_Auth.Begin(s_szAloneChal, "MP account", !s_Auth.wantNewAccount, last.c_str()); else s_Auth.Begin(s_szAuthChal, client.GetServerName(), !last.empty(), last.c_str()); }
            if(P.Button(s_bAuthAlone ? "Close" : "Leave server", P.p0.x + P.pad + halfW + gap, halfW, bh, false)) { ImGui::End(); ImGui::PopStyleColor(); ImGui::PopStyleVar(2); if(s_bAuthAlone) AuthAloneClose(); else LeaveServer("Left the server."); return; }
            break;
        }
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

// ---- /stats  /char ----------------------------------------------------------------------------
static bool AuthCommand(const char* cmd)
{
    if(strcmp(cmd, "stats") && strcmp(cmd, "char")) return false;
    if(!s_Auth.LoggedIn())
    {
        SysMsg(COL_YELLOW, s_bAuthReq ? "Log in first." : "No account here: this server has no login system (the default server and Brief Case War have it).");
        return true;
    }
    s_Auth.RequestStats();
    s_bStatsOpen = true; s_nStatsAt = MP_NowMs();
    return true;
}

static void RenderStats()
{
    if(!s_bStatsOpen) return;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const bool dz  = MP::IsOnDz();    // 3.58
    const bool rp  = MP::IsOnRp();    // 3.25
    const bool mw3 = MP::IsOnMw3();   // 3.23
    const bool bcw = MP::IsOnBcw() || mw3 || rp || dz;
    AuthPanel P; P.f = UI::Font(); P.L = AuthStyleLook(bcw, mw3, rp, dz); P.pad = UI::S(26); P.fs = UI::S(28); P.ids = 900;
    P.w = fminf(UI::S(700), ds.x * 0.9f);
    const float h = UI::S(600);
    P.p0 = ImVec2((ds.x - P.w) * 0.5f, (ds.y - h) * 0.5f);
    ImGui::SetNextWindowPos(P.p0); ImGui::SetNextWindowSize(ImVec2(P.w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    bool close = false;
    if(ImGui::Begin("##MPStats", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar))
    {
        P.dl = ImGui::GetWindowDrawList();
        const ImVec2 p1(P.p0.x + P.w, P.p0.y + h);
        P.dl->AddRectFilled(P.p0, p1, P.L.panel, UI::S(8));
        P.dl->AddRectFilled(P.p0, ImVec2(p1.x, P.p0.y + UI::S(60)), P.L.header, UI::S(8), ImDrawFlags_RoundCornersTop);
        P.dl->AddText(P.f, UI::S(30), ImVec2(P.p0.x + P.pad, P.p0.y + UI::S(15)), IM_COL32_WHITE, "My account  (only you see this)");
        P.y = P.p0.y + UI::S(80);
        auto& S = s_Auth.stats;
        auto row = [&](const char* k, const std::string& v)
        {
            P.dl->AddText(P.f, P.fs, ImVec2(P.p0.x + P.pad, P.y), P.L.dim, k);
            P.dl->AddText(P.f, P.fs, ImVec2(P.p0.x + P.pad + UI::S(230), P.y), P.L.text, v.c_str());
            P.y += P.fs + UI::S(14);
        };
        auto when = [](const std::string& unix) -> std::string
        {
            time_t tt = (time_t)atoll(unix.c_str()); if(tt <= 0) return "-";
            struct tm tmv; localtime_r(&tt, &tmv); char b[40]; strftime(b, sizeof(b), "%Y-%m-%d %H:%M", &tmv); return b;
        };
        if(s_Auth.statsPending && S.empty()) P.Text("Loading...", P.L.text, 0, true);
        else if(S["ok"] != "1") P.Text(S.count("err") ? S["err"].c_str() : "Could not load your account.", P.L.err, 0, true);
        else
        {
            row("User", S["user"]);
            row("Discord", S["discord"]);
            row("Gender", S["gender"]);
            row("Age", S["age"]);
            row("Created", when(S["created"]));
            row("Logins", S["logins"]);
            row("Last login", when(S["last"]));
            char sc[32]; snprintf(sc, sizeof(sc), "%d", s_nMyScore); row("Score here", sc);
            row("Server", client.GetServerName());
        }
        P.y = P.p0.y + h - P.pad - UI::S(66);
        if(P.Button("Close", P.p0.x + P.pad, P.w - P.pad * 2, UI::S(66), true)) close = true;
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    if(close || client.GetState() == NET_DISCONNECTED) s_bStatsOpen = false;
}
