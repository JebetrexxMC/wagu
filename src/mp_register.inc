// 5.02.31: Broker MQQ register / login (the default server). The MP mod's own account, NOT the Discord bot
// authentication (mp_auth.inc): no bot, no code - a SA-MP style dialog the first time you join:
//   Registering...  -> a password -> your age -> your gender  => files/TYFMC/MP/register.txt
// Every later join / reconnect / restart: Logging in...  [Login] [Options]
//   Options: Forgot password (type your username -> a new password -> the same password again)
//            Create new account (a new register.txt replaces the old one)  /  Quit (leave the server)
// register.txt keeps the name, a random salt and SHA-256(salt + password) (never the password itself), age, gender.
// A hidden backup of the same (hash only) is kept on the MP HiveMQ link (retained, wagu/reg1/<sha of the name>).
#include <mbedtls/sha256.h>

enum { REG_NONE = 0, REG_PASS, REG_AGE, REG_GENDER, REG_DONE, LOG_PASS, LOG_OPT, FGT_NAME, FGT_NEW, FGT_CONF, NEW_CONFIRM };
struct RegAccount { char name[MP_NAME_LEN] = ""; char salt[33] = ""; char hash[65] = ""; int age = 0; char gender[8] = ""; long created = 0; };
struct RegState
{
    bool pending = false, active = false, loggedIn = false, backupDone = false;
    uint64_t joinedAt = 0, nextBackup = 0;
    int  tries = 0;
    char newPass[64] = ""; int newAge = 0; bool resetting = false;
    RegAccount acc;
};
static RegState s_Reg;

static void RegHex(const unsigned char* b, int n, char* out) { static const char* h = "0123456789abcdef"; for(int i = 0; i < n; ++i) { out[i * 2] = h[b[i] >> 4]; out[i * 2 + 1] = h[b[i] & 15]; } out[n * 2] = 0; }
static void RegHash(const char* salt, const char* pass, char out[65])
{
    std::string in = std::string("wagu-reg-v1|") + salt + "|" + pass;
    unsigned char d[32]; mbedtls_sha256((const unsigned char*)in.data(), in.size(), d, 0);
    RegHex(d, 32, out);
}
static void RegNewSalt(char out[33])
{
    unsigned char b[16] = {};
    if(FILE* f = fopen("/dev/urandom", "rb")) { if(fread(b, 1, 16, f) != 16) {} fclose(f); }
    const uint64_t t = MP_NowMs(); for(int i = 0; i < 8; ++i) b[i] ^= (unsigned char)(t >> (i * 8));
    RegHex(b, 16, out);
}
static std::string RegPath() { return std::string(s_szDataDir) + "/register.txt"; }
static bool RegLoad(RegAccount& a)
{
    a = RegAccount();
    FILE* f = fopen(RegPath().c_str(), "r"); if(!f) return false;
    char ln[256];
    while(fgets(ln, sizeof(ln), f))
    {
        ln[strcspn(ln, "\r\n")] = 0;
        char* eq = strchr(ln, '='); if(!eq || ln[0] == '#') continue; *eq = 0; const char* v = eq + 1;
        if(!strcmp(ln, "name")) MP_CopyStr(a.name, v, sizeof(a.name));
        else if(!strcmp(ln, "salt")) MP_CopyStr(a.salt, v, sizeof(a.salt));
        else if(!strcmp(ln, "hash")) MP_CopyStr(a.hash, v, sizeof(a.hash));
        else if(!strcmp(ln, "age")) a.age = atoi(v);
        else if(!strcmp(ln, "gender")) MP_CopyStr(a.gender, v, sizeof(a.gender));
        else if(!strcmp(ln, "created")) a.created = atol(v);
    }
    fclose(f);
    return a.name[0] && a.salt[0] && strlen(a.hash) == 64;
}
static bool RegSave(const RegAccount& a)
{
    FILE* f = fopen(RegPath().c_str(), "w"); if(!f) return false;
    fprintf(f, "# Broker MQQ account (MP Register System). The password is never saved, only its salted SHA-256.\n");
    fprintf(f, "name=%s\nsalt=%s\nhash=%s\nage=%d\ngender=%s\ncreated=%ld\n", a.name, a.salt, a.hash, a.age, a.gender, a.created);
    fclose(f);
    s_Reg.backupDone = false; s_Reg.nextBackup = 0;
    return true;
}
static void RegBackupTick(uint64_t now)
{
    if(s_Reg.backupDone || !s_Reg.acc.name[0] || now < s_Reg.nextBackup) return;
    s_Reg.nextBackup = now + 15000;
    if(!Online::SocLinkUp()) return;
    char lower[MP_NAME_LEN]; MP_CopyStr(lower, s_Reg.acc.name, sizeof(lower)); for(char* c = lower; *c; ++c) *c = (char)tolower((unsigned char)*c);
    unsigned char d[32]; mbedtls_sha256((const unsigned char*)lower, strlen(lower), d, 0); char id[65]; RegHex(d, 32, id);
    char body[320];
    snprintf(body, sizeof(body), "{\"v\":1,\"s\":\"%s\",\"h\":\"%s\",\"a\":%d,\"g\":\"%s\",\"t\":%ld}", s_Reg.acc.salt, s_Reg.acc.hash, s_Reg.acc.age, s_Reg.acc.gender, s_Reg.acc.created);
    Online::SocLinkPub(std::string("wagu/reg1/") + std::string(id, 32), body, true);
    s_Reg.backupDone = true;
}

