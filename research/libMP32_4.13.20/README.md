# libMP32.so 4.13.20: the main menu [Multiplayer] button and the custom loading screen

`research/libMP32.so` is MP 4.13.20 for armeabi-v7a. It prints "MP Launcher v4.13.20". It was decompiled with Ghidra 12.2. The two `.c` files next to this README hold the decompiled functions. Most names come from the library's own exported symbols (`MP::InitMainMenu`, `MP::DrawMenuMpFront`, `HookOf_LS_RenderSplash`, ...). `FUN_00649878` is the tap handler, `MenuMpTap` in the source.

## How it works in 4.13.20

1. **The setting.** `MP::InitMainMenu` reads `MainMenuMultiplayer` (default 1) and nothing else; `ServerBrowserMode` did not exist yet (4.13.22). It also looks up PCMenu's `PCMenuLoading` interface (through AML's `GetInterface`) and logs "PCMenu loading screen found".
2. **The button.** `MP::DrawMenuMpFront` draws an ImGui window `##MPMenuTile` with an invisible button `##mpTile`. This happens only while the game's title menu is up: `Game::IsMainMenuShown()`, i.e. `MobileMenu::IsMainMenuScreen`, and no ped is loaded yet (`Game::LocalPed() < 1`), so it never shows on the pause menu.
   - Position / size: x = 13 % and y = 70 % of the screen, w = 11 % of the width, h = 17 % of the height (bottom left).
   - Shape: a slanted 4-point polygon (`AddConvexPolyFilled`), fill `0xDC0C0C0C` (`0xE63C3C3C` while pressed), outline `0xE6C8C8C8`.
   - Icon and label: a person made of a head and an arc, colour `0xFFDCDCDC`, with "Multiplayer" centred under it in the normal UI font.
3. **The tap** (`FUN_00649878` = `MenuMpTap`). It sets "Multiplayer mode", resets the percent and loads the newest save, or starts a new game if there is none (`Game::NewestSaveSlot` -> `RequestLoadSlot` / `RequestNewGame`). Then it logs "Main menu: Multiplayer -> load slot N (ok)".
4. **The custom loading screen.** `OnModLoad` always hooks `CLoadingScreen::RenderSplash` and `CLoadingScreen::RenderLoadingBar` with inline hooks ("Loading screen: %d hooks, percent %s"; the percent is `CLoadingScreen::m_PercentLoaded`). It also hooks `MobileMenu::Update`.
   - `HookOf_LS_RenderSplash`: while `MP::MenuMpLoading()` is true and PCMenu's loading screen is not on (`MP::PCMenuLoadingActive()`), it draws `HUD::RenderLoading(percent)`, which is `MP::DrawMenuLoading`: ui_bg.jpg, title.png, "GAMEPLAY TIP", "LOADING WORLD nn%". The game's own splash is skipped for that frame.
   - `HookOf_LS_RenderLoadingBar` skips the game's bar while MP's panel is drawn.
5. **After the load.** `HookOf_MobileMenu_Update` -> `MP::MenuMpMenuTick` closes the game menu the load returns to. `MP::Process` puts up the black screen with CJ frozen and invisible at Grove Street. The percent runs 90 -> 100 %, then the server select opens.

## What changed later, and the 4.13.50 fix

- **4.13.25** stopped hooking the loading screen whenever PCMenu is installed. Both mods patched the same code, and the game closed with no crash log. From then on, with PCMenu installed, a Multiplayer tap showed the game's own splash, or PCMenu's when its loading screen was on, instead of MP's custom loading screen.
- **4.13.49** gave the button the new rounded "MultiPlayer" look and made it follow `MainMenuMultiplayer` together with `ServerBrowserMode = default`.
- **4.13.50** hooks the loading screen again when PCMenu is installed. This time it goes through libGTASA's own `.got.plt` slots, found at run time from the library's PLT relocations (`Game::FindPltSlot`), and chains with PCMenu instead of patching the same code a second time. PCMenu's loading screen still wins while it is on. The log line reads "Loading screen: PCMenu is installed - 2 slot hooks ...".
