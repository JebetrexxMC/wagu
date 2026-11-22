// MP 5.02.43: the neon title menu backgrounds (research/gta-5-sunset.jpg, 9161613bf9e9a83fe0d2072a70cd8260.jpg,
// 80a3caae43bb7913893e3e4dc1826289.jpg, resized to power-of-two JPEGs) built into libMP32.so / libMP64.so.
// src/ui/neonpaint.cpp decodes them from memory (stb_image): nothing is read from assets/MP, files/TYFMC or Host.
// Include path: src/embed/menubg (Android.mk LOCAL_ASFLAGS).
.macro MP_MENUBG name, file
    .section .rodata.mp_menubg_\name,"a"
    .balign 16
    .global mp_menubg_\name
    .hidden mp_menubg_\name
mp_menubg_\name:
    .incbin "\file"
    .global mp_menubg_\name\()_end
    .hidden mp_menubg_\name\()_end
mp_menubg_\name\()_end:
    .byte 0
.endm
    MP_MENUBG 0, "bg0.jpg"
    MP_MENUBG 1, "bg1.jpg"
    MP_MENUBG 2, "bg2.jpg"