// ---- the dialogs ---------------------------------------------------------------------------------------------
static void RegShow(int step, const char* err = nullptr);
static void RegOnDialog(int response, int item, const char* input, int step)
{
    switch(step)
    {
        case REG_PASS:
            if(!response) { LeaveServer("You left Broker MQQ (not registered)."); return; }
            if(strlen(input) < 4) { RegShow(REG_PASS, "The password needs at least 4 characters."); return; }
            MP_CopyStr(s_Reg.newPass, input, sizeof(s_Reg.newPass)); RegShow(REG_AGE); return;
        case REG_AGE:
        {
            if(!response) { RegShow(REG_PASS); return; }
            const int age = atoi(input);
            if(age < 7 || age > 99) { RegShow(REG_AGE, "Type your age (7 - 99)."); return; }
            s_Reg.newAge = age; RegShow(REG_GENDER); return;
        }
        case REG_GENDER:
        {
            if(!response) { RegShow(REG_AGE); return; }
            RegAccount a; MP_CopyStr(a.name, settings.name, sizeof(a.name)); RegNewSalt(a.salt); RegHash(a.salt, s_Reg.newPass, a.hash);
            a.age = s_Reg.newAge; MP_CopyStr(a.gender, item == 1 ? "Female" : "Male", sizeof(a.gender)); a.created = (long)time(nullptr);
            memset(s_Reg.newPass, 0, sizeof(s_Reg.newPass));
            if(!RegSave(a)) { RegShow(REG_PASS, "Could not write register.txt in the MP folder."); return; }
            s_Reg.acc = a; s_Reg.loggedIn = true; RegShow(REG_DONE); return;
        }
        case REG_DONE: s_Reg.active = false; return;
        case LOG_PASS:
        {
            if(!response) { RegShow(LOG_OPT); return; }
            char h[65]; RegHash(s_Reg.acc.salt, input, h);
            if(!strcmp(h, s_Reg.acc.hash))
            {
                s_Reg.active = false; s_Reg.loggedIn = true;
                SysMsg(COL_GREEN, "{33CC55}Logged in. {FFFFFF}Welcome back, {FFFF00}%s{FFFFFF}!", s_Reg.acc.name);
                return;
            }
            if(++s_Reg.tries >= 5) { LeaveServer("Wrong password 5 times. Use Options > Forgot password next time."); return; }
            char e[96]; snprintf(e, sizeof(e), "Wrong password (%d of 5).", s_Reg.tries); RegShow(LOG_PASS, e); return;
        }
        case LOG_OPT:
            if(!response) { RegShow(LOG_PASS); return; }
            if(item == 0) RegShow(FGT_NAME);
            else if(item == 1) RegShow(NEW_CONFIRM);
            else LeaveServer("You left Broker MQQ.");
            return;
        case FGT_NAME:
            if(!response) { RegShow(LOG_PASS); return; }
            if(strcasecmp(input, s_Reg.acc.name)) { RegShow(FGT_NAME, "That is not the username of this account."); return; }
            RegShow(FGT_NEW); return;
        case FGT_NEW:
            if(!response) { RegShow(LOG_PASS); return; }
            if(strlen(input) < 4) { RegShow(FGT_NEW, "The password needs at least 4 characters."); return; }
            MP_CopyStr(s_Reg.newPass, input, sizeof(s_Reg.newPass)); RegShow(FGT_CONF); return;
        case FGT_CONF:
            if(!response) { RegShow(FGT_NEW); return; }
            if(strcmp(input, s_Reg.newPass)) { RegShow(FGT_NEW, "The two passwords are not the same. Create it again."); return; }
            RegNewSalt(s_Reg.acc.salt); RegHash(s_Reg.acc.salt, s_Reg.newPass, s_Reg.acc.hash); memset(s_Reg.newPass, 0, sizeof(s_Reg.newPass));
            RegSave(s_Reg.acc); s_Reg.active = false; s_Reg.loggedIn = true;
            SysMsg(COL_GREEN, "{33CC55}Your new password is saved. {FFFFFF}Logged in as {FFFF00}%s{FFFFFF}.", s_Reg.acc.name);
            return;
        case NEW_CONFIRM:
            if(!response) { RegShow(LOG_OPT); return; }
            RegShow(REG_PASS); return;
    }
}
static void RegDialogFn(int response, int item, const char* input, int user) { RegOnDialog(response, item, input ? input : "", user); }
static void RegShow(int step, const char* err)
{
    s_Reg.active = true;
    char info[900]; const char* nm = settings.name;
    const char* e1 = err ? "{FF4444}" : ""; const char* e2 = err ? err : ""; const char* e3 = err ? "\n\n" : "";
    switch(step)
    {
        case REG_PASS:
            snprintf(info, sizeof(info), "%s%s%s{FFFFFF}Welcome to {DC2626}Broker MQQ{FFFFFF}, {FFFF00}%s{FFFFFF}!\nThis name is not registered.\n\n{A9C4E4}Create a password for your account:", e1, e2, e3, nm);
            ShowDialog(9501, DIALOG_STYLE_PASSWORD, "{FFFFFF}Registering...", info, "Register", "Quit", RegDialogFn, REG_PASS); break;
        case REG_AGE:
            snprintf(info, sizeof(info), "%s%s%s{FFFFFF}How old are you?\n\n{A9C4E4}Type your age:", e1, e2, e3);
            ShowDialog(9502, DIALOG_STYLE_INPUT, "{FFFFFF}Registering... (age)", info, "Next", "Back", RegDialogFn, REG_AGE); break;
        case REG_GENDER:
            ShowDialog(9503, DIALOG_STYLE_LIST, "{FFFFFF}Registering... (gender)", "Male\nFemale", "Done", "Back", RegDialogFn, REG_GENDER); break;
        case REG_DONE:
            snprintf(info, sizeof(info), "{33CC55}Your account is registered!\n\n{FFFFFF}Name: {FFFF00}%s\n{FFFFFF}Age: {FFFF00}%d\n{FFFFFF}Gender: {FFFF00}%s\n\n{A9C4E4}Saved in the MP folder (register.txt).\nNext time you join you log in with this password.",
                     s_Reg.acc.name, s_Reg.acc.age, s_Reg.acc.gender);
            ShowDialog(9504, DIALOG_STYLE_MSGBOX, "{FFFFFF}Registered", info, "Play", "", RegDialogFn, REG_DONE); break;
        case LOG_PASS:
            snprintf(info, sizeof(info), "%s%s%s{FFFFFF}Welcome back to {DC2626}Broker MQQ{FFFFFF}, {FFFF00}%s{FFFFFF}!\nThis account is registered.\n\n{A9C4E4}Type your password to log in:", e1, e2, e3, s_Reg.acc.name);
            ShowDialog(9505, DIALOG_STYLE_PASSWORD, "{FFFFFF}Logging in...", info, "Login", "Options", RegDialogFn, LOG_PASS); break;
        case LOG_OPT:
            ShowDialog(9506, DIALOG_STYLE_LIST, "{FFFFFF}Logging in... (options)", "Forgot password\nCreate new account\nQuit", "Select", "Back", RegDialogFn, LOG_OPT); break;
        case FGT_NAME:
            snprintf(info, sizeof(info), "%s%s%s{FFFFFF}Forgot your password?\n\n{A9C4E4}Type the username of this account:", e1, e2, e3);
            ShowDialog(9507, DIALOG_STYLE_INPUT, "{FFFFFF}Forgot password", info, "Next", "Back", RegDialogFn, FGT_NAME); break;
        case FGT_NEW:
            snprintf(info, sizeof(info), "%s%s%s{FFFFFF}Create a new password for {FFFF00}%s{FFFFFF}:", e1, e2, e3, s_Reg.acc.name);
            ShowDialog(9508, DIALOG_STYLE_PASSWORD, "{FFFFFF}Forgot password (new password)", info, "Next", "Back", RegDialogFn, FGT_NEW); break;
        case FGT_CONF:
            ShowDialog(9509, DIALOG_STYLE_PASSWORD, "{FFFFFF}Forgot password (confirm)", "{FFFFFF}Type the same new password again:", "Save", "Back", RegDialogFn, FGT_CONF); break;
        case NEW_CONFIRM:
            snprintf(info, sizeof(info), "{FFFFFF}Create a new account?\n\n{FF4444}The account {FFFF00}%s {FF4444}in register.txt is replaced\n{FF4444}by a new one for {FFFF00}%s{FF4444}.", s_Reg.acc.name, settings.name);
            ShowDialog(9510, DIALOG_STYLE_MSGBOX, "{FFFFFF}Create new account", info, "Create", "Back", RegDialogFn, NEW_CONFIRM); break;
    }
}

