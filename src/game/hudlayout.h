#pragma once
// MP 4.13.22: GTA HUD settings (Settings > Display > GTA HUD) - move / size every part of the game's own HUD.
//
// Built from what libAdvancedSettings.SA.so (ARM's Advanced Settings, research/) does - Ghidra decompile in
// research/libAdvancedSettings.SA_decompiled.c: the same HUD_* parts (PlayerInfo, BarHealth, BarArmor,
// BarBreath, WeaponIcon, Ammo, Money, MoneyChange, Clock, WantedStar, Radar, VehName, AreaName, Radio,
// Subtitles) and the same game functions (CWidgetPlayerInfo::Draw / Render*Bar / DrawWeaponIcon / DrawWanted,
// the radar, CFont::PrintString, the CSprite2d vertex setters). MP's own way: while a part draws, every sprite /
// rect / text it makes is moved + scaled around the part's first point (x, y, zoom, size, width, height, alpha).
// Nothing is hooked until the player turns "Custom GTA HUD" on; all 100 % / 0 = the game's own HUD.
namespace HudLayout
{
    enum Part { PLAYERINFO = 0, BAR_HEALTH, BAR_ARMOR, BAR_BREATH, WEAPON_ICON, AMMO, MONEY, MONEY_CHANGE, CLOCK,
                WANTED, RADAR, VEH_NAME, AREA_NAME, RADIO, SUBTITLES, PART_COUNT };
    struct Cfg { bool show; float x, y, z, size, w, h, alpha; };   // x / y: % of the screen, z (zoom) / size / w / h / alpha: %
    const char* PartName(int p);       // "Health bar"
    const char* PartKey(int p);        // "HUD_BarHealth" (the Advanced Settings section names)
    Cfg&  Get(int p);
    void  Reset(int p);
    bool  Enabled();
    void  SetEnabled(bool on);         // installs the hooks the first time
    int   HookCount();                 // game functions found + hooked (0 = off / not available)
    void  Init(void* gtasa);           // libGTASA handle (no hooks yet)
    void  SetScreen(float w, float h); // every frame (ImGui display size)
    void  Load();                      // from TYFMC.MP.ini [GTAHUD]
    void  Save();
    bool  Conflict();                  // 4.13.34: Advanced Settings is installed (it owns the HUD)
    // 5.00.04: presets (Settings > Display > GTA HUD). 0 = the game's own HUD (all parts reset),
    // 1 = Compact: clock right above the money, health / armor / breath bars right under it (the PC-like
    // stacked look) instead of the mobile layout's big gaps. Turns Custom GTA HUD on (preset 1) and saves.
    enum Preset { PRESET_GAME = 0, PRESET_COMPACT, PRESET_COUNT };
    const char* PresetName(int preset);
    void  ApplyPreset(int preset);
    int   CurrentPreset();             // 5.00.06: Compact is the default (measured in weapon-icon heights)

    // 5.00.08: with ARM's Advanced Settings installed MP's own HUD hooks stay off (double hooks = spaghetti), so
    // "Custom GTA HUD" + Compact used to change nothing. Now MP writes the Compact layout (the "correct" HUD photo,
    // Screenshots/IMG_20261117_172345.jpg) into Advanced Settings' own ini (AML configs/AdvancedSettings.SA.ini,
    // keys from research/libAdvancedSettings.SA.so 7.3.1). Advanced Settings sees the new file time and reloads
    // it by itself. The player's own ini is kept once as <ini>.mp_orig; AdvsRestore() puts it back.
    const char* AdvsIniText();         // the whole Compact AdvancedSettings.SA.ini (docs/hud/AdvancedSettings.SA.ini)
    bool  AdvsApplyCompact();          // merge the Compact HUD_* keys into the ini (other keys / sections are kept)
    bool  AdvsRestore();               // the player's own ini back (from .mp_orig)
    bool  AdvsApplied();               // a .mp_orig backup exists = MP's Compact is in the ini
    const char* AdvsIniPath();         // the ini MP writes ("" = no AML config folder)
    void  AdvsAutoApply();             // once: Custom GTA HUD on + Compact + Advanced Settings -> AdvsApplyCompact()
}
