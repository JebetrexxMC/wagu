// MP 5.01.06: MP's own moonloader entry scripts built into libMP32.so / libMP64.so. src/mp_monet.cpp writes them
// into files/TYFMC/MP/moonloader/ before MonetLoader starts when they are missing or an older MP copy is there, so
// moonloader/lua_scripts/ (HassleHUD, moonloader-scripts, zips) works even when only libMP was updated.
// Include path: assets/MP/moonloader (Android.mk LOCAL_ASFLAGS).
.macro MP_CORE name, file
    .section .rodata.mp_core_\name,"a"
    .balign 16
    .global mp_core_\name
    .hidden mp_core_\name
mp_core_\name:
    .incbin "\file"
    .global mp_core_\name\()_end
    .hidden mp_core_\name\()_end
mp_core_\name\()_end:
    .byte 0
.endm
    MP_CORE lsl, "lua_scripts_loader.lua"
    MP_CORE mods, "mods_loader.lua"
    MP_CORE zip, "lib/mp_zip.lua"
    MP_CORE pcp, "lib/mp_pcpaths.lua"
    MP_CORE mimgui, "lib/mimgui/init.lua"
    MP_CORE mlib, "lib/moonloader.lua"
    .section .note.GNU-stack,"",%progbits
