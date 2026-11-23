// 5.02.68: PhoneLab test hook (https://github.com/JebetrexxMC/mp-phonelab). Off unless a test writes
// files/TYFMC/MP/phonelab_cmd.txt (adb shell / Termux). MP reads it once a second, deletes it and runs each line,
// so a script or an AI tester can repeat a bug the same way every time and read the answer in mp_log.txt
// ("PhoneLab: ..."). Commands: state | mark <text> | neon_browse | neon_join [name part] | neon_quick |
// connect <host:port> | spawn
static void PhoneLabState(const char* why)
{
    logger->Info("PhoneLab: state (%s) game %d, title %d, ped %d, net %d, join phase %d, register %d, keyboard %d, Solo %d, stream %d vol %.0f%%",
                 why, (int)Game::IsReady(), (int)NeonTitleCtx(), Game::LocalPed(), (int)client.GetState(), s_nJoinPhase,
                 (int)RegScreenUp(), (int)VKeyboard::IsOpen(), (int)SoloIsOn(), (int)Sound::StreamActive(), Sound::StreamBaseVolume() * 100.0f);
}
static void PhoneLabRun(char* line)
{
    while(*line == ' ' || *line == '\t') ++line;
    size_t n = strlen(line); while(n && (line[n-1] == '\r' || line[n-1] == '\n' || line[n-1] == ' ')) line[--n] = 0;
    if(!*line || *line == '#') return;
    char* arg = strchr(line, ' '); if(arg) { *arg++ = 0; while(*arg == ' ') ++arg; } else arg = line + n;
    if(!strcmp(line, "state")) { PhoneLabState("asked"); return; }
    if(!strcmp(line, "mark")) { logger->Info("PhoneLab: mark %s", arg); return; }
    if(!strcmp(line, "neon_browse")) { if(NeonTitleCtx()) NeonBrowseFromTitle(); else s_nNeonPage = NP_BROWSER; logger->Info("PhoneLab: neon_browse ok"); return; }
    if(!strcmp(line, "neon_quick")) { logger->Info("PhoneLab: neon_quick"); OnNeonQuick("", nullptr); return; }
    if(!strcmp(line, "connect")) { const bool ok = *arg && NeonJoinAddress(arg); logger->Info("PhoneLab: connect %s (%s)", arg, ok ? "ok" : "failed"); return; }
    if(!strcmp(line, "spawn")) { logger->Info("PhoneLab: spawn (join phase %d)", s_nJoinPhase); MP::SpawnConfirm(); return; }
    if(!strcmp(line, "neon_join"))
    {   // the same as Browse servers -> a row -> Join: the default server, or the first row whose name has <arg>
        std::vector<LRow> rows; const int keep = s_nNeonList; char q[64]; MP_CopyStr(q, s_szNeonSearch, sizeof(q));
        s_nNeonList = 0; s_szNeonSearch[0] = 0; NeonBuild(rows); s_nNeonList = keep; MP_CopyStr(s_szNeonSearch, q, sizeof(s_szNeonSearch));
        int pick = -1;
        for(int i = 0; i < (int)rows.size() && pick < 0; ++i) if(*arg ? strcasestr(rows[i].name, arg) != nullptr : rows[i].kind == 0) pick = i;
        if(pick < 0 && !*arg && !rows.empty()) pick = 0;
        if(pick < 0) { logger->Info("PhoneLab: neon_join %s - no such row (%d rows)", arg, (int)rows.size()); return; }
        logger->Info("PhoneLab: neon_join -> %s (%s:%d)", rows[pick].name, rows[pick].ip, rows[pick].port);
        NeonJoinRow(rows[pick]); return;
    }
    logger->Info("PhoneLab: unknown command '%s'", line);
}
static void PhoneLabTick(uint64_t now)
{
    static uint64_t s_nAt = 0; static bool s_bSaid = false;
    if(now - s_nAt < 1000 || !s_szDataDir[0]) return;
    s_nAt = now;
    char path[512]; snprintf(path, sizeof(path), "%s/phonelab_cmd.txt", s_szDataDir);
    FILE* f = fopen(path, "r"); if(!f) return;
    char buf[4096]; const size_t got = fread(buf, 1, sizeof(buf) - 1, f); fclose(f); buf[got] = 0;
    unlink(path);
    if(!s_bSaid) { s_bSaid = true; logger->Info("PhoneLab: test commands on (phonelab_cmd.txt)"); }
    for(char* save = nullptr, *l = strtok_r(buf, "\n", &save); l; l = strtok_r(nullptr, "\n", &save)) PhoneLabRun(l);
}