// ---- hooks -------------------------------------------------------------------------------------------------------
static void RegOnJoined()
{
    if(!MP::IsOnDefault()) return;
    if(s_Reg.loggedIn) return;   // a reconnect in the same session: still logged in
    s_Reg.pending = true; s_Reg.joinedAt = MP_NowMs(); s_Reg.tries = 0;
}
static void RegReset()
{
    // leaving the server: log out (the next join asks again), keep the account
    if(s_Reg.active && SampDialogOpen() && s_Dlg.fn == RegDialogFn) s_Dlg.open = false;
    s_Reg.pending = s_Reg.active = false; s_Reg.loggedIn = false; s_Reg.tries = 0;
    memset(s_Reg.newPass, 0, sizeof(s_Reg.newPass));
}
static bool RegScreenUp() { return s_Reg.active && SampDialogOpen() && s_Dlg.fn == RegDialogFn; }
// 5.02.35: the < Spawn > picker on the default server waits for the register / login (AuthHoldsJoin)
// 5.02.67: the 20 s cap counted from "Connected successfully!". A join from the neon title menu (Browse servers /
// the server list) runs right after the Multiplayer save load, so the connection view was often not ready inside
// those 20 s: the picker opened first, the dialog then waited for the world and popped up at < Spawn > (with the
// keyboard, on the same frame as the spawn). The wait now counts from the moment the connection view is ready.
static uint64_t s_nRegViewAt = 0;   // the connection view became ready for the dialog (0 = not yet)
static bool RegHoldsJoin(uint64_t now)
{
    if(!MP::IsOnDefault()) return false;
    if(RegScreenUp()) return true;   // also "Registered" / "Logged in" until you close it
    if(s_Reg.loggedIn) return false;
    if(!s_Reg.pending) return false;
    if(now - s_Reg.joinedAt < 45000) return true;                 // never stuck: 45 s at most
    return s_nRegViewAt && now - s_nRegViewAt < 8000;
}
static void RegTick(uint64_t now)
{
    RegBackupTick(now);
    if(!s_Reg.pending || client.GetState() != NET_CONNECTED) { s_nRegViewAt = 0; return; }
    if(!MP::IsOnDefault()) { s_Reg.pending = false; return; }
    // 5.02.35: on the connection screen (the Santa Maria Beach camera, after "Connected successfully!"), like SA-MP:
    // the dialog no longer waits until you picked a spawn place and are standing in the world. The < Spawn >
    // picker waits for it instead (RegHoldsJoin). Without the join screen (quiet rejoin) it still opens in the world.
    // 5.02.67: the picker already open (JP_PICK) counts as the connection view too: it goes back to the camera view
    // and the dialog opens there, before you spawn.
    const bool feedDone = s_nFeedStep < 0 && s_nFeedRes < 0;
    const bool joinView = JoinFlowActive() && feedDone && (s_nJoinPhase == JP_WAIT || s_nJoinPhase == JP_PICK);
    const bool otherDlg = SampDialogOpen() && s_Dlg.fn != RegDialogFn;
    const char* why = nullptr;
    if(JoinFlowActive() && !joinView) why = !feedDone ? "loading feed" : "join screen";
    else if(AuthScreenUp()) why = "MP login screen";
    else if(otherDlg) why = "another dialog is open";
    else if(Game::LocalPed() <= 0) why = "no player yet";
    else if(!joinView && now - s_Reg.joinedAt < 3000) why = "3 s in the world";
    if(why)
    {   // 5.02.67: mp_log.txt says what the dialog waits for (every 2 s, at most 12 lines a join)
        static uint64_t s_nWhyAt = 0; static int s_nWhyN = 0;
        if(now - s_Reg.joinedAt < 1000) s_nWhyN = 0;
        if(s_nWhyN < 12 && now - s_nWhyAt > 2000)
        { s_nWhyAt = now; ++s_nWhyN; logger->Info("Register: waiting (%s; join phase %d, %u ms after Connected successfully)", why, s_nJoinPhase, (unsigned)(now - s_Reg.joinedAt)); }
        return;
    }
    if(!s_nRegViewAt) s_nRegViewAt = now;
    if(joinView && s_nJoinPhase == JP_PICK) { s_nJoinPhase = JP_WAIT; s_nJoinT0 = now; }   // the picker comes back after the login
    s_Reg.pending = false;
    const bool have = RegLoad(s_Reg.acc);
    logger->Info("Register: %s dialog on the %s (%u ms after Connected successfully)", have ? "login" : "register",
                 joinView ? "connection screen" : "game screen", (unsigned)(now - s_Reg.joinedAt));
    if(have) RegShow(LOG_PASS); else RegShow(REG_PASS);
}
static void RegRender()
{
    // the dialog draws itself (mp_samp.inc); dim the world behind it like the SA-MP login
    if(!RegScreenUp()) return;
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImGui::GetIO().DisplaySize, IM_COL32(0, 0, 0, 90));
}
