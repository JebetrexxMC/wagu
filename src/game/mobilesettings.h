#pragma once
// MP 5.01.11: the game's own settings (MobileSettings::settings, the Options pages) read and written directly, the
// way PCMenu's PC settings pages do it - for the neon Settings page on the title screen. SAUtils' mod settings
// (gMoreSettings, its "Mods" page and the tabs mods add) come along.
#include <string>
#include <vector>

namespace GameSettings
{
    enum Kind { K_TOGGLE = 0, K_SLIDER = 1, K_BUTTON = 2, K_HEADER = 3, K_INFO = 4 };   // 5.01.13: + mod buttons, section titles, mods folder rows
    struct View { int value, vmin, vmax, type; };
    struct Item
    {
        int  id;              // setting id (0..36 the game's, 37+ SAUtils)
        int  tab;             // 0 Controls, 1 Game, 2 Display, 3 Audio, 4 Language, 5+ Mods
        int  kind;            // K_TOGGLE / K_SLIDER
        char label[48];
        int  ref = -1;        // 5.01.13: K_BUTTON = its index in SAUtils' gMoreSettings
        char value[24] = "";  // 5.01.13: K_INFO / K_HEADER: the text on the right
    };
    bool Init(void* gameHandle);                // symbols (OnModLoad)
    bool Ready();                               // the settings table is readable
    bool Read(int id, View& v);
    bool Write(int id, int value);              // + the game's side effect (touch layout, audio, radio, HRTF) + SAUtils' change callback
    void Step(int id, int dir);                 // toggle: next / previous value (wraps), slider: one 20th
    void SetFraction(int id, float f);          // slider: 0..1 of its range
    std::string ValueText(int id);              // "ON", "HIGH", "75%" ... (ASCII)
    void Items(std::vector<Item>& out);         // every row the neon Settings page shows
    bool Press(int ref);                        // 5.01.13: a mod's SAUtils button (AddButton)
    int  ModCount();                            // 5.01.13: SAUtils items found (log / empty text)
    bool SAUtilsFound();
    void Save();                                // the game's settings file (when MobileSettings::Save... exists)
    const char* SaveInfo();
    std::string GxtText(const char* key);       // 5.02.01: the game's text of a GXT key (ASCII upper case; the key when unknown)
}
