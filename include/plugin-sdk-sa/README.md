# plugin-sdk-sa headers (5.02.80)

Headers from [JebetrexxMC/plugin-sdk-sa](https://github.com/JebetrexxMC/plugin-sdk-sa) (commit in `UPSTREAM_COMMIT`, MIT license in `LICENSE`):

- `plugin_sa/game_sa/` - GTA San Andreas classes (CPed, CVehicle, CStreaming, CTheScripts, RenderWare `rw/`, ...)
- `plugin_vc/game_vc/` - GTA Vice City classes (ViceLoader / VC side of AML Multiplayer)
- `shared/` - the SDK's common helpers (patching, events, extenders)

They describe the PC game. MP and AML Multiplayer use them as the **reference layout** for the structs and
names (class layouts, field names, enums) next to `include/mod/` (AML) and `include/sautils/`; the Android
addresses still come from AML's `aml->GetSym()` / the patterns in `src/game/`. Not added to the global
include path (Windows-only headers inside), include them by path where needed:

```cpp
#include "../include/plugin-sdk-sa/plugin_sa/game_sa/eWeaponType.h"
```
