script_name("GTAV_Crosshair")
-- Dynamic Ammo HUD & Animated Crosshair v2.0 Final by Fausto200 (https://libertycity.net/files/file246763.html)
-- 2.0-mp (MP 5.02.37): runs on Android MonetLoader. The PC addresses this script patches (CHud::DrawCrossHairs
-- 0x58E020, camera mode 0xB6F1A8, CPed + 0x5A0 slots, CPed + 0x480 sprint) mean other things in libGTASA.so, so on
-- the phone: the default crosshair is hidden by a monethook hook instead of writing 0xC3, aiming comes from the
-- camera, the clip from mp_weaponslots, and the sprint bar is off. Command: /ammohud (menu), /ammohud on|off.

local MP = { android = (MONET_VERSION ~= nil) or (package.config:sub(1, 1) == '/'), ws = nil, off = false,
    read = readMemory, write = writeMemory, hook = nil }
if MP.android then local ok, m = pcall(require, 'mp_weaponslots'); if ok then MP.ws = m end end
local function readMemory(a, ...)
    if MP.android and (a == 0x58E020 or a == 0xB6F1A8) then return 0 end
    return MP.read(a, ...)
end
local function writeMemory(a, ...)
    if MP.android and a == 0x58E020 then return end
    return MP.write(a, ...)
end

local inicfg = require 'inicfg'
local memory = require 'memory'
local okMimgui, imgui = pcall(require, 'mimgui')
local USAR_MIMGUI = (okMimgui and imgui ~= nil)
local archivoConfig = 'mira_calibracion.ini'

-- ==================== MOTOR GRAFICO MIMGUI (ANTI-BUG DE PAUSA) ====================
local DL = nil
local IM_FONTS = {}
local TXT = { font = nil, mult = 1.0, fuentes = {}, pop = 0, prevClip = nil, previa = false }

local natDrawBox     = renderDrawBox
local natDrawLine    = renderDrawLine
local natDrawTex     = renderDrawTexture
local natDrawText    = renderFontDrawText
local natTextLen     = renderGetFontDrawTextLength
local natCreateFont  = renderCreateFont
local natReleaseFont = renderReleaseFont
local natLoadTex     = renderLoadTextureFromFile
local natReleaseTex  = renderReleaseTexture

local floor, ceil, max, min, abs, sin, cos, random, rad = math.floor, math.ceil, math.max, math.min, math.abs, math.sin, math.cos, math.random, math.rad

-- Escala por resolucion: 1.0 a 1080p de alto. Posiciones y tamanos del HUD (que estan pensados en pixeles a 1080p) se
-- multiplican por K, asi a 1440p / 4K no se ve pequeno ni descolocado. Se actualiza cada frame en procesarYRenderizarFrame.
local K = 1.0

local function argbToImU32(c)
    c = floor(c or 0)
    if c < 0 then c = c + 4294967296 end
    local a = floor(c / 16777216) % 256
    local r = floor(c / 65536) % 256
    local g = floor(c / 256) % 256
    local b = c % 256
    return a * 16777216 + b * 65536 + g * 256 + r
end

-- Vectores reutilizados: antes cada renderDrawBox / renderDrawLine creaba ImVec2 nuevos en cada llamada (cientos por frame).
-- Las funciones de dibujo de imgui copian el valor al instante, asi que es seguro cambiar .x / .y y volver a usarlos.
local P1, P2, P3, P4, UV0, UV1, UV2, UV3

if USAR_MIMGUI then
    P1, P2, P3, P4 = imgui.ImVec2(0, 0), imgui.ImVec2(0, 0), imgui.ImVec2(0, 0), imgui.ImVec2(0, 0)
    UV0, UV1, UV2, UV3 = imgui.ImVec2(0, 0), imgui.ImVec2(1, 0), imgui.ImVec2(1, 1), imgui.ImVec2(0, 1)

    renderCreateFont = function(nombre, tam, flags)
        local id = #IM_FONTS + 1
        IM_FONTS[id] = { size = floor(tam * 1.35 + 0.5) }
        return id
    end

    renderReleaseFont = function(f) end

    renderGetFontDrawTextLength = function(f, txt)
        txt = tostring(txt or "")
        if #txt == 0 then return 0 end
        local fd = IM_FONTS[f]
        local sz = (fd and fd.size or 15) * K * TXT.mult
        if TXT.font then
            local okF, tsF = pcall(function() return TXT.font:CalcTextSizeA(sz, 100000, 0, txt) end)
            if okF and tsF and tsF.x then return tsF.x end
        end
        local ok, ts = pcall(imgui.CalcTextSize, txt)
        if ok and ts and ts.x then
            local baseH = (ts.y and ts.y > 0) and ts.y or 13
            return ts.x * (sz / baseH)
        end
        return #txt * (sz * 0.52)
    end

    renderDrawBox = function(x, y, w, h, color)
        if not DL or w <= 0 or h <= 0 then return end
        P1.x, P1.y = x, y
        P2.x, P2.y = x + w, y + h
        DL:AddRectFilled(P1, P2, argbToImU32(color))
    end

    renderDrawLine = function(x1, y1, x2, y2, grosor, color)
        if not DL then return end
        P1.x, P1.y = x1, y1
        P2.x, P2.y = x2, y2
        DL:AddLine(P1, P2, argbToImU32(color), max(1.0, grosor or 1.0))
    end

    -- Soporta rotacion real para los casquillos voladores mediante AddImageQuad
    renderDrawTexture = function(tex, x, y, w, h, rot, color)
        if not DL or not tex then return end
        local u32 = argbToImU32(color)
        if not rot or abs(rot) < 0.1 or not DL.AddImageQuad then
            P1.x, P1.y = x, y
            P2.x, P2.y = x + w, y + h
            DL:AddImage(tex, P1, P2, UV0, UV2, u32)
        else
            local cx, cy = x + w * 0.5, y + h * 0.5
            local hw, hh = w * 0.5, h * 0.5
            local a = rad(rot)
            local ca, sa = cos(a), sin(a)
            P1.x, P1.y = cx + (-hw * ca - (-hh) * sa), cy + (-hw * sa + (-hh) * ca)
            P2.x, P2.y = cx + ( hw * ca - (-hh) * sa), cy + ( hw * sa + (-hh) * ca)
            P3.x, P3.y = cx + ( hw * ca - ( hh) * sa), cy + ( hw * sa + ( hh) * ca)
            P4.x, P4.y = cx + (-hw * ca - ( hh) * sa), cy + (-hw * sa + ( hh) * ca)
            DL:AddImageQuad(tex, P1, P2, P3, P4, UV0, UV1, UV2, UV3, u32)
        end
    end

    renderFontDrawText = function(f, s, x, y, color)
        if not DL then return end
        s = tostring(s or "")
        if #s == 0 then return end
        local fd = IM_FONTS[f]
        local sz = (fd and fd.size or 15) * K * TXT.mult
        P1.x, P1.y = x, y
        local fontPtr = TXT.font or (imgui.GetFont and imgui.GetFont() or nil)
        if fontPtr then
            DL:AddTextFontPtr(fontPtr, sz, P1, argbToImU32(color), s)
        else
            DL:AddText(P1, argbToImU32(color), s)
        end
    end
end

-- Circulo relleno (mimgui: nativo con antialiasing; sin mimgui: poligono de MoonLoader)
local function circuloLleno(x, y, r, color)
    if r <= 0.3 then return end
    if USAR_MIMGUI then
        if not DL then return end
        P1.x, P1.y = x, y
        DL:AddCircleFilled(P1, r, argbToImU32(color), 16)
    elseif renderDrawPolygon then
        pcall(renderDrawPolygon, x, y, r, 16, 0.0, color)
    end
end

-- ==================== CONFIGURACION ====================
local config = inicfg.load({
    ajustes = {
        idioma = MP.android and 'en' or 'es',          -- 'es' o 'en'
        idiomaElegido = MP.android,  -- false: primera instalacion (MP: no keyboard on the phone, English by default)
        offsetX = 0,
        offsetY = 0,
        calibrado = MP.android,
        separacion = 0.0,
        largoLinea = 4.0,
        grosorLinea = 3.0,
        opacidad = 255,
        expansion = true,
        escopetaActiva = true,   -- escopetas con mira circular (estilo juegos modernos)
        escopetaRadio = 11.0,    -- radio del aro en pixeles
        escopetaGrosor = 1.5,    -- grosor del aro
        escopetaPunto = true,    -- punto central dentro del aro
        colorEnemigo = true,
        hitmarker = true,
        hudMunicion = true,
        hudSiempre = false,
        hudEscala = 1.0,
        hudAltura = 210,
        hudX = 50,                -- distancia al borde derecho (px a 1080p)
        sprintX = 50,
        minigunMax = 500,
        combustibleMax = 500,
        chalecoHud = true,
        chalecoSiempre = false,
        chalecoEscala = 1.0,
        chalecoBarra = true,
        chalecoX = 50,
        chalecoAltura = 290,
        sprintHud = true,
        sprintAltura = 150,
        casquilloSonido = true,    -- sonido al rebotar el casquillo (necesita casquillo.mp3 en resource/ammo, resource/hud o resource)
        casquilloVolumen = 0.35
    },
    sistema = {
        byteOriginal = 0x53
    }
}, archivoConfig)

local OFFSET_X = config.ajustes.offsetX
local OFFSET_Y = config.ajustes.offsetY
local MODO_CALIBRACION = not config.ajustes.calibrado
local menuIdiomaActivo = not config.ajustes.idiomaElegido

-- Efectos de balas del HUD (menu: escribe BALAS). Los valores por defecto reproducen el aspecto original.
local FX_DEF = {
    fxCasquillo = 1,       -- 1 clasico, 2 laton, 3 neon, 4 pesado, 5 flotante, 6 rafaga, 7 sin casquillos
    fxPaleta = 1,          -- 1 ambar, 2 blanco, 3 azul chaleco, 4 rojo alerta
    fxIntensidad = 1.0,
    fxDestello = true, fxChispas = false, fxHumo = false, fxImpacto = false,
    fxBrasas = false, fxFragmentos = false, fxRetroceso = false, fxEstela = true,
    fxRetrocesoModo = 1,   -- 1 horizontal, 2 vertical, 3 ambos
    fxResplandor = false, fxHumoCas = false, fxPulso = false, fxRecarga = false,
    -- efectos del chaleco
    chDestello = true, chOnda = true, chSacudida = true, chBarraDano = true, chRotura = true,
    -- barra del chaleco: estilo (1 clasica, 2 segmentos, 3 neon, 4 linea fina, 5 degradado) y efectos
    chEstiloBarra = 1, chBarraBrillo = false, chBarraLatido = false, chBarraPunta = false,
    -- texto del HUD: estilo (1 clasico, 2 neon, 3 contorno, 4 glitch, 5 retro), animacion de recarga (1 clasica, 2 ola, 3 maquina, 4 glitch, 5 barra segmentos), fuente ttf (0 = por defecto)
    txtEstilo = 1, txtRecarga = 1, txtFuente = 0, txtPop = true,
    casModo = 1   -- casquillos: 1 un rebote y se deslizan, 2 siguen rebotando, 3 caen de largo fuera de la pantalla
}
for k, v in pairs(FX_DEF) do
    if config.ajustes[k] == nil then config.ajustes[k] = v end
end

-- ==================== DICCIONARIO BILINGÜE (100% COMPATIBLE ASCII) ====================
local TEXTOS = {
    es = {
        recargando     = "RECARGANDO",
        sinMunicion    = "SIN MUNICION",
        calibTitulo    = "CALIBRACION DE MIRA",
        calibFlechas   = "Flechas: Mover",
        calibShift     = "SHIFT: Cambiar velocidad (Actual: %s)",
        calibIdioma    = "L: Cambiar idioma [ES/EN] (Actual: Espanol)",
        calibBorrar    = "BORRAR: Volver al centro",
        calibEnter     = "ENTER: GUARDAR y OCULTAR",
        calibAvisoCheat= "(Ctrl+M: abrir el menu)",
        calibVelLenta  = "LENTA (Precisa)",
        calibVelNormal = "NORMAL",
        avisoNoApunta  = "CALIBRACION DE MIRA activa: apunta con un arma para ajustarla\nL: Cambiar idioma [ES/EN]\nENTER: terminar la calibracion"
    },
    en = {
        recargando     = "RELOADING",
        sinMunicion    = "OUT OF AMMO",
        calibTitulo    = "CROSSHAIR CALIBRATION",
        calibFlechas   = "Arrow keys: Move",
        calibShift     = "SHIFT: Toggle speed (Current: %s)",
        calibIdioma    = "L: Switch language [ES/EN] (Current: English)",
        calibBorrar    = "BACKSPACE: Reset to center",
        calibEnter     = "ENTER: SAVE & HIDE",
        calibAvisoCheat= "(Ctrl+M: open the menu)",
        calibVelLenta  = "SLOW (Precise)",
        calibVelNormal = "NORMAL",
        avisoNoApunta  = "CROSSHAIR CALIBRATION active: aim with a weapon to adjust\nL: Switch language [ES/EN]\nENTER: finish calibration"
    }
}

local TX_FX = {
    es = {
        fxTitulo = "EFECTOS DE BALAS - HUD", fxPresets = "PRESETS", fxEfectos = "EFECTOS AL DISPARAR",
        fxCasquillo = "CASQUILLO", fxColor = "COLOR DE EFECTOS", fxIntensidad = "Intensidad",
        fxProbar = "PROBAR DISPARO", fxAuto = "Probar en automatico", fxCerrar = "CERRAR",
        fxSinArma = "Equipa un arma con cargador para ver el HUD y probar.",
        fxAyuda = "Escribe BALAS para abrir o cerrar este menu",
        fxE_destello = "Destello", fxT_destello = "Fogonazo breve en la bala que sale del cargador",
        fxE_chispas = "Chispas", fxT_chispas = "Chispas que saltan hacia arriba y caen con gravedad",
        fxE_humo = "Humo", fxT_humo = "Volutas grises que suben y se disipan",
        fxE_impacto = "Impacto", fxT_impacto = "Onda expansiva con rayos de impacto",
        fxE_brasas = "Brasas", fxT_brasas = "Puntos incandescentes que flotan y titilan",
        fxE_fragmentos = "Fragmentos", fxT_fragmentos = "Esquirlas metalicas que giran y caen",
        fxE_retroceso = "Retroceso del cargador", fxT_retroceso = "El bloque de municion da un tiron al disparar (elige la direccion abajo)",
        fxRetroDir = "DIRECCION DEL RETROCESO", fxR_h = "Horizontal", fxR_v = "Vertical", fxR_a = "Ambos",
        fxE_resplandor = "Resplandor", fxT_resplandor = "Halo suave del color elegido alrededor de la bala al disparar",
        fxE_humocas = "Humo del casquillo", fxT_humocas = "Un hilo de humo sigue al casquillo mientras vuela",
        fxE_pulso = "Pulso del contador", fxT_pulso = "El numero de balas salta y se tine del color elegido",
        fxE_recarga = "Brillo de recarga", fxT_recarga = "Cada bala brilla al volver al cargador",
        fxE_estela = "Estela del casquillo", fxT_estela = "Rastro fantasma mientras el casquillo vuela",
        fxC_clasico = "Clasico", fxC_dorado = "Laton dorado", fxC_neon = "Neon", fxC_pesado = "Pesado",
        fxC_flotante = "Flotante", fxC_rafaga = "Rafaga doble", fxC_ninguno = "Sin casquillos",
        fxP_ambar = "Ambar", fxP_blanco = "Blanco", fxP_azul = "Azul chaleco", fxP_rojo = "Rojo alerta",
        fxPre_vanilla = "Vanilla", fxPre_arcade = "Arcade", fxPre_cine = "Cinematico",
        fxPre_neon = "Neon", fxPre_metralla = "Metralla", fxPre_limpio = "Limpio"
    },
    en = {
        fxTitulo = "BULLET EFFECTS - HUD", fxPresets = "PRESETS", fxEfectos = "FIRING EFFECTS",
        fxCasquillo = "SHELL CASING", fxColor = "EFFECT COLOR", fxIntensidad = "Intensity",
        fxProbar = "TEST SHOT", fxAuto = "Auto test", fxCerrar = "CLOSE",
        fxSinArma = "Equip a weapon with a magazine to see the HUD and test.",
        fxAyuda = "Type BALAS to open or close this menu",
        fxE_destello = "Muzzle flash", fxT_destello = "Short flash on the bullet leaving the magazine",
        fxE_chispas = "Sparks", fxT_chispas = "Sparks that fly up and fall with gravity",
        fxE_humo = "Smoke", fxT_humo = "Grey wisps that rise and fade",
        fxE_impacto = "Impact", fxT_impacto = "Shockwave ring with impact streaks",
        fxE_brasas = "Embers", fxT_brasas = "Glowing dots that float and flicker",
        fxE_fragmentos = "Shards", fxT_fragmentos = "Metal splinters that spin and fall",
        fxE_retroceso = "Magazine recoil", fxT_retroceso = "The ammo block kicks when you fire (pick the direction below)",
        fxRetroDir = "RECOIL DIRECTION", fxR_h = "Horizontal", fxR_v = "Vertical", fxR_a = "Both",
        fxE_resplandor = "Glow", fxT_resplandor = "Soft glow in the chosen color around the bullet when firing",
        fxE_humocas = "Casing smoke", fxT_humocas = "A thin smoke trail follows the casing while it flies",
        fxE_pulso = "Counter pulse", fxT_pulso = "The bullet counter pops and tints to the chosen color",
        fxE_recarga = "Reload shine", fxT_recarga = "Each bullet shines as it returns to the magazine",
        fxE_estela = "Casing trail", fxT_estela = "Ghost trail while the casing flies",
        fxC_clasico = "Classic", fxC_dorado = "Gold brass", fxC_neon = "Neon", fxC_pesado = "Heavy",
        fxC_flotante = "Floating", fxC_rafaga = "Double burst", fxC_ninguno = "No casings",
        fxP_ambar = "Amber", fxP_blanco = "White", fxP_azul = "Armor blue", fxP_rojo = "Alert red",
        fxPre_vanilla = "Vanilla", fxPre_arcade = "Arcade", fxPre_cine = "Cinematic",
        fxPre_neon = "Neon", fxPre_metralla = "Shrapnel", fxPre_limpio = "Clean"
    }
}
for lang, tb in pairs(TX_FX) do
    for k, v in pairs(tb) do TEXTOS[lang][k] = v end
end

local TX_CH = {
    es = {
        fxChaleco = "CHALECO", fxProbarGolpe = "PROBAR GOLPE", fxProbarRotura = "PROBAR ROTURA",
        fxCh_destello = "Destello de impacto", fxChT_destello = "El chaleco se ilumina al recibir un golpe",
        fxCh_onda = "Onda de escudo", fxChT_onda = "Anillo azul que se expande al recibir dano",
        fxCh_sacudida = "Sacudida", fxChT_sacudida = "El chaleco y su barra tiemblan al recibir dano",
        fxCh_barra = "Barra de dano reciente", fxChT_barra = "Trozo claro que se vacia despues de perder armadura",
        fxCh_rotura = "Rotura del chaleco", fxChT_rotura = "Explosion de fragmentos y grietas al llegar a 0"
    },
    en = {
        fxChaleco = "ARMOR", fxProbarGolpe = "TEST HIT", fxProbarRotura = "TEST BREAK",
        fxCh_destello = "Impact flash", fxChT_destello = "The armor lights up when it takes a hit",
        fxCh_onda = "Shield ripple", fxChT_onda = "Blue ring that expands when you take damage",
        fxCh_sacudida = "Shake", fxChT_sacudida = "The armor and its bar shake when you take damage",
        fxCh_barra = "Recent damage bar", fxChT_barra = "Light segment that drains after losing armor",
        fxCh_rotura = "Armor break", fxChT_rotura = "Burst of shards and cracks when it hits 0"
    }
}
for lang, tb in pairs(TX_CH) do
    for k, v in pairs(tb) do TEXTOS[lang][k] = v end
end

local TX_AJ = {
    es = {
        ajTitulo = "AJUSTES", ajIdioma = "IDIOMA", ajHud = "HUD",
        ajMunicion = "HUD de municion", ajChaleco = "HUD de chaleco", ajBarra = "Barra del chaleco",
        ajEfectos = "Efectos de balas y chaleco...", ajAyuda = "Ctrl+M abre o cierra este menu"
    },
    en = {
        ajTitulo = "SETTINGS", ajIdioma = "LANGUAGE", ajHud = "HUD",
        ajMunicion = "Ammo HUD", ajChaleco = "Armor HUD", ajBarra = "Armor bar",
        ajEfectos = "Bullet and armor effects...", ajAyuda = "Press Ctrl+M to open or close this menu"
    }
}
for lang, tb in pairs(TX_AJ) do
    for k, v in pairs(tb) do TEXTOS[lang][k] = v end
end

local TX_BARRA = {
    es = {
        fxBarra = "BARRA DEL CHALECO", fxBEfectos = "EFECTOS DE LA BARRA",
        fxB_clasico = "Clasica", fxB_segmentos = "Segmentos", fxB_neon = "Neon",
        fxB_linea = "Linea fina", fxB_degradado = "Degradado",
        fxBE_brillo = "Barrido de brillo", fxBT_brillo = "Una franja de luz recorre la barra",
        fxBE_latido = "Latido al bajar", fxBT_latido = "Halo rojo que late cuando queda poca armadura",
        fxBE_punta = "Punta brillante", fxBT_punta = "Resplandor en el borde del nivel actual",
        fxBVista = "Vista previa del chaleco", fxBVistaTip = "Muestra el chaleco con un nivel de prueba mientras este menu esta abierto"
    },
    en = {
        fxBarra = "ARMOR BAR", fxBEfectos = "BAR EFFECTS",
        fxB_clasico = "Classic", fxB_segmentos = "Segments", fxB_neon = "Neon",
        fxB_linea = "Thin line", fxB_degradado = "Gradient",
        fxBE_brillo = "Shine sweep", fxBT_brillo = "A band of light runs along the bar",
        fxBE_latido = "Low armor pulse", fxBT_latido = "Red halo that pulses when armor is low",
        fxBE_punta = "Glowing tip", fxBT_punta = "Glow on the edge of the current level",
        fxBVista = "Armor preview", fxBVistaTip = "Shows the armor with a test level while this menu is open"
    }
}
for lang, tb in pairs(TX_BARRA) do
    for k, v in pairs(tb) do TEXTOS[lang][k] = v end
end

local TX_MENU = {
    es = {
        tabGeneral = "GENERAL", tabBalas = "BALAS", tabChaleco = "CHALECO", ajMira = "MIRA",
        ajCalibrar = "Calibrar mira", ajCalibrarFin = "Guardar y terminar calibracion",
        ajCalibrarTip = "Cierra el menu: apunta con un arma y mueve la mira con las flechas"
    },
    en = {
        tabGeneral = "GENERAL", tabBalas = "BULLETS", tabChaleco = "ARMOR", ajMira = "CROSSHAIR",
        ajCalibrar = "Calibrate crosshair", ajCalibrarFin = "Save and finish calibration",
        ajCalibrarTip = "Closes the menu: aim with a weapon and move the crosshair with the arrow keys"
    }
}
for lang, tb in pairs(TX_MENU) do
    for k, v in pairs(tb) do TEXTOS[lang][k] = v end
end

do
    local TX_TXT = {
        es = { tabPos = "POSICION", posHud = "HUD DE MUNICION", posChaleco = "CHALECO", posSprint = "SPRINT", posX = "Distancia al borde derecho", posY = "Altura desde abajo", posEsc = "Escala", posEscSprint = "(la escala del sprint usa la del HUD de municion)", posReset = "RESTABLECER POSICIONES", casModo = "Comportamiento del casquillo", casM_1 = "Un rebote", casM_2 = "Siguen rebotando", casM_3 = "Caen de largo", txtTitulo = "TEXTO DEL HUD", txtEstilo = "Estilo de numeros y texto", txtRecarga = "Animacion de RECARGANDO",
               txtFuente = "Fuente", txtFuenteDef = "Por defecto", txtPop = "Pop al disparar", txtPrevia = "Previsualizar recarga",
               txtSinFuentes = "Para mas fuentes: pon archivos .ttf en moonloader/resource/fonts y reinicia",
               txtE_1 = "Clasico", txtE_2 = "Neon", txtE_3 = "Contorno", txtE_4 = "Glitch", txtE_5 = "Retro",
               txtR_1 = "Clasica", txtR_2 = "Ola", txtR_3 = "Maquina", txtR_4 = "Glitch", txtR_5 = "Segmentos" },
        en = { tabPos = "POSITION", posHud = "AMMO HUD", posChaleco = "ARMOR", posSprint = "SPRINT", posX = "Distance from right edge", posY = "Height from bottom", posEsc = "Scale", posEscSprint = "(sprint scale uses the ammo HUD scale)", posReset = "RESET POSITIONS", casModo = "Casing behavior", casM_1 = "One bounce", casM_2 = "Keep bouncing", casM_3 = "Fall through", txtTitulo = "HUD TEXT", txtEstilo = "Number and text style", txtRecarga = "RELOADING animation",
               txtFuente = "Font", txtFuenteDef = "Default", txtPop = "Pop on shot", txtPrevia = "Preview reload",
               txtSinFuentes = "For more fonts: put .ttf files in moonloader/resource/fonts and restart",
               txtE_1 = "Classic", txtE_2 = "Neon", txtE_3 = "Outline", txtE_4 = "Glitch", txtE_5 = "Retro",
               txtR_1 = "Classic", txtR_2 = "Wave", txtR_3 = "Typewriter", txtR_4 = "Glitch", txtR_5 = "Segments" }
    }
    for lang, tb in pairs(TX_TXT) do
        for k, v in pairs(tb) do TEXTOS[lang][k] = v end
    end
end

local function T(clave)
    local lang = config.ajustes.idioma or "es"
    if not TEXTOS[lang] then lang = "es" end
    return TEXTOS[lang][clave] or TEXTOS["es"][clave] or ""
end

local TX_MIRA = {
    es = {
        tabMira = "MIRA", miraC_mP = "PISTOLAS / SMG", miraC_mR = "RIFLES", miraC_mE = "ESCOPETAS", miraC_mO = "OTRAS",
        miraEstilo = "Diseno de mira", miraVista = "Vista previa",
        miraE_cruz = "Cruz clasica", miraE_punto = "Punto", miraE_aro = "Aro", miraE_cruzpunto = "Cruz con punto",
        miraE_t = "Cruz en T", miraE_esquinas = "Esquinas", miraE_chevron = "Chevron", miraE_diamante = "Diamante",
        miraE_aroticks = "Aro con marcas", miraE_puntos4 = "Cuatro puntos",
        miraE_arcos = "Arcos ( )", miraE_dobleAro = "Doble aro", miraE_aroPartido = "Aro partido", miraE_cuadrado = "Cuadrado",
        miraE_corchetes = "Corchetes [ ]", miraE_equis = "Equis (X)", miraE_perdigones = "Perdigones", miraE_mirilla = "Mirilla",
        miraTam = "Tamano", miraGrosor = "Grosor", miraSep = "Separacion", miraExp = "Apertura al disparar",
        miraOp = "Opacidad", miraPunto = "Punto central", miraContorno = "Contorno negro", miraColor = "Color",
        miraNotaEnemigo = "El color pasa a rojo sobre enemigos si esa opcion esta activa.",
        miraAplicarTodas = "Aplicar a todas las armas", miraRestaurar = "Restaurar esta categoria"
    },
    en = {
        tabMira = "CROSSHAIR", miraC_mP = "PISTOLS / SMG", miraC_mR = "RIFLES", miraC_mE = "SHOTGUNS", miraC_mO = "OTHERS",
        miraEstilo = "Crosshair design", miraVista = "Preview",
        miraE_cruz = "Classic cross", miraE_punto = "Dot", miraE_aro = "Ring", miraE_cruzpunto = "Cross with dot",
        miraE_t = "T cross", miraE_esquinas = "Corners", miraE_chevron = "Chevron", miraE_diamante = "Diamond",
        miraE_aroticks = "Ring with ticks", miraE_puntos4 = "Four dots",
        miraE_arcos = "Arcs ( )", miraE_dobleAro = "Double ring", miraE_aroPartido = "Broken ring", miraE_cuadrado = "Square",
        miraE_corchetes = "Brackets [ ]", miraE_equis = "X cross", miraE_perdigones = "Pellets", miraE_mirilla = "Scope",
        miraTam = "Size", miraGrosor = "Thickness", miraSep = "Gap", miraExp = "Spread when shooting",
        miraOp = "Opacity", miraPunto = "Center dot", miraContorno = "Black outline", miraColor = "Color",
        miraNotaEnemigo = "The color turns red over enemies if that option is enabled.",
        miraAplicarTodas = "Apply to all weapons", miraRestaurar = "Reset this category"
    }
}
for lang, tb in pairs(TX_MIRA) do
    for k, v in pairs(tb) do TEXTOS[lang][k] = v end
end

-- Direcciones de memoria GTA SA 1.0 US
local MEM_CROSSHAIR = 0x58E020
local MEM_CAM_MODE  = 0xB6F1A8
local MEM_PAUSED    = 0xBA67A4
local SPRINT_OFFSET = 0x18 -- CPlayerData::m_fTimeCanRun (24 decimal)
local BYTE_ORIGINAL_DEFAULT = 0x53

local MODOS_APUNTADO = { [53] = true, [55] = true, [63] = true }
local ARMAS_MIRA_ORIGINAL = { [34] = true, [35] = true, [36] = true }
local ARMA_COUNTRY_RIFLE = 33
local ARMAS_MIRA_CIRCULAR = { [25] = true, [26] = true, [27] = true }  -- escopetas: mira en aro
local MODOS_RIFLE = { [7] = true, [8] = true, [34] = true, [39] = true, [40] = true, [42] = true, [51] = true, [52] = true }

local originalByte = (config.sistema.byteOriginal ~= 0 and config.sistema.byteOriginal ~= 0xC3)
                     and config.sistema.byteOriginal or BYTE_ORIGINAL_DEFAULT
local parcheActivo = false
local fuente = nil
local fuenteMunicion = nil
local fuenteMenu = nil
local modListo = false

local function col(a, r, g, b)
    a = max(0, min(255, floor(a)))
    return a * 16777216 + floor(r) * 65536 + floor(g) * 256 + floor(b)
end

local function guardarConfig()
    config.ajustes.offsetX = floor(OFFSET_X + 0.5)
    config.ajustes.offsetY = floor(OFFSET_Y + 0.5)
    config.ajustes.idioma = config.ajustes.idioma or "es"
    config.ajustes.idiomaElegido = (config.ajustes.idiomaElegido == true)
    inicfg.save(config, archivoConfig)
end

-- MP: hide the game's crosshair on Android with a hook on CHud::DrawCrossHairs (skipped while parcheActivo)
function MP.hookCrosshair()
    if MP.hook ~= nil or not MP.android then return end
    MP.hook = false
    local okH, hook = pcall(require, 'monethook')
    if not okH or not getModuleProcAddress then return end
    local addr = getModuleProcAddress('libGTASA.so', '_ZN4CHud14DrawCrossHairsEv')
    if not addr or addr == 0 then return end
    local h
    local ok, res = pcall(hook.new, 'void(*)()', function()
        if parcheActivo and not MP.off then return end
        return h()
    end, addr)
    if ok and res then h = res; MP.hook = res else print('[ammohud] crosshair hook failed: ' .. tostring(res)) end
end

local function ponerParche(activar)
    if MP.android then parcheActivo = activar and true or false; return end
    if activar then
        if not parcheActivo then
            writeMemory(MEM_CROSSHAIR, 1, 0xC3, true)
            parcheActivo = true
        end
    elseif parcheActivo then
        writeMemory(MEM_CROSSHAIR, 1, originalByte, true)
        parcheActivo = false
    end
end

-- MP: camera mode on Android. 53 = aiming over the shoulder, 7 = sniper/rifle scope, 4 = normal follow.
function mpCamMode(arma)
    if not doesCharExist(playerPed) or not isCharOnFoot(playerPed) then return 4 end
    local aiming = false
    local okT, t = pcall(isPlayerTargettingAnything, playerHandle)
    if okT and t then aiming = true end
    local cx, cy, cz = getActiveCameraCoordinates()
    local px, py, pz = getCharCoordinates(playerPed)
    if cx and px then
        local d = math.sqrt((cx - px) ^ 2 + (cy - py) ^ 2 + (cz - pz) ^ 2)
        if d < 0.9 then return 7 end
        if d < 2.7 then aiming = true end
    end
    return aiming and 53 or 4
end

local function pausaActiva()
    local ok, v = pcall(isPauseMenuActive)
    if ok and v == true then return true end
    local okG, g = pcall(isGamePaused)
    if okG and g == true then return true end
    local okR, bR = pcall(readMemory, MEM_PAUSED, 1, false)
    if okR and type(bR) == "number" and bR ~= 0 then return true end
    return false
end

-- ==================== DETECCION DE OBJETIVO Y ENEMIGOS ====================
local function objetivoActual()
    if not isPlayerPlaying(playerHandle) then return nil end
    local ok, a, b = pcall(getCharPlayerIsTargeting, playerHandle)
    if not ok then return nil end
    local ped = nil
    if a == true and type(b) == "number" then
        ped = b
    elseif type(a) == "number" and a > 0 then
        ped = a
    end
    if ped and ped > 0 and doesCharExist(ped) then return ped end
    return nil
end

local function esAliado(ped)
    if not doesCharExist(ped) then return false end
    local ok, grp = pcall(getPlayerGroup, playerHandle)
    if not ok or not grp then return false end
    local okM, miembro = pcall(isGroupMember, grp, ped)
    return okM and miembro == true
end

local function crucetaSobrePed(ped, cx, cy)
    if not doesCharExist(ped) or not isCharOnScreen(ped) then return false end
    local px, py, pz = getCharCoordinates(ped)
    local xa, ya = convert3DCoordsToScreen(px, py, pz + 0.85)
    local xb, yb = convert3DCoordsToScreen(px, py, pz - 0.95)
    if not (xa and ya and xb and yb) then return false end
    local alto = abs(yb - ya)
    local semiAncho = max(alto * 0.28, 14)
    local margenY = max(alto * 0.06, 10)
    return abs(cx - (xa + xb) * 0.5) <= semiAncho
       and cy >= min(ya, yb) - margenY and cy <= max(ya, yb) + margenY
end

-- ==================== DIBUJO DE LA MIRA ====================
local function dibujarCruzClasica(cx, cy, gapBase, largo, grosor, spread, color, opacidad)
    cx = floor(cx)
    cy = floor(cy)
    local colorFondo = col(opacidad, 0, 0, 0)
    local gap = floor(gapBase + spread)
    local l = floor(largo)
    local gro = floor(grosor)
    local rGro = floor(gro * 0.5)
    local x0, y0 = cx - rGro, cy - rGro
    local x1, y1 = x0 + gro, y0 + gro

    if l > 0 and gro > 0 then
        renderDrawBox(x0 - gap - l - 1, y0 - 1, l + 2, gro + 2, colorFondo)
        renderDrawBox(x1 + gap - 1,     y0 - 1, l + 2, gro + 2, colorFondo)
        renderDrawBox(x0 - 1, y0 - gap - l - 1, gro + 2, l + 2, colorFondo)
        renderDrawBox(x0 - 1, y1 + gap - 1,     gro + 2, l + 2, colorFondo)

        if gap <= 1 then
            renderDrawBox(x0 - 1, y0 - 1, gro + 2, gro + 2, colorFondo)
        end

        renderDrawBox(x0 - gap - l, y0, l, gro, color)
        renderDrawBox(x1 + gap,     y0, l, gro, color)
        renderDrawBox(x0, y0 - gap - l, gro, l, color)
        renderDrawBox(x0, y1 + gap,     gro, l, color)

        if gap == 0 then
            renderDrawBox(x0, y0, gro, gro, color)
        end
    end
end

-- Respaldo (sin mimgui): aro dibujado con 40 segmentos de linea
local function dibujarMiraCircularLineas(cx, cy, radio, grosor, spread, color, opacidad, punto)
    local r = radio + spread * 0.9
    local seg = 40
    local colorFondo = col(opacidad, 0, 0, 0)
    for pasada = 1, 2 do
        local c = (pasada == 1) and colorFondo or color
        local g = (pasada == 1) and (grosor + 1.6) or grosor
        for i = 0, seg - 1 do
            local a1 = (i / seg) * 2 * math.pi
            local a2 = ((i + 1) / seg) * 2 * math.pi
            renderDrawLine(cx + cos(a1) * r, cy + sin(a1) * r, cx + cos(a2) * r, cy + sin(a2) * r, g, c)
        end
    end
    if punto then
        local p = 2
        renderDrawBox(cx - 2, cy - 2, p + 2, p + 2, colorFondo)
        renderDrawBox(cx - 1, cy - 1, p, p, color)
    end
end

-- Mira circular para escopetas: aro REAL (DrawList:AddCircle, con antialiasing) con contorno negro y un punto central.
-- Se abre un poco al disparar. Son 2-4 llamadas en vez de ~80 lineas por frame.
local function circuloMimgui(cx, cy, r, grosor, seg, colorFondo, color, punto)
    P1.x, P1.y = cx, cy
    DL:AddCircle(P1, r, argbToImU32(colorFondo), seg, grosor + 1.6 * K)
    DL:AddCircle(P1, r, argbToImU32(color), seg, grosor)
    if punto then
        DL:AddCircleFilled(P1, 2.2 * K, argbToImU32(colorFondo), 12)
        DL:AddCircleFilled(P1, 1.3 * K, argbToImU32(color), 12)
    end
end

local function dibujarMiraCircular(cx, cy, radio, grosor, spread, color, opacidad, punto)
    if USAR_MIMGUI and DL then
        local r = radio + spread * 0.9
        local seg = max(24, min(72, floor(r * 2.2)))
        if pcall(circuloMimgui, cx, cy, r, grosor, seg, col(opacidad, 0, 0, 0), color, punto) then return end
    end
    dibujarMiraCircularLineas(cx, cy, radio, grosor, spread, color, opacidad, punto)
end

-- ==================== VARIACIONES DE MIRA (pistolas / rifles / escopetas / otras) ====================
local ESTILOS_MIRA = { "cruz", "punto", "aro", "cruzpunto", "t", "esquinas", "chevron", "diamante", "aroticks", "puntos4",
                       "arcos", "dobleAro", "aroPartido", "cuadrado", "corchetes", "equis", "perdigones", "mirilla" }
local EST_DEF = {
    cruz      = { tam = 4,  grosor = 3,   sep = 0 },
    punto     = { tam = 3,  grosor = 1,   sep = 0 },
    aro       = { tam = 11, grosor = 1.5, sep = 0, punto = true },
    cruzpunto = { tam = 5,  grosor = 2,   sep = 4 },
    t         = { tam = 6,  grosor = 2,   sep = 3 },
    esquinas  = { tam = 12, grosor = 2,   sep = 0 },
    chevron   = { tam = 8,  grosor = 2,   sep = 3 },
    diamante  = { tam = 8,  grosor = 1.5, sep = 0 },
    aroticks  = { tam = 9,  grosor = 1.5, sep = 3 },
    puntos4   = { tam = 9,  grosor = 2,   sep = 0 },
    arcos      = { tam = 12, grosor = 2,   sep = 0, punto = true },
    dobleAro   = { tam = 13, grosor = 1.5, sep = 0 },
    aroPartido = { tam = 11, grosor = 2,   sep = 0 },
    cuadrado   = { tam = 9,  grosor = 1.5, sep = 0, punto = true },
    corchetes  = { tam = 11, grosor = 2,   sep = 0, punto = true },
    equis      = { tam = 6,  grosor = 2,   sep = 3 },
    perdigones = { tam = 11, grosor = 2,   sep = 0, punto = true },
    mirilla    = { tam = 14, grosor = 1.5, sep = 3 }
}

-- categoria de cada arma: mP pistolas/SMG, mR rifles, mE escopetas, mO el resto
local CAT_MIRA_ARMA = {
    [22] = "mP", [23] = "mP", [24] = "mP", [28] = "mP", [29] = "mP", [32] = "mP",
    [30] = "mR", [31] = "mR", [33] = "mR",
    [25] = "mE", [26] = "mE", [27] = "mE"
}
local CATS_MIRA = { "mP", "mR", "mE", "mO" }
local KEYS_MIRA = { "Estilo", "Tam", "Grosor", "Sep", "Punto", "Exp", "Op", "ColR", "ColG", "ColB", "Contorno" }

-- Valores efectivos de una categoria. Si el .ini no tiene "Estilo" se usa el aspecto original (cruz; aro en escopetas).
local function ajusteMiraCat(pref)
    local A = config.ajustes
    local r = {}
    local est = A[pref .. "Estilo"]
    if est == nil then
        if pref == "mE" and A.escopetaActiva ~= false then
            r.estilo, r.tam, r.grosor, r.sep = 3, A.escopetaRadio or 11.0, A.escopetaGrosor or 1.5, 0
            r.punto = (A.escopetaPunto ~= false)
        else
            r.estilo, r.tam, r.grosor, r.sep = 1, A.largoLinea or 4.0, A.grosorLinea or 3.0, A.separacion or 0.0
            r.punto = false
        end
    else
        local d = EST_DEF[ESTILOS_MIRA[est]] or EST_DEF.cruz
        r.estilo = est
        r.tam = A[pref .. "Tam"] or d.tam
        r.grosor = A[pref .. "Grosor"] or d.grosor
        r.sep = A[pref .. "Sep"] or d.sep
        r.punto = (A[pref .. "Punto"] == true)
    end
    r.exp = A[pref .. "Exp"] or 1.0
    r.op = A[pref .. "Op"] or A.opacidad or 255
    r.r, r.g, r.b = A[pref .. "ColR"] or 255, A[pref .. "ColG"] or 255, A[pref .. "ColB"] or 255
    r.contorno = (A[pref .. "Contorno"] ~= false)
    return r
end

local function ajusteMira(armaId)
    return ajusteMiraCat(CAT_MIRA_ARMA[armaId] or "mO")
end

-- segmentos reutilizables: primero todos los contornos negros y luego los colores, asi las esquinas no se manchan
local SEG, nSeg = {}, 0
local function addSeg(x1, y1, x2, y2)
    nSeg = nSeg + 1
    local sg = SEG[nSeg]
    if not sg then sg = { 0, 0, 0, 0 }; SEG[nSeg] = sg end
    sg[1], sg[2], sg[3], sg[4] = x1, y1, x2, y2
end
-- arco de a1 a a2 (radianes) hecho con segmentos; sirve igual con mimgui que sin el
function SEG.arco(cx, cy, r, a1, a2)
    local n = max(4, floor(abs(a2 - a1) * r / 4))
    local px, py = cx + cos(a1) * r, cy + sin(a1) * r
    for i = 1, n do
        local a = a1 + (a2 - a1) * i / n
        local x, y = cx + cos(a) * r, cy + sin(a) * r
        addSeg(px, py, x, y)
        px, py = x, y
    end
end
local function volcarSeg(g, color, fondo, contorno)
    if contorno then
        for i = 1, nSeg do local sg = SEG[i]; renderDrawLine(sg[1], sg[2], sg[3], sg[4], g + 2, fondo) end
    end
    for i = 1, nSeg do local sg = SEG[i]; renderDrawLine(sg[1], sg[2], sg[3], sg[4], g, color) end
    nSeg = 0
end

local function puntoMira(x, y, r, color, fondo, contorno)
    if contorno then circuloLleno(x, y, r + 1.2 * K, fondo) end
    circuloLleno(x, y, r, color)
end

-- Todas las medidas llegan ya multiplicadas por K. spread = apertura por disparo (px).
local function dibujarMiraEstilo(est, cx, cy, tam, grosor, gap, spread, color, op, punto, contorno)
    local id = ESTILOS_MIRA[est] or "cruz"
    local opFondo = contorno and op or 0           -- opacidad del contorno negro (0 = sin contorno)
    local fondo = col(opFondo, 0, 0, 0)
    cx, cy = floor(cx), floor(cy)
    local conPunto = punto

    if id == "cruz" then
        dibujarCruzClasica(cx, cy, gap, tam, grosor, spread, color, opFondo)
        conPunto = false
    elseif id == "punto" then
        puntoMira(cx, cy, max(1.2 * K, tam + spread * 0.25), color, fondo, contorno)
        conPunto = false
    elseif id == "aro" then
        dibujarMiraCircular(cx, cy, tam, grosor, spread, color, opFondo, punto)
        conPunto = false
    elseif id == "cruzpunto" or id == "t" then
        local g0 = gap + spread
        local g1 = g0 + tam
        addSeg(cx - g1, cy, cx - g0, cy)
        addSeg(cx + g0, cy, cx + g1, cy)
        addSeg(cx, cy + g0, cx, cy + g1)
        if id == "cruzpunto" then
            addSeg(cx, cy - g1, cx, cy - g0)
            conPunto = true
        end
        volcarSeg(grosor, color, fondo, contorno)
    elseif id == "esquinas" then
        local d, l = tam + spread, tam * 0.45
        for sx = -1, 1, 2 do
            for sy = -1, 1, 2 do
                local px, py = cx + sx * d, cy + sy * d
                addSeg(px, py, px - sx * l, py)
                addSeg(px, py, px, py - sy * l)
            end
        end
        volcarSeg(grosor, color, fondo, contorno)
    elseif id == "chevron" then
        local ay = cy - gap - spread * 0.6
        addSeg(cx - tam, ay + tam * 0.85, cx, ay)
        addSeg(cx, ay, cx + tam, ay + tam * 0.85)
        volcarSeg(grosor, color, fondo, contorno)
    elseif id == "diamante" then
        local d = tam + spread * 0.8
        addSeg(cx, cy - d, cx + d, cy)
        addSeg(cx + d, cy, cx, cy + d)
        addSeg(cx, cy + d, cx - d, cy)
        addSeg(cx - d, cy, cx, cy - d)
        volcarSeg(grosor, color, fondo, contorno)
    elseif id == "aroticks" then
        dibujarMiraCircular(cx, cy, tam, grosor, spread, color, opFondo, false)
        local r0 = tam + spread * 0.9 + 2 * K
        local l = max(gap, 2 * K)
        addSeg(cx + r0, cy, cx + r0 + l, cy)
        addSeg(cx - r0, cy, cx - r0 - l, cy)
        addSeg(cx, cy + r0, cx, cy + r0 + l)
        addSeg(cx, cy - r0, cx, cy - r0 - l)
        volcarSeg(grosor, color, fondo, contorno)
    elseif id == "puntos4" then
        local d = tam + spread
        local rr = max(1.2 * K, grosor)
        puntoMira(cx + d, cy, rr, color, fondo, contorno)
        puntoMira(cx - d, cy, rr, color, fondo, contorno)
        puntoMira(cx, cy + d, rr, color, fondo, contorno)
        puntoMira(cx, cy - d, rr, color, fondo, contorno)
    elseif id == "arcos" then                         -- ( . )  dos parentesis laterales
        local r = tam + spread * 0.9
        SEG.arco(cx, cy, r, -0.7, 0.7)
        SEG.arco(cx, cy, r, math.pi - 0.7, math.pi + 0.7)
        volcarSeg(grosor, color, fondo, contorno)
    elseif id == "dobleAro" then                      -- aro exterior + aro interior
        dibujarMiraCircular(cx, cy, tam, grosor, spread, color, opFondo, false)
        dibujarMiraCircular(cx, cy, max(2 * K, tam * 0.42), grosor, spread * 0.4, color, opFondo, false)
        conPunto = punto
    elseif id == "aroPartido" then                    -- aro en 4 arcos con huecos en las diagonales
        local r = tam + spread * 0.9
        for k = 0, 3 do
            local c = k * math.pi * 0.5
            SEG.arco(cx, cy, r, c - 0.55, c + 0.55)
        end
        volcarSeg(grosor, color, fondo, contorno)
    elseif id == "cuadrado" then
        local d = tam + spread * 0.8
        addSeg(cx - d, cy - d, cx + d, cy - d)
        addSeg(cx + d, cy - d, cx + d, cy + d)
        addSeg(cx + d, cy + d, cx - d, cy + d)
        addSeg(cx - d, cy + d, cx - d, cy - d)
        volcarSeg(grosor, color, fondo, contorno)
    elseif id == "corchetes" then                     -- [ . ]
        local d, h, l = tam + spread, tam * 0.8, tam * 0.5
        addSeg(cx - d, cy - h, cx - d, cy + h)
        addSeg(cx - d, cy - h, cx - d + l, cy - h)
        addSeg(cx - d, cy + h, cx - d + l, cy + h)
        addSeg(cx + d, cy - h, cx + d, cy + h)
        addSeg(cx + d, cy - h, cx + d - l, cy - h)
        addSeg(cx + d, cy + h, cx + d - l, cy + h)
        volcarSeg(grosor, color, fondo, contorno)
    elseif id == "equis" then                         -- cruz en diagonal (X)
        local g0 = (gap + spread) * 0.7071
        local g1 = g0 + tam * 0.7071
        for sx = -1, 1, 2 do
            for sy = -1, 1, 2 do
                addSeg(cx + sx * g0, cy + sy * g0, cx + sx * g1, cy + sy * g1)
            end
        end
        volcarSeg(grosor, color, fondo, contorno)
    elseif id == "perdigones" then                    -- 8 puntos en circulo: dispersion de escopeta
        local d = tam + spread * 0.9
        local rr = max(1.0 * K, grosor * 0.6)
        for k = 0, 7 do
            local a = k * math.pi * 0.25
            puntoMira(cx + cos(a) * d, cy + sin(a) * d, rr, color, fondo, contorno)
        end
    elseif id == "mirilla" then                       -- aro con cruz de francotirador
        local r = tam + spread * 0.5
        dibujarMiraCircular(cx, cy, r, grosor, 0, color, opFondo, false)
        local g0 = min(max(gap, 2 * K) + spread * 0.5, r - 2 * K)
        local ext = max(3 * K, tam * 0.3)
        addSeg(cx - r - ext, cy, cx - g0, cy)
        addSeg(cx + g0, cy, cx + r + ext, cy)
        addSeg(cx, cy - r - ext, cx, cy - g0)
        addSeg(cx, cy + g0, cx, cy + r + ext)
        volcarSeg(grosor, color, fondo, contorno)
    end

    if conPunto then puntoMira(cx, cy, max(1.1 * K, grosor * 0.55), color, fondo, contorno) end
end

-- ==================== HUD DE MUNICION ====================
local CLIP_ARMA = {
    [22] = 17, [23] = 17, [24] = 7, [25] = 1, [26] = 2, [27] = 7, [28] = 50,
    [29] = 30, [30] = 30, [31] = 50, [32] = 50, [33] = 1, [34] = 1, [35] = 1, [36] = 1, [37] = 1, [38] = 1,
    [16] = 1, [17] = 1, [18] = 1, [39] = 1, [41] = 1, [42] = 1
}
local MAX_ICONOS = 15
local MINI_BALAS = 30

local TIPO_ARMA = {
    [22] = "corta", [23] = "corta", [24] = "corta",
    [28] = "corta", [29] = "corta", [32] = "corta",
    [30] = "larga", [31] = "larga", [33] = "larga", [34] = "larga",
    [25] = "cartucho", [26] = "cartucho", [27] = "cartucho",
    [35] = "cohete", [36] = "cohete",
    [37] = "fuego",
    [38] = "minigun",
    [16] = "granada", [17] = "humo", [18] = "molotov", [39] = "satchel",
    [41] = "pintura", [42] = "extintor"
}

local ICONOS = {
    corta    = { archivo = "bala_corta.png", ratio = 0.422, alto = 32 },
    larga    = { archivo = "bala_larga.png", ratio = 0.233, alto = 52 },
    cartucho = { archivo = "cartucho.png", ratio = 0.406, alto = 34 },
    cohete   = { archivo = "misil.png", ratio = 0.375, alto = 54 },
    granada  = { archivo = "granada.png", ratio = 0.719, alto = 38 },
    humo     = { archivo = "humo.png", ratio = 0.562, alto = 44 },
    molotov  = { archivo = "molotov.png", ratio = 0.359, alto = 48 },
    satchel  = { archivo = "satchel.png", ratio = 0.805, alto = 38 },
    pintura  = { archivo = "pintura.png", ratio = 0.492, alto = 44 },
    extintor = { archivo = "extintor.png", ratio = 0.547, alto = 60 },
    fuego    = { archivo = "combustible.png", ratio = 0.523, alto = 60 },
    chaleco  = { archivo = "chaleco.png", ratio = 0.836, alto = 50 }
}
local TEX = {}
local texIntentado = false
local TIPOS_UNIDAD = { cohete = true, granada = true, humo = true, molotov = true, satchel = true }
local TIPOS_PORCENTAJE = { fuego = true, pintura = true, extintor = true }

local function cargarTexturas()
    if texIntentado then return end
    texIntentado = true
    local base = getWorkingDirectory() .. "\\"
    local carpetas = {
        base .. "resource\\ammo\\",
        base .. "resource\\chaleco\\",
        base .. "resource\\armor\\",
        base .. "resource\\hud\\",
        base .. "resource\\",
        base
    }
    for tipo, cfg in pairs(ICONOS) do
        for _, carpeta in ipairs(carpetas) do
            local ruta = carpeta .. cfg.archivo
            if doesFileExist(ruta) then
                local tex
                if USAR_MIMGUI then
                    local ok, t = pcall(imgui.CreateTextureFromFile, ruta)
                    if ok and t then tex = t end
                elseif natLoadTex then
                    local ok, t = pcall(natLoadTex, ruta)
                    if ok and t then tex = t end
                end
                if tex then
                    TEX[tipo] = tex
                    break
                else
                    print("[HUD] no se pudo cargar la imagen: " .. ruta)
                end
            end
        end
        if not TEX[tipo] then
            print("[HUD] no se encontro " .. cfg.archivo)
        end
    end
end

local hud = {
    arma = -1, totalPrev = 0, llenasPrev = -1, simClip = 0, simRecargaHasta = 0,
    ultimaActividad = -10, anim = {}, volando = {}, alpha = 0, maxMun = {},
    clipMax = {}  -- tamano REAL del cargador de cada arma, aprendido del juego (no de la tabla CLIP_ARMA)
}

-- Casquillos que salen volando del HUD: cada tipo de arma los expulsa distinto {fuerza lateral, fuerza vertical, giro}
local EYECCION = {
    base     = { 1.0, 1.0, 1.0 },
    corta    = { 1.0, 1.0, 1.0 },
    larga    = { 1.25, 1.1, 1.0 },
    cartucho = { 0.9, 1.25, 1.6 },     -- escopeta: salto alto y mucho giro
    cohete   = { 0.6, 0.8, 0.5 },
}
local casSnd, casUlt = nil, -10

local function cargarSonidoCasquillo()
    if config.ajustes.casquilloSonido == false or not loadAudioStream then return end
    local base = getWorkingDirectory() .. "\\"
    for _, c in ipairs({ base .. "resource\\ammo\\", base .. "resource\\hud\\", base .. "resource\\", base .. "resources\\", base }) do
        local ruta = c .. "casquillo.mp3"
        if doesFileExist(ruta) then
            local ok, st = pcall(loadAudioStream, ruta)
            if ok and st then casSnd = st; return end
        end
    end
end

local function reproducirCasquillo()
    if not casSnd then return end
    local ahora = os.clock()
    if ahora - casUlt < 0.05 then return end
    casUlt = ahora
    pcall(setAudioStreamVolume, casSnd, config.ajustes.casquilloVolumen or 0.35)
    pcall(setAudioStreamState, casSnd, 0)
    pcall(setAudioStreamState, casSnd, 1)
end

-- ==================== EFECTOS DE BALAS DEL HUD ====================
-- Paletas pensadas para el HUD: ambar (color de RECARGANDO), blanco (texto), azul (chaleco) y rojo (municion baja).
local PALETAS = {
    { id = "ambar",  c1 = { 255, 190, 60 },  c2 = { 255, 240, 190 } },
    { id = "blanco", c1 = { 235, 235, 235 }, c2 = { 255, 255, 255 } },
    { id = "azul",   c1 = { 110, 185, 255 }, c2 = { 215, 238, 255 } },
    { id = "rojo",   c1 = { 255, 80, 80 },   c2 = { 255, 190, 170 } }
}

-- Estilos de casquillo: gravedad, fuerza de expulsion, giro, cuantos salen por disparo, vida (s) y rebote
local ESTILOS_CAS = {
    { id = "clasico",  grav = 1100, vel = 1.0,  giro = 1.0,  cant = 1, vida = 1.5, rebote = 0.35 },
    { id = "dorado",   grav = 1100, vel = 1.0,  giro = 1.0,  cant = 1, vida = 1.5, rebote = 0.35, tint = { 255, 214, 120 } },
    { id = "neon",     grav = 1100, vel = 1.0,  giro = 1.0,  cant = 1, vida = 1.5, rebote = 0.35, neon = true },
    { id = "pesado",   grav = 1900, vel = 0.75, giro = 0.35, cant = 1, vida = 1.2, rebote = 0.18 },
    { id = "flotante", grav = 240,  vel = 0.9,  giro = 0.5,  cant = 1, vida = 2.6, rebote = 0.5 },
    { id = "rafaga",   grav = 1100, vel = 1.25, giro = 1.3,  cant = 2, vida = 1.5, rebote = 0.35 },
    { id = "ninguno",  grav = 1100, vel = 1.0,  giro = 1.0,  cant = 0, vida = 1.5, rebote = 0.35 }
}

local fx = { parts = {}, retro = 0, retroV = 0, pulso = 0, ultLim = -10 }
local menuFx = { abierto = false, autoOn = false, sucio = false, ultCambio = 0 }

local function lerp(a, b, t) return a + (b - a) * t end

-- ==================== TEXTO ESTILIZADO ====================
local OFS8 = { {-1,0},{1,0},{0,-1},{0,1},{-1,-1},{1,-1},{-1,1},{1,1} }

local function partesColor(c)
    c = floor(c)
    if c < 0 then c = c + 4294967296 end
    return floor(c / 16777216) % 256, floor(c / 65536) % 256, floor(c / 256) % 256, c % 256
end

-- estilos: 1 clasico, 2 neon, 3 contorno, 4 glitch, 5 retro
local function textoFx(f, s, x, y, color, estilo)
    local ca, cr, cg, cb = partesColor(color)
    local u = K
    if estilo == 2 then
        for _, o in ipairs(OFS8) do
            renderFontDrawText(f, s, x + o[1] * 2.2 * u, y + o[2] * 2.2 * u, col(ca * 0.10, cr, cg, cb))
        end
        for _, o in ipairs(OFS8) do
            renderFontDrawText(f, s, x + o[1] * 1.1 * u, y + o[2] * 1.1 * u, col(ca * 0.22, cr, cg, cb))
        end
        renderFontDrawText(f, s, x, y, col(ca, min(255, cr + 70), min(255, cg + 70), min(255, cb + 70)))
    elseif estilo == 3 then
        for _, o in ipairs(OFS8) do
            renderFontDrawText(f, s, x + o[1] * 1.3 * u, y + o[2] * 1.3 * u, col(ca, 0, 0, 0))
        end
        renderFontDrawText(f, s, x, y, color)
    elseif estilo == 4 then
        local t = floor(os.clock() * 9)
        local h = sin(t * 12.9898) * 43758.5453
        h = h - floor(h)
        local sal = (h > 0.82) and (h * 6 - 4) or 0     -- sacudida ocasional
        local d = (1.4 + abs(sal)) * u
        renderFontDrawText(f, s, x - d + sal * u, y, col(ca * 0.75, 0, 255, 255))
        renderFontDrawText(f, s, x + d + sal * u, y, col(ca * 0.75, 255, 30, 60))
        renderFontDrawText(f, s, x + sal * u, y, color)
    elseif estilo == 5 then
        for i = 3, 1, -1 do
            renderFontDrawText(f, s, x + i * u, y + i * u, col(ca * (0.9 - i * 0.18), cr * 0.25, cg * 0.25, cb * 0.25))
        end
        renderFontDrawText(f, s, x, y, color)
    else
        renderFontDrawText(f, s, x + 1, y + 1, col(ca, 0, 0, 0))
        renderFontDrawText(f, s, x, y, color)
    end
end

-- animaciones de RECARGANDO: 1 clasica (se queda en el bloque original), 2 ola, 3 maquina de escribir, 4 glitch, 5 segmentos
-- devuelve true si la dibujo (false = usar la clasica)
local function recargaFx(f, base, xDer, yTxt, a, escala, ahora, anim, estilo)
    if anim == 1 then return false end
    local wTotal = renderGetFontDrawTextLength(f, base .. "...")
    local xT = xDer - wTotal
    local n = #base
    if anim == 2 then
        for i = 1, n do
            local pre = (i > 1) and renderGetFontDrawTextLength(f, base:sub(1, i - 1)) or 0
            local fase = ahora * 7 - i * 0.55
            local dy = sin(fase) * 4 * escala
            local br = 0.5 + 0.5 * sin(fase)
            local c = col(a, lerp(255, 255, br), lerp(170, 235, br), lerp(40, 160, br))
            textoFx(f, base:sub(i, i), xT + pre, yTxt + dy, c, estilo)
        end
    elseif anim == 3 then
        local ciclo = n + 8
        local k = floor(ahora * 14) % ciclo
        local vis = min(n, k)
        local txt = base:sub(1, vis)
        local cur = (floor(ahora * 3) % 2 == 0) and "_" or " "
        textoFx(f, txt .. cur, xT, yTxt, col(a, 120, 255, 140), estilo)
    elseif anim == 4 then
        local t = floor(ahora * 10)
        local h = sin(t * 78.233) * 43758.5453
        h = h - floor(h)
        local txt = base
        if h > 0.7 then
            local pos = floor(h * 997) % n + 1
            local r = string.char(65 + floor(h * 7919) % 26)
            txt = base:sub(1, pos - 1) .. r .. base:sub(pos + 1)
        end
        local sal = (h > 0.85) and (h * 8 - 6) or 0
        textoFx(f, txt, xT + sal, yTxt, col(a * (0.7 + 0.3 * sin(ahora * 20)), 255, 190, 60), 4)
    elseif anim == 5 then
        textoFx(f, base, xDer - renderGetFontDrawTextLength(f, base), yTxt, col(a, 255, 190, 60), estilo)
        local segs = 10
        local prog = (ahora * 0.9) % 1.0
        local lleno = floor(prog * segs + 0.5)
        local sw = (wTotal - (segs - 1) * 2) / segs
        local yB = yTxt + 24 * escala
        for i = 1, segs do
            local on = (i <= lleno)
            renderDrawBox(xT + (i - 1) * (sw + 2), yB, sw, 4 * escala, on and col(a, 255, 190, 60) or col(a * 0.2, 200, 200, 200))
        end
    end
    return true
end

local function paletaFx()
    return PALETAS[config.ajustes.fxPaleta or 1] or PALETAS[1]
end

local function estiloCasquillo()
    return ESTILOS_CAS[config.ajustes.fxCasquillo or 1] or ESTILOS_CAS[1]
end

-- Anillo (mimgui: AddCircle con antialiasing; sin mimgui: segmentos de linea)
local function anilloFx(x, y, r, grosor, color)
    if r <= 0.5 then return end
    if USAR_MIMGUI and DL then
        P1.x, P1.y = x, y
        DL:AddCircle(P1, r, argbToImU32(color), 24, max(1, grosor))
    else
        local seg = 20
        for i = 0, seg - 1 do
            local a1 = (i / seg) * 6.2831853
            local a2 = ((i + 1) / seg) * 6.2831853
            renderDrawLine(x + cos(a1) * r, y + sin(a1) * r, x + cos(a2) * r, y + sin(a2) * r, max(1, grosor), color)
        end
    end
end

-- Lanza los efectos activos desde un punto del HUD (la bala que sale del cargador).
-- limitar = true: armas de fuego continuo (minigun, lanzallamas), una rafaga de efectos cada 70 ms como maximo.
local function lanzarEfectos(x, y, esc, ahora, limitar)
    local A = config.ajustes
    local P = fx.parts
    if #P > 240 then return end
    if limitar then
        if ahora - fx.ultLim < 0.07 then return end
        fx.ultLim = ahora
    end
    local I = max(0.3, min(2.0, A.fxIntensidad or 1.0))
    if A.fxRetroceso then fx.retro = 1; fx.retroV = 0 end
    if A.fxPulso then fx.pulso = 1 end

    if A.fxResplandor then
        P[#P + 1] = { t = "halo", x = x, y = y, nacido = ahora, vida = 0.34, esc = esc }
    end
    if A.fxDestello ~= false then
        P[#P + 1] = { t = "flash", x = x, y = y, nacido = ahora, vida = 0.12 }
    end
    if A.fxImpacto then
        P[#P + 1] = { t = "anillo", x = x, y = y, nacido = ahora, vida = 0.28, esc = esc }
        local nr = max(3, floor(5 * I + 0.5))
        local base = random() * 6.2831853
        for i = 1, nr do
            P[#P + 1] = { t = "rayo", x = x, y = y, ang = base + (i / nr) * 6.2831853, nacido = ahora, vida = 0.22, esc = esc }
        end
    end
    if A.fxChispas then
        for i = 1, max(2, floor(7 * I + 0.5)) do
            local ang = -1.5708 + (random() - 0.5) * 2.6
            local v = random(140, 420) * K
            P[#P + 1] = {
                t = "chispa", x = x, y = y, vx = cos(ang) * v - 40 * K, vy = sin(ang) * v,
                nacido = ahora, vida = 0.22 + random() * 0.28, esc = esc
            }
        end
    end
    if A.fxHumo then
        for i = 1, max(1, floor(2.5 * I + 0.5)) do
            P[#P + 1] = {
                t = "humo", x = x + (random() - 0.5) * 6 * esc, y = y - random() * 4 * esc,
                vx = (random() - 0.5) * 40 * K - 12 * K, vy = -random(30, 80) * K,
                nacido = ahora, vida = 0.9 + random() * 0.8,
                r0 = (2.5 + random() * 2) * esc, r1 = (13 + random() * 10) * esc, g = random(135, 190),
                ph = random() * 6.2831853, rot = (random() - 0.5) * 2.2, turb = 0.6 + random() * 0.8
            }
        end
    end
    if A.fxBrasas then
        for i = 1, max(2, floor(4 * I + 0.5)) do
            P[#P + 1] = {
                t = "brasa", x = x + (random() - 0.5) * 6 * esc, y = y,
                vx = (random() - 0.5) * 90 * K - 20 * K, vy = -random(40, 150) * K,
                nacido = ahora, vida = 0.7 + random() * 0.7, r = (1 + random() * 1.4) * esc, ph = random() * 6.2831853
            }
        end
    end
    if A.fxFragmentos then
        for i = 1, max(2, floor(4 * I + 0.5)) do
            P[#P + 1] = {
                t = "frag", x = x, y = y, vx = -random(60, 260) * K, vy = -random(120, 320) * K,
                ang = random() * 6.2831853, vang = (random() - 0.5) * 24,
                len = (3 + random() * 4) * esc, nacido = ahora, vida = 0.55 + random() * 0.3, esc = esc
            }
        end
    end
end

local function actualizarFx(ahora, dt)
    -- retroceso como muelle amortiguado: tiron rapido, vuelve con un ligero rebote
    local dtc = min(dt, 0.033)
    fx.retroV = fx.retroV + (-380 * fx.retro - 24 * fx.retroV) * dtc
    fx.retro = fx.retro + fx.retroV * dtc
    if abs(fx.retro) < 0.004 and abs(fx.retroV) < 0.05 then fx.retro, fx.retroV = 0, 0 end
    fx.pulso = fx.pulso * max(0, 1 - dt * 7)
    if fx.pulso < 0.01 then fx.pulso = 0 end
    local P = fx.parts
    if #P == 0 then return end
    local pal = paletaFx()
    local c1, c2 = pal.c1, pal.c2
    for k = #P, 1, -1 do
        local p = P[k]
        local edad = ahora - p.nacido
        if edad >= p.vida then
            P[k] = P[#P]
            P[#P] = nil
        else
            local u = edad / p.vida
            local t = p.t
            if t == "flash" then
                local uu = 1 - u
                circuloLleno(p.x, p.y, (5 + 7 * uu) * K, col(170 * uu, (c1[1] + c2[1]) * 0.5, (c1[2] + c2[2]) * 0.5, (c1[3] + c2[3]) * 0.5))
                circuloLleno(p.x, p.y, (2 + 3 * uu) * K, col(230 * uu, 255, 255, 235))
                local ln = (6 + 11 * uu) * K
                local cEst = col(150 * uu, c2[1], c2[2], c2[3])
                renderDrawLine(p.x - ln, p.y, p.x + ln, p.y, max(1, 1.2 * K), cEst)
                renderDrawLine(p.x, p.y - ln, p.x, p.y + ln, max(1, 1.2 * K), cEst)
            elseif t == "halo" then
                local uu = 1 - u
                local r = (9 + 17 * u) * p.esc
                local al = 110 * uu * uu
                circuloLleno(p.x, p.y, r * 1.8, col(al * 0.22, c1[1], c1[2], c1[3]))
                circuloLleno(p.x, p.y, r * 1.2, col(al * 0.32, c1[1], c1[2], c1[3]))
                circuloLleno(p.x, p.y, r * 0.65, col(al * 0.45, c2[1], c2[2], c2[3]))
            elseif t == "anillo" then
                local e = 1 - (1 - u) * (1 - u)
                local rr = (4 + 24 * e) * p.esc
                anilloFx(p.x, p.y, rr, 2 * p.esc * (1 - u), col(220 * (1 - u), c2[1], c2[2], c2[3]))
                anilloFx(p.x, p.y, rr * 0.62, 1, col(120 * (1 - u), c1[1], c1[2], c1[3]))
            elseif t == "rayo" then
                local e = 1 - (1 - u) * (1 - u)
                local ra = (5 + 24 * e) * p.esc
                local rb = ra + (2 + 9 * (1 - u)) * p.esc
                local dx, dy = cos(p.ang), sin(p.ang)
                renderDrawLine(p.x + dx * ra, p.y + dy * ra, p.x + dx * rb, p.y + dy * rb, max(1, 1.6 * p.esc), col(230 * (1 - u), c2[1], c2[2], c2[3]))
            elseif t == "chispa" then
                p.vy = p.vy + 700 * K * dt
                p.x = p.x + p.vx * dt
                p.y = p.y + p.vy * dt
                renderDrawLine(p.x, p.y, p.x - p.vx * 0.05, p.y - p.vy * 0.05, max(2, 3.6 * p.esc), col(60 * (1 - u), c1[1], c1[2], c1[3]))
                renderDrawLine(p.x, p.y, p.x - p.vx * 0.04, p.y - p.vy * 0.04, max(1, 1.6 * p.esc),
                    col(255 * (1 - u), lerp(c2[1], c1[1], u), lerp(c2[2], c1[2], u), lerp(c2[3], c1[3], u)))
            elseif t == "humo" then
                -- Movimiento: flota hacia arriba, se frena y se agita con turbulencia suave
                p.vx = p.vx * max(0, 1 - 1.3 * dt) + sin(ahora * 2.6 * p.turb + p.ph) * 22 * K * dt
                p.vy = p.vy * max(0, 1 - 0.9 * dt) - 14 * K * dt
                p.x = p.x + p.vx * dt
                p.y = p.y + p.vy * dt
                -- Crece rapido al inicio y luego se asienta
                local e = 1 - (1 - u) * (1 - u)
                local r = lerp(p.r0, p.r1, e)
                -- Se desvanece de forma suave (cae mas rapido al final)
                local al = 135 * (1 - u) * (1 - u) * min(1, edad / 0.07)
                -- Color: polvora oscura al nacer, gris claro al disiparse
                local gg = lerp(p.g * 0.62, min(232, p.g + 45), e)
                local cr, cg, cb = min(255, gg + 5), gg, min(255, gg + 8)
                -- Cada voluta son varias nubes pequeñas que giran y se deforman:
                -- asi el contorno deja de ser un circulo perfecto
                local ang0 = p.ph + p.rot * edad
                local nb = p.nb or 3
                for bl = 1, nb do
                    local ang = ang0 + bl * (6.2831853 / nb)
                    local off = r * (0.38 + 0.18 * sin(edad * 3.4 * p.turb + bl * 1.7 + p.ph))
                    local bx = p.x + cos(ang) * off
                    local by = p.y + sin(ang) * off * 0.78 - r * 0.12 * u
                    local br = r * (0.52 + 0.1 * bl * 0.5)
                    -- Capas concentricas: borde difuso y centro mas denso
                    circuloLleno(bx, by, br, col(al * 0.20, cr, cg, cb))
                    circuloLleno(bx, by, br * 0.68, col(al * 0.26, cr, cg, cb))
                    circuloLleno(bx, by, br * 0.38, col(al * 0.30, min(255, cr + 12), min(255, cg + 12), min(255, cb + 12)))
                end
            elseif t == "brasa" then
                p.vy = p.vy + 90 * K * dt
                p.vx = p.vx + sin(ahora * 9 + p.ph) * 60 * K * dt
                p.x = p.x + p.vx * dt
                p.y = p.y + p.vy * dt
                local al = 255 * (1 - u) * (0.65 + 0.35 * sin(ahora * 32 + p.ph))
                circuloLleno(p.x, p.y, p.r * 2.6, col(al * 0.18, c1[1], c1[2], c1[3]))
                circuloLleno(p.x, p.y, p.r, col(al, lerp(c2[1], c1[1], u), lerp(c2[2], c1[2], u), lerp(c2[3], c1[3], u)))
            elseif t == "frag" then
                p.vy = p.vy + 1000 * K * dt
                p.x = p.x + p.vx * dt
                p.y = p.y + p.vy * dt
                p.ang = p.ang + p.vang * dt
                local dx, dy = cos(p.ang) * p.len * 0.5, sin(p.ang) * p.len * 0.5
                renderDrawLine(p.x - dx, p.y - dy, p.x + dx, p.y + dy, max(1, 1.8 * p.esc), col(255 * (1 - u * u), 215, 220, 230))
            end
        end
    end
end

-- Un solo punto de entrada desde el frame (prueba automatica del menu + dibujo de particulas)
local function tickEfectosHud(ahora, dt)
    if menuFx.abierto and menuFx.autoOn and (ahora - (menuFx.ultAuto or 0)) > 0.4 then
        hud.probar = true
        menuFx.ultAuto = ahora
    end
    actualizarFx(ahora, dt)
end

local function dibujarBala(x, y, ang, escala, a, vacia, tint)
    local tr, tg, tb = 1, 1, 1
    if tint then tr, tg, tb = tint[1] / 255, tint[2] / 255, tint[3] / 255 end
    local largo, ancho = 26 * escala, 8.5 * escala
    local dx, dy = cos(ang), sin(ang)
    local bx, by = x - dx * largo * 0.5, y - dy * largo * 0.5
    local mx, my = x + dx * largo * 0.12, y + dy * largo * 0.12
    local tx, ty = x + dx * largo * 0.5, y + dy * largo * 0.5
    if vacia then
        renderDrawLine(bx, by, tx, ty, ancho, col(a * 0.30, 140, 140, 140))
        return
    end
    renderDrawLine(bx, by, mx, my, ancho, col(a, 245 * tr, 245 * tg, 245 * tb))
    renderDrawLine(mx, my, tx, ty, ancho * 0.82, col(a, 190 * tr, 190 * tg, 190 * tb))
    renderDrawLine(bx, by, bx + dx * 3.2 * escala, by + dy * 3.2 * escala, ancho * 1.15, col(a, 150 * tr, 150 * tg, 150 * tb))
end

local function dibujarIconoMunicion(tex, cfg, x, y, ang, esc, a, vacia, tint)
    if tex and cfg then
        local w = cfg.alto * cfg.ratio * esc
        local h = cfg.alto * esc
        local rot = (ang + 1.57079632679) * 57.2957795131
        local c
        if vacia then
            c = col(a * 0.30, 140, 140, 140)
        elseif tint then
            c = col(a, tint[1], tint[2], tint[3])
        else
            c = col(a, 255, 255, 255)
        end
        renderDrawTexture(tex, x - w * 0.5, y - h * 0.5, w, h, rot, c)
    else
        dibujarBala(x, y, ang, esc, a, vacia, tint)
    end
end

local function leerClip(arma, tam, total)
    if MP.android then
        local v = MP.ws and MP.ws.clip(playerPed, arma)
        if v and v >= 0 and v <= total and v <= tam then return v end
        return nil
    end
    if not (getCharPointer and getWeapontypeSlot and doesCharExist(playerPed)) then return nil end
    local ptr = getCharPointer(playerPed)
    if not ptr or ptr < 0x10000 then return nil end
    local slot = getWeapontypeSlot(arma)
    if not slot or slot < 0 or slot > 12 then return nil end
    local val = memory.getuint32(ptr + 0x5A0 + slot * 0x1C + 0x8, false)
    if val and val >= 0 and val <= tam and val <= total then return val end
    return nil
end

-- Cuantas balas hay ahora mismo en el cargador, sin compararlo con la tabla CLIP_ARMA
local function leerClipCrudo(arma, total)
    if MP.android then
        local v = MP.ws and MP.ws.clip(playerPed, arma)
        if v and v >= 0 and v <= total and v <= 1000 then return v end
        return nil
    end
    if not (getCharPointer and getWeapontypeSlot and doesCharExist(playerPed)) then return nil end
    local ptr = getCharPointer(playerPed)
    if not ptr or ptr < 0x10000 then return nil end
    local slot = getWeapontypeSlot(arma)
    if not slot or slot < 0 or slot > 12 then return nil end
    local val = memory.getuint32(ptr + 0x5A0 + slot * 0x1C + 0x8, false)
    if val and val >= 0 and val <= 1000 and val <= total then return val end
    return nil
end

local function actualizarHUDMunicion(sw, sh, ahora, dt, apuntando)
    if not config.ajustes.hudMunicion then return end

    local arma = getCurrentCharWeapon(playerPed)
    local tam = CLIP_ARMA[arma]
    if not tam then
        hud.arma = -1
        if #hud.volando > 0 then hud.volando = {} end
        return
    end

    local total = getAmmoInCharWeapon(playerPed, arma)
    local escala = (config.ajustes.hudEscala or 1.0) * K
    local tipo = TIPO_ARMA[arma]
    local esCohete = TIPOS_UNIDAD[tipo] == true
    local esFuego = TIPOS_PORCENTAJE[tipo] == true
    local esMinigun = (tipo == "minigun")

    if esCohete or esFuego or esMinigun then
        hud.maxMun[arma] = max(hud.maxMun[arma] or 0, total)
    end

    -- Tamano real del cargador: los valores de CLIP_ARMA son los del juego original y no coinciden si el weapon.dat
    -- esta modificado o cambia la habilidad con el arma (p. ej. M4 de 30 en vez de 50, pistola con silenciador de 7
    -- en vez de 17). Se aprende del propio juego: el cargador lleno es el mayor valor visto, y CLIP_ARMA solo se
    -- usa si no se puede leer la memoria.
    if not (esCohete or esFuego or esMinigun) then
        local crudo = leerClipCrudo(arma, total)
        if crudo then
            local visto = hud.clipMax[arma] or 0
            if crudo > visto then visto = crudo; hud.clipMax[arma] = visto end
            if visto >= 1 then tam = visto end
        elseif (hud.clipMax[arma] or 0) >= 1 then
            tam = hud.clipMax[arma]
        end
    end

    local refMini = max(config.ajustes.minigunMax or 500, hud.maxMun[arma] or 0)
    if esMinigun then tam = MINI_BALAS end
    local escBala = escala
    local n = esMinigun and tam or min(tam, MAX_ICONOS)
    local cfg = tipo and ICONOS[tipo]
    local tex = cfg and TEX[tipo]

    if arma ~= hud.arma then
        hud.arma = arma
        hud.totalPrev = total
        hud.simClip = min(tam, total)
        hud.simRecargaHasta = 0
        hud.llenasPrev = -1
        hud.ultimaActividad = ahora
        hud.volando = {}
    end

    local disparos = hud.totalPrev - total
    if disparos < 0 or disparos > 10 then disparos = 0 end
    hud.totalPrev = total
    local esPrueba = false
    if hud.probar then
        hud.probar = false
        if disparos <= 0 then disparos = 1; esPrueba = true end
    end

    local clip
    if esCohete or esFuego or esMinigun then
        clip = esMinigun and total or min(total, tam)
    else
        clip = leerClip(arma, tam, total)
    end

    if clip == nil then
        if disparos > 0 and not esPrueba then
            hud.simClip = max(0, hud.simClip - disparos)
            if hud.simClip == 0 and total > 0 then hud.simRecargaHasta = ahora + 1.5 end
        end
        if hud.simClip == 0 and total > 0 and hud.simRecargaHasta > 0 and ahora >= hud.simRecargaHasta then
            hud.simClip = min(tam, total)
            hud.simRecargaHasta = 0
        end
        clip = min(hud.simClip, total)
    end

    local llenas = (tam <= MAX_ICONOS) and min(clip, tam) or ceil(clip / tam * n)
    if esMinigun then llenas = ceil(min(1, total / refMini) * n) end
    llenas = max(0, min(n, llenas))

    local altoIcono = tex and (cfg.alto * escBala) or (26 * escBala)
    local paso = tex and (cfg.alto * cfg.ratio * escBala + 2 * escBala) or (19 * escBala)
    local xDer = sw - (config.ajustes.hudX or 50) * K
    local modoRetro = config.ajustes.fxRetrocesoModo or 1
    if config.ajustes.fxRetroceso and modoRetro ~= 2 then xDer = xDer - fx.retro * 7 * escala end
    local yBase = sh - (config.ajustes.hudAltura or 210) * K
    -- Retroceso vertical: el bloque salta hacia arriba con cada disparo y vuelve a su sitio
    if config.ajustes.fxRetroceso and modoRetro ~= 1 then yBase = yBase - fx.retro * 6 * escala end
    local yBalas = yBase - altoIcono * 0.5

    if disparos > 0 then
        hud.ultimaActividad = ahora
        local cantBase = (esFuego or esMinigun) and 0 or min(disparos, 5)
        local ej = EYECCION[tipo] or EYECCION.base
        local est = estiloCasquillo()
        for k = 1, cantBase do
            local idx = min(n, llenas + k)
            local xIco = xDer - (n - idx) * paso - paso * 0.5
            lanzarEfectos(xIco, yBalas, escala, ahora, false)
            for _ = 1, est.cant do
                hud.volando[#hud.volando + 1] = {
                    x = xIco, y = yBalas, sx = xIco, sy = yBalas,
                    vx = -random(40, 150) * K * ej[1] * est.vel, vy = -random(220, 340) * K * ej[2] * est.vel,
                    ang = -1.57079632679, vang = (random() - 0.5) * 20 * ej[3] * est.giro,
                    nacido = ahora, rebotes = 0, hist = {}, ult = ahora, est = est,
                    suelo = yBase + 48 * escala          -- por debajo del texto: ahi rebota
                }
            end
        end
        if cantBase == 0 then
            -- minigun / lanzallamas: no hay icono que se consuma, los efectos salen del borde del HUD
            lanzarEfectos(xDer - 12 * K, yBalas, escala, ahora, true)
        end
    end

    if false and esMinigun and hud.llenasPrev >= 0 and llenas < hud.llenasPrev then
        hud.ultimaActividad = ahora
        for i = llenas + 1, min(hud.llenasPrev, llenas + 3) do
            local xIco = xDer - (n - i) * paso - paso * 0.5
            hud.volando[#hud.volando + 1] = {
                x = xIco, y = yBalas,
                vx = -random(30, 120), vy = -random(160, 280),
                ang = -1.57079632679, vang = (random() - 0.5) * 20,
                nacido = ahora
            }
        end
    end

    if llenas > hud.llenasPrev then
        local desde = (hud.llenasPrev < 0) and 0 or hud.llenasPrev
        for i = desde + 1, llenas do hud.anim[i] = ahora + (i - desde - 1) * 0.03 end
    end
    hud.llenasPrev = llenas

    if apuntando then hud.ultimaActividad = ahora end
    local objetivo = 0
    local mostrarPos = TXT.mostrar
    TXT.mostrar = false
    if TXT.previa or mostrarPos or ahora - hud.ultimaActividad < 4.0 then
        objetivo = 1
    elseif config.ajustes.hudSiempre then
        objetivo = 0.35
    end
    hud.alpha = hud.alpha + (objetivo - hud.alpha) * min(1, dt * 6)
    local af = hud.alpha

    if af > 0.02 and fuenteMunicion then
        local a = 255 * af
        if not esMinigun then
            renderDrawBox(xDer - n * paso - 4, yBase + 6 * escala, n * paso + 8, 1, col(a * 0.45, 200, 200, 200))
        end

        for i = 1, (esMinigun and 0 or n) do
            local x = xDer - (n - i) * paso - paso * 0.5
            if i <= llenas then
                local t = ahora - (hud.anim[i] or (ahora - 1))
                if t >= 0 then
                    local k = max(0, min(1, t / 0.15))
                    if config.ajustes.fxRecarga and t < 0.32 then
                        local gl = 1 - t / 0.32
                        local palR = paletaFx()
                        circuloLleno(x, yBalas, 13 * escBala, col(a * 0.20 * gl, palR.c1[1], palR.c1[2], palR.c1[3]))
                        circuloLleno(x, yBalas, 7 * escBala, col(a * 0.30 * gl, palR.c2[1], palR.c2[2], palR.c2[3]))
                    end
                    dibujarIconoMunicion(tex, cfg, x, yBalas - (1 - k) * 6, -1.57079632679, escBala * k, a, false)
                end
            else
                dibujarIconoMunicion(tex, cfg, x, yBalas, -1.57079632679, escBala, a, true)
            end
        end

        local pct = 0
        local bajo
        if esFuego then
            local maxF = max(arma == 37 and (config.ajustes.combustibleMax or 500) or 1, hud.maxMun[arma] or 0)
            pct = max(0, min(100, floor(total / maxF * 100 + 0.5)))
            bajo = (pct <= 25)
        elseif esMinigun then
            bajo = (total <= refMini * 0.2)
        elseif esCohete then
            bajo = (total <= 1)
        else
            bajo = (total <= 0) or (clip <= max(1, floor(tam * 0.25)))
        end

        local yTxt = yBase + 14 * escala
        local pulsoTxt = (config.ajustes.fxPulso and fx.pulso > 0.02) and fx.pulso or 0
        if pulsoTxt > 0 then yTxt = yTxt - pulsoTxt * 2.5 * escala end
        local parpadeo = 0.55 + 0.45 * sin(ahora * 8)
        local cBlanco = bajo and col(a * parpadeo, 255, 90, 90) or col(a, 255, 255, 255)
        if pulsoTxt > 0 and not bajo then
            local palP = paletaFx()
            cBlanco = col(a, lerp(255, palP.c1[1], pulsoTxt * 0.9), lerp(255, palP.c1[2], pulsoTxt * 0.9), lerp(255, palP.c1[3], pulsoTxt * 0.9))
        end
        local cGris = col(a * 0.75, 170, 170, 170)
        local estTxt = config.ajustes.txtEstilo or 1
        local animRec = config.ajustes.txtRecarga or 1

        -- fuente elegida y "pop" al disparar (la fuente se restaura al terminar el bloque)
        local idxF = config.ajustes.txtFuente or 0
        TXT.font = (idxF > 0 and TXT.fuentes[idxF]) and TXT.fuentes[idxF].ptr or nil
        if TXT.prevClip ~= nil and clip < TXT.prevClip then TXT.pop = 1 end
        TXT.prevClip = clip
        TXT.pop = max(0, TXT.pop - dt * 5)
        TXT.mult = (config.ajustes.txtPop ~= false) and (1 + 0.22 * TXT.pop) or 1.0
        local yPop = yTxt - (TXT.mult - 1) * 10 * escala

        if esFuego then
            local txt = pct .. "%"
            local wt = renderGetFontDrawTextLength(fuenteMunicion, txt)
            local xPrimero = xDer - (n - 1) * paso - paso * 0.5
            local anchoRef = cfg and (cfg.alto * cfg.ratio * escala) or (19 * escala)
            local xTxt = xPrimero - anchoRef * 0.5 - 8 * escala - wt
            local yTxtF = yBalas - 9
            textoFx(fuenteMunicion, txt, xTxt, yTxtF, cBlanco, estTxt)
        elseif (esCohete or esMinigun) and total > 0 then
            local txt = tostring(total)
            local wt = renderGetFontDrawTextLength(fuenteMunicion, txt)
            textoFx(fuenteMunicion, txt, xDer - wt, yPop, cBlanco, estTxt)
        elseif (total > 0 and clip == 0) or (TXT.previa and not esFuego and not esCohete and not esMinigun) then
            TXT.mult = 1.0
            local base = T("recargando")
            if not recargaFx(fuenteMunicion, base, xDer, yTxt, a, escala, ahora, animRec, estTxt) then
                local wTotal = renderGetFontDrawTextLength(fuenteMunicion, base .. "...")
                local xT = xDer - wTotal
                local puntos = string.rep(".", floor(ahora * 3) % 4)
                local pulso = 0.65 + 0.35 * sin(ahora * 5)
                local cAmbar = col(a * pulso, 255, 190, 60)
                textoFx(fuenteMunicion, base .. puntos, xT, yTxt, cAmbar, estTxt)
                local yB = yTxt + 22 * escala
                local seg = wTotal * 0.3
                local pos = (sin(ahora * 4) * 0.5 + 0.5) * (wTotal - seg)
                renderDrawBox(xT, yB, wTotal, 2, col(a * 0.25, 200, 200, 200))
                renderDrawBox(xT + pos, yB, seg, 2, col(a, 255, 190, 60))
            end
        elseif total <= 0 then
            local txt = T("sinMunicion")
            local wt = renderGetFontDrawTextLength(fuenteMunicion, txt)
            textoFx(fuenteMunicion, txt, xDer - wt, yTxt, cBlanco, estTxt)
        else
            local sClip = tostring(clip)
            local sRes = " / " .. tostring(max(0, total - clip))
            local wRes = renderGetFontDrawTextLength(fuenteMunicion, sRes)
            local wClip = renderGetFontDrawTextLength(fuenteMunicion, sClip)
            local xRes = xDer - wRes
            local xClip = xRes - wClip
            textoFx(fuenteMunicion, sRes, xRes, yPop, cGris, estTxt == 4 and 1 or estTxt)
            textoFx(fuenteMunicion, sClip, xClip, yPop, cBlanco, estTxt)
        end
        TXT.font = nil
        TXT.mult = 1.0
    end

    local estela = (config.ajustes.fxEstela ~= false)
    local palFx = paletaFx()
    for k = #hud.volando, 1, -1 do
        local b = hud.volando[k]
        local est = b.est or ESTILOS_CAS[1]
        b.vy = b.vy + (est.grav or 1100) * K * dt
        b.x = b.x + b.vx * dt
        b.y = b.y + b.vy * dt
        b.ang = b.ang + b.vang * dt

        -- rebote en el "suelo": uno corto con sonido; despues se queda deslizando
        local modoC = config.ajustes.casModo or 1
        if modoC ~= 3 and b.y >= b.suelo and b.vy > 0 then
            b.y = b.suelo
            local limRebotes = (modoC == 2) and 5 or 1
            if b.rebotes < limRebotes then
                if modoC == 2 then
                    b.vy = -b.vy * max(est.rebote or 0.35, 0.55)
                    b.vx = b.vx * 0.85
                    b.vang = b.vang * 0.8
                else
                    b.vy = -b.vy * (est.rebote or 0.35)
                    b.vx = b.vx * 0.55
                    b.vang = b.vang * 0.5
                end
                if b.rebotes < 2 then reproducirCasquillo() end
            else
                b.vy = 0
                b.vx = b.vx * 0.92
                b.vang = b.vang * 0.9
            end
            b.rebotes = b.rebotes + 1
        end

        local vida = (est.vida or 1.5) * (modoC == 2 and 1.8 or 1)
        local edad = ahora - b.nacido
        local fuera = false
        if modoC == 3 then
            local _, shC = getScreenResolution()
            fuera = (b.y > shC + 40) or edad > 4
        end
        if (modoC ~= 3 and edad > vida) or fuera then
            table.remove(hud.volando, k)
        else
            local fa = (modoC == 3 or edad < vida * 0.6) and 1 or max(0, 1 - (edad - vida * 0.6) / (vida * 0.4))
            local rapido = (b.vx * b.vx + b.vy * b.vy) > (80 * K) * (80 * K)
            local tint = est.tint
            if est.neon then tint = palFx.c1 end

            -- humo del casquillo: hilo corto de humo mientras se mueve rapido
            if config.ajustes.fxHumoCas and rapido and ahora - (b.ultH or 0) > 0.07 and #fx.parts < 180 then
                b.ultH = ahora
                fx.parts[#fx.parts + 1] = {
                    t = "humo", x = b.x, y = b.y, vx = (random() - 0.5) * 20 * K, vy = -random(10, 35) * K,
                    nacido = ahora, vida = 0.45 + random() * 0.35,
                    r0 = (1.2 + random()) * escBala, r1 = (5 + random() * 3) * escBala, g = random(150, 205),
                    ph = random() * 6.2831853, rot = (random() - 0.5) * 2.5, turb = 0.6 + random() * 0.8, nb = 2
                }
            end

            -- estela: copias fantasma en posiciones anteriores (solo mientras se mueve rapido)
            if rapido and estela then
                if ahora - b.ult >= 0.025 then
                    b.hist[#b.hist + 1] = { b.x, b.y, b.ang }
                    if #b.hist > 4 then table.remove(b.hist, 1) end
                    b.ult = ahora
                end
                local nh = #b.hist
                for i = 1, nh do
                    local h = b.hist[i]
                    dibujarIconoMunicion(tex, cfg, h[1], h[2], h[3], escBala, 255 * fa * 0.16 * (i / nh), false, tint)
                end
            elseif #b.hist > 0 then
                b.hist = {}
            end

            -- estilo neon: halo del color de la paleta detras del casquillo
            if est.neon then
                circuloLleno(b.x, b.y, 15 * escBala, col(26 * fa, tint[1], tint[2], tint[3]))
                circuloLleno(b.x, b.y, 9 * escBala, col(46 * fa, tint[1], tint[2], tint[3]))
            end

            dibujarIconoMunicion(tex, cfg, b.x, b.y, b.ang, escBala, 255 * fa, false, tint)
        end
    end
end

-- ==================== HUD DE CHALECO ====================
local CHALECO_RANGOS = {
    { {0.08, 0.30}, {0.70, 0.92} }, { {0.08, 0.30}, {0.70, 0.92} }, { {0.08, 0.34}, {0.66, 0.92} },
    { {0.14, 0.40}, {0.60, 0.86} }, { {0.14, 0.46}, {0.54, 0.86} }, { {0.14, 0.86} },
    { {0.12, 0.88} }, { {0.06, 0.94} }, { {0.06, 0.94} }, { {0.06, 0.94} }, { {0.06, 0.94} },
    { {0.08, 0.92} }, { {0.10, 0.90} }, { {0.16, 0.84} }
}
local GRIETA_1 = { {0.62, 0.40}, {0.72, 0.56}, {0.64, 0.70} }
local GRIETA_2 = { {0.30, 0.50}, {0.40, 0.64}, {0.34, 0.80} }
local GRIETA_3 = { {0.50, 0.30}, {0.46, 0.55}, {0.56, 0.72}, {0.50, 0.92} }
local MARCAS_BARRA = { 0.25, 0.5, 0.75 }

local CH_COLORES = { { 110, 185, 255 }, { 215, 238, 255 }, { 255, 255, 255 } }
local chaleco = {
    prev = -1, alpha = 0, ultimaActividad = -10, frag = {},
    shake = 0, flash = 0, ondas = {}, rotura = -10, ghost = nil, ghostT = -10, probar = nil
}

local function dibujarChalecoTextura(cx, cy, esc, a, pct)
    local tex, cfg = TEX.chaleco, ICONOS.chaleco
    if not tex then return false end
    local w, h = cfg.alto * cfg.ratio * esc, cfg.alto * esc
    local x, y = cx - w * 0.5, cy - h * 0.5
    local r, g, b = 110, 185, 255
    if pct <= 30 then r, g, b = 255, 80, 80 elseif pct <= 60 then r, g, b = 255, 200, 70 end
    local vacio = (pct <= 0)
    local cBorde = vacio and col(a * 0.30, 140, 140, 140) or col(a, r, g, b)
    local d = max(1.5, 2.0 * esc)
    local dd = d * 0.7071
    local dirs = { {d, 0}, {-d, 0}, {0, d}, {0, -d}, {dd, dd}, {-dd, dd}, {dd, -dd}, {-dd, -dd} }
    for i = 1, #dirs do
        renderDrawTexture(tex, x + dirs[i][1], y + dirs[i][2], w, h, 0, cBorde)
    end
    renderDrawTexture(tex, x, y, w, h, 0, vacio and col(a * 0.45, 255, 255, 255) or col(a, 255, 255, 255))
    return true
end

local function dibujarGrieta(pts, x0, y0, W, H, grosor, color)
    for i = 1, #pts - 1 do
        renderDrawLine(x0 + pts[i][1] * W, y0 + pts[i][2] * H, x0 + pts[i + 1][1] * W, y0 + pts[i + 1][2] * H, grosor, color)
    end
end

local function dibujarChaleco(cx, cy, esc, a, pct)
    if dibujarChalecoTextura(cx, cy, esc, a, pct) then return end
    local W, H = 38 * esc, 44 * esc
    local nF = #CHALECO_RANGOS
    local hf = H / nF
    local x0, y0 = cx - W * 0.5, cy - H * 0.5
    local llenas = ceil(pct / 100 * nF)
    local r, g, b = 110, 185, 255
    if pct <= 30 then r, g, b = 255, 80, 80 elseif pct <= 60 then r, g, b = 255, 200, 70 end

    local cSombra = col(a, 0, 0, 0)
    local cViva = col(a, r, g, b)
    local cVacia = col(a * 0.22, 150, 150, 150)

    for f = 1, nF do
        local yy = floor(y0 + (f - 1) * hf)
        local rangos = CHALECO_RANGOS[f]
        for i = 1, #rangos do
            local rg = rangos[i]
            renderDrawBox(floor(x0 + rg[1] * W) - 1, yy - 1, floor((rg[2] - rg[1]) * W) + 2, ceil(hf) + 2, cSombra)
        end
    end

    for f = 1, nF do
        local yy = floor(y0 + (f - 1) * hf)
        local c = (f > nF - llenas) and cViva or cVacia
        local rangos = CHALECO_RANGOS[f]
        for i = 1, #rangos do
            local rg = rangos[i]
            renderDrawBox(floor(x0 + rg[1] * W), yy, floor((rg[2] - rg[1]) * W), ceil(hf), c)
        end
    end

    if pct > 0 then
        local cg = col(a, 20, 20, 20)
        local gro = max(1, 1.5 * esc)
        if pct < 80 then dibujarGrieta(GRIETA_1, x0, y0, W, H, gro, cg) end
        if pct < 55 then dibujarGrieta(GRIETA_2, x0, y0, W, H, gro, cg) end
        if pct < 30 then dibujarGrieta(GRIETA_3, x0, y0, W, H, gro, cg) end
    end
end

local BARRA_ESTILOS = { "clasico", "segmentos", "neon", "linea", "degradado" }

-- relleno entre dos porcentajes de una barra vertical (0 = abajo, 100 = arriba)
local function rellenoBarra(bx, by, bw, bh, p0, p1, color)
    if p1 <= p0 then return end
    renderDrawBox(bx, by + bh * (1 - p1 / 100), bw, bh * (p1 - p0) / 100, color)
end

local function dibujarBarraChaleco(estilo, bx, by, bw, bh, arm, ghost, a, ahora, esc, A, r, g, b)
    -- latido: halo rojo detras de la barra cuando queda poca armadura
    if A.chBarraLatido == true and arm > 0 and arm <= 30 then
        local pu = 0.5 + 0.5 * sin(ahora * 7)
        renderDrawBox(bx - 3, by - 3, bw + 6, bh + 6, col(a * 0.20 * pu, 255, 60, 60))
    end
    local cGhost = col(a * 0.95, 255, 160, 150)

    if estilo == 2 then
        -- segmentos: 10 bloques con hueco; el ultimo se rellena parcialmente
        local n = 10
        local gap = max(2, 2 * esc)
        local sh = (bh - (n - 1) * gap) / n
        for i = 1, n do
            local sy = by + bh - i * sh - (i - 1) * gap
            local p0 = (i - 1) * 10
            renderDrawBox(bx - 1, sy - 1, bw + 2, sh + 2, col(a, 0, 0, 0))
            renderDrawBox(bx, sy, bw, sh, col(a * 0.22, 160, 160, 160))
            local fg = max(0, min(1, (ghost - p0) / 10))
            local fa = max(0, min(1, (arm - p0) / 10))
            if fg > fa then renderDrawBox(bx, sy + sh * (1 - fg), bw, sh * (fg - fa), cGhost) end
            if fa > 0 then renderDrawBox(bx, sy + sh * (1 - fa), bw, sh * fa, col(a, r, g, b)) end
        end
    elseif estilo == 3 then
        -- neon: marco fino de color y resplandor suave alrededor del relleno
        local gl = 0.8 + 0.2 * sin(ahora * 4)
        if arm > 0 then
            local ty = by + bh * (1 - arm / 100)
            renderDrawBox(bx - 3, ty, bw + 6, bh * arm / 100, col(a * 0.14 * gl, r, g, b))
            renderDrawBox(bx - 1.5, ty, bw + 3, bh * arm / 100, col(a * 0.22 * gl, r, g, b))
        end
        renderDrawBox(bx, by, bw, bh, col(a * 0.45, 0, 0, 0))
        rellenoBarra(bx, by, bw, bh, arm, ghost, cGhost)
        rellenoBarra(bx, by, bw, bh, 0, arm, col(a * 0.95, r, g, b))
        local cm = col(a * 0.9, r, g, b)
        renderDrawBox(bx - 1, by - 1, bw + 2, 1, cm)
        renderDrawBox(bx - 1, by + bh, bw + 2, 1, cm)
        renderDrawBox(bx - 1, by - 1, 1, bh + 2, cm)
        renderDrawBox(bx + bw, by - 1, 1, bh + 2, cm)
    elseif estilo == 4 then
        -- linea fina: riel delgado con una marca blanca en el nivel actual
        local lw = max(2, 2 * esc)
        local lx = bx + (bw - lw) * 0.5
        renderDrawBox(lx, by, lw, bh, col(a * 0.35, 255, 255, 255))
        rellenoBarra(lx, by, lw, bh, arm, ghost, cGhost)
        rellenoBarra(lx, by, lw, bh, 0, arm, col(a, r, g, b))
        if arm > 0 then
            renderDrawBox(bx - 1, by + bh * (1 - arm / 100) - 1, bw + 2, 2, col(a, 255, 255, 255))
        end
    elseif estilo == 5 then
        -- degradado: oscuro abajo, claro arriba, en 20 franjas
        renderDrawBox(bx - 1, by - 1, bw + 2, bh + 2, col(a, 0, 0, 0))
        renderDrawBox(bx, by, bw, bh, col(a * 0.25, 160, 160, 160))
        rellenoBarra(bx, by, bw, bh, arm, ghost, cGhost)
        local n = 20
        for k = 1, n do
            local p0, p1 = (k - 1) * 100 / n, k * 100 / n
            if arm > p0 then
                local f = 0.4 + 0.6 * (k / n)
                rellenoBarra(bx, by, bw, bh, p0, min(arm, p1), col(a, r * f, g * f, b * f))
            end
        end
    else
        -- clasica
        renderDrawBox(bx - 1, by - 1, bw + 2, bh + 2, col(a, 0, 0, 0))
        renderDrawBox(bx, by, bw, bh, col(a * 0.25, 160, 160, 160))
        rellenoBarra(bx, by, bw, bh, arm, ghost, cGhost)
        rellenoBarra(bx, by, bw, bh, 0, arm, col(a, r, g, b))
        local cDiv = col(a * 0.8, 0, 0, 0)
        for i = 1, #MARCAS_BARRA do
            renderDrawBox(bx, by + bh * (1 - MARCAS_BARRA[i]), bw, 1, cDiv)
        end
    end

    -- efectos (valen para cualquier estilo)
    if arm > 0 then
        local ty = by + bh * (1 - arm / 100)
        local fondo = by + bh
        if A.chBarraBrillo == true then
            -- barrido: franja de luz que sube por la parte llena
            local bandH = max(3, bh * 0.1)
            local s = (ahora * 0.7) % 1
            local cB = fondo + bandH - s * (bh + 2 * bandH)
            for i = -1, 1 do
                local y0 = max(cB + i * bandH - bandH * 0.5, ty)
                local y1 = min(cB + i * bandH + bandH * 0.5, fondo)
                if y1 > y0 then
                    renderDrawBox(bx, y0, bw, y1 - y0, col(a * ((i == 0) and 0.55 or 0.22), 255, 255, 255))
                end
            end
        end
        if A.chBarraPunta == true then
            local fl = 0.75 + 0.25 * sin(ahora * 13)
            circuloLleno(bx + bw * 0.5, ty, bw * 1.7, col(a * 0.22 * fl, r, g, b))
            renderDrawBox(bx - 1, ty - 1, bw + 2, 2, col(a * fl, 255, 255, 255))
        end
    end
end

local function actualizarHUDChaleco(sw, sh, ahora, dt, apuntando)
    local A = config.ajustes
    if not A.chalecoHud then return end
    local arm = max(0, min(100, floor(getCharArmour(playerPed) + 0.5)))
    local demo = (menuFx.abierto and menuFx.demo) == true
    if demo then arm = floor(max(0, min(100, menuFx.demoPct or 70))) end
    if demo ~= chaleco.demoActivo then
        chaleco.demoActivo = demo
        chaleco.prev = arm
        chaleco.ghost = arm
    end
    if demo then
        chaleco.prev = arm
        chaleco.ultimaActividad = ahora
    end
    local esc = (A.chalecoEscala or 1.0) * K
    local W, H = 38 * esc, 44 * esc
    if TEX.chaleco then
        W, H = ICONOS.chaleco.alto * ICONOS.chaleco.ratio * esc, ICONOS.chaleco.alto * esc
    end
    local cx = sw - (A.chalecoX or 50) * K - W * 0.5
    local cy = sh - (A.chalecoAltura or 290) * K - H * 0.5

    if chaleco.prev < 0 then chaleco.prev = arm end
    if chaleco.ghost == nil then chaleco.ghost = arm end

    local golpe, rotura = nil, false
    if arm ~= chaleco.prev then
        chaleco.ultimaActividad = ahora
        if arm < chaleco.prev then
            golpe = chaleco.prev - arm
            rotura = (arm <= 0)
            for _ = 1, min(8, ceil((chaleco.prev - arm) / 3)) do
                chaleco.frag[#chaleco.frag + 1] = {
                    x = cx + (random() - 0.5) * W * 0.8, y = cy + (random() - 0.5) * H * 0.6,
                    vx = (random() - 0.5) * 140 * K, vy = -random(60, 180) * K,
                    lado = random(3, 6) * K, nacido = ahora
                }
            end
        end
        chaleco.prev = arm
    end

    -- golpe de prueba desde el menu (BALAS)
    if chaleco.probar then
        golpe = golpe or 25
        if chaleco.probar == "rotura" then rotura = true end
        chaleco.probar = nil
        chaleco.ultimaActividad = ahora
    end

    if golpe then
        if A.chDestello ~= false then chaleco.flash = 1 end
        if A.chSacudida ~= false then chaleco.shake = min(1, 0.35 + golpe / 30) end
        if A.chOnda ~= false and #chaleco.ondas < 5 then
            chaleco.ondas[#chaleco.ondas + 1] = { nacido = ahora, grande = rotura }
        end
        if A.chBarraDano ~= false then
            chaleco.ghost = min(100, max(chaleco.ghost, arm + golpe))
            chaleco.ghostT = ahora
        end
    end

    if rotura and A.chRotura ~= false then
        chaleco.rotura = ahora
        chaleco.flash = 1
        local I = max(0.3, min(2.0, A.fxIntensidad or 1.0))
        for _ = 1, max(10, floor(22 * I + 0.5)) do
            chaleco.frag[#chaleco.frag + 1] = {
                x = cx + (random() - 0.5) * W * 0.7, y = cy + (random() - 0.5) * H * 0.7,
                vx = (random() - 0.5) * 460 * K, vy = -random(80, 320) * K,
                lado = random(3, 8) * K, nacido = ahora, vida = 1.1, c = CH_COLORES[random(1, #CH_COLORES)]
            }
        end
    end

    -- la barra de "dano reciente" se vacia despues de una pausa corta; si subes armadura, se iguala
    if chaleco.ghost > arm then
        if A.chBarraDano == false then
            chaleco.ghost = arm
        elseif ahora - chaleco.ghostT > 0.35 then
            chaleco.ghost = max(arm, chaleco.ghost - 60 * dt)
        end
    else
        chaleco.ghost = arm
    end
    chaleco.shake = chaleco.shake * max(0, 1 - dt * 5)
    chaleco.flash = max(0, chaleco.flash - dt * 4)
    if chaleco.shake > 0.02 then
        cx = cx + sin(ahora * 70) * 4 * chaleco.shake * esc
        cy = cy + cos(ahora * 55) * 2.5 * chaleco.shake * esc
    end

    if apuntando and arm > 0 then chaleco.ultimaActividad = ahora end
    local objetivo = 0
    if ahora - chaleco.ultimaActividad < 4.0 then
        objetivo = 1
    elseif A.chalecoSiempre and arm > 0 then
        objetivo = 0.35
    end
    chaleco.alpha = chaleco.alpha + (objetivo - chaleco.alpha) * min(1, dt * 6)
    local af = chaleco.alpha

    if af > 0.02 and fuenteMunicion then
        local a = 255 * af
        if arm > 0 and arm <= 25 then a = a * (0.65 + 0.35 * sin(ahora * 8)) end
        dibujarChaleco(cx, cy, esc, a, arm)

        -- destello de impacto: velo luminoso sobre el icono
        if chaleco.flash > 0.02 and A.chDestello ~= false then
            local fl = chaleco.flash * af
            local R = max(W, H) * 0.62
            circuloLleno(cx, cy, R, col(70 * fl, 255, 235, 235))
            circuloLleno(cx, cy, R * 0.65, col(90 * fl, 255, 255, 255))
        end

        -- rotura: las grietas aparecen de golpe sobre el icono y se desvanecen
        if A.chRotura ~= false and (ahora - chaleco.rotura) < 0.6 then
            local u = (ahora - chaleco.rotura) / 0.6
            local x0, y0 = cx - W * 0.5, cy - H * 0.5
            local cg = col(255 * (1 - u) * af, 255, 255, 255)
            local gro = max(1.5, 2 * esc)
            dibujarGrieta(GRIETA_1, x0, y0, W, H, gro, cg)
            dibujarGrieta(GRIETA_2, x0, y0, W, H, gro, cg)
            dibujarGrieta(GRIETA_3, x0, y0, W, H, gro, cg)
        end

        local anchoBarra = 0
        if A.chalecoBarra ~= false then
            local bw, bh = max(4, 5 * esc), H
            local bx = cx - W * 0.5 - 7 * esc - bw
            local by = cy - bh * 0.5
            local r, g, b = 110, 185, 255
            if arm <= 30 then r, g, b = 255, 80, 80 elseif arm <= 60 then r, g, b = 255, 200, 70 end
            dibujarBarraChaleco(A.chEstiloBarra or 1, bx, by, bw, bh, arm,
                (A.chBarraDano ~= false) and chaleco.ghost or arm, a, ahora, esc, A, r, g, b)
            anchoBarra = bw + 13 * esc
        end

        local txt = arm .. "%"
        local wt = renderGetFontDrawTextLength(fuenteMunicion, txt)
        local xT = cx - W * 0.5 - 8 * esc - anchoBarra - wt
        local cT = (arm <= 25) and col(a, 255, 90, 90) or col(a, 255, 255, 255)
        renderFontDrawText(fuenteMunicion, txt, xT + 1, cy - 8, col(a, 0, 0, 0))
        renderFontDrawText(fuenteMunicion, txt, xT, cy - 9, cT)
    end

    -- ondas de escudo: anillos azules que se expanden desde el chaleco
    for k = #chaleco.ondas, 1, -1 do
        local o = chaleco.ondas[k]
        local u = (ahora - o.nacido) / 0.45
        if u >= 1 then
            table.remove(chaleco.ondas, k)
        elseif af > 0.02 then
            local e = 1 - (1 - u) * (1 - u)
            local rr = max(W, H) * 0.5 + (10 + 34 * e) * esc * (o.grande and 1.6 or 1)
            anilloFx(cx, cy, rr, max(1, 2.2 * esc * (1 - u)), col(200 * (1 - u) * af, 110, 185, 255))
            anilloFx(cx, cy, rr * 0.82, 1, col(110 * (1 - u) * af, 215, 238, 255))
        end
    end

    for k = #chaleco.frag, 1, -1 do
        local fr = chaleco.frag[k]
        fr.vy = fr.vy + 900 * K * dt
        fr.x = fr.x + fr.vx * dt
        fr.y = fr.y + fr.vy * dt
        local vida = fr.vida or 0.9
        local edad = ahora - fr.nacido
        if edad > vida then
            table.remove(chaleco.frag, k)
        else
            local cr, cg2, cb = 110, 185, 255
            if fr.c then cr, cg2, cb = fr.c[1], fr.c[2], fr.c[3] end
            renderDrawBox(fr.x, fr.y, fr.lado, fr.lado, col(255 * (1 - edad / vida), cr, cg2, cb))
        end
    end
end

-- ==================== BARRA DE SPRINT ====================
local sprint = {
    max = 0, alpha = 0, ultimaActividad = -10,
    vPrev = nil, subiendoFrames = 0, topeT = 0,
    statStamina = -1, statFat = -1,
    shown = 1, agotado = false
}

local function leerSprint()
    if MP.android then return nil end
    if not (getCharPointer and doesCharExist(playerPed)) then return nil end
    local ptr = getCharPointer(playerPed)
    if not ptr or ptr < 0x10000 then return nil end
    local pd = memory.getuint32(ptr + 0x480, false)
    if not pd or pd < 0x10000 then return nil end
    local v = memory.getfloat(pd + SPRINT_OFFSET, false)
    if type(v) == "number" and v == v and v >= -500 and v <= 100000 then
        return v
    end
    return nil
end

local function actualizarCapacidadSprint(v, dt)
    local st = getFloatStat(22) or 0
    local ft = getFloatStat(21) or 0

    if sprint.statStamina < 0 or abs(st - sprint.statStamina) > 1.0 or abs(ft - sprint.statFat) > 2.0 then
        sprint.statStamina = st
        sprint.statFat = ft
        sprint.max = max(v, 100)
    end

    if v > sprint.max then sprint.max = v end

    if sprint.vPrev then
        local d = v - sprint.vPrev
        if d > 0.005 then
            sprint.subiendoFrames = sprint.subiendoFrames + 1
            sprint.topeT = 0
        elseif d < -0.005 then
            sprint.subiendoFrames = 0
            sprint.topeT = 0
        elseif sprint.subiendoFrames >= 15 and getCharSpeed(playerPed) < 0.2 and v > 50 then
            sprint.topeT = sprint.topeT + dt
            if sprint.topeT >= 0.5 then
                sprint.max = v
                sprint.subiendoFrames = 0
                sprint.topeT = 0
            end
        end
    end
    sprint.vPrev = v
end

local function actualizarBarraSprint(sw, sh, ahora, dt)
    if not config.ajustes.sprintHud then return end
    if isCharInAnyCar(playerPed) then
        sprint.alpha = 0
        return
    end

    local v = leerSprint()
    if not v then return end

    actualizarCapacidadSprint(v, dt)
    if sprint.max <= 0 then return end

    local vPos = max(0, v)
    local ratio = max(0, min(1, vPos / sprint.max))
    local gastando = (sprint.vPrev and (v < sprint.vPrev - 0.01))
                  or (isButtonPressed(playerHandle, 16) and getCharSpeed(playerPed) > 2.5)

    sprint.shown = sprint.shown + (ratio - sprint.shown) * min(1, dt * 12)

    if v <= 0.5 or ratio < 0.03 then
        sprint.agotado = true
    elseif ratio > 0.25 then
        sprint.agotado = false
    end

    if ratio < 0.995 or gastando then
        sprint.ultimaActividad = ahora
    end

    local objetivo = (ahora - sprint.ultimaActividad < 1.5) and 1 or 0
    sprint.alpha = sprint.alpha + (objetivo - sprint.alpha) * min(1, dt * 8)
    local af = sprint.alpha
    if af <= 0.02 then return end

    local esc = (config.ajustes.hudEscala or 1.0) * K
    local a = 255 * af
    local w, h = 150 * esc, max(3, floor(4 * esc))
    local x = sw - (config.ajustes.sprintX or 50) * K - w
    local y = sh - (config.ajustes.sprintAltura or 150) * K

    local r, g, b = 255, 255, 255
    local aRel = a * 0.9
    if sprint.agotado or ratio <= 0.2 then
        r, g, b = 230, 70, 70
        if sprint.agotado then aRel = a * (0.6 + 0.4 * sin(ahora * 10)) end
    end

    renderDrawBox(x - 1, y - 1, w + 2, h + 2, col(a * 0.35, 0, 0, 0))
    renderDrawBox(x, y, w, h, col(a * 0.28, 255, 255, 255))
    local wf = floor(w * sprint.shown + 0.5)
    if wf > 0 then renderDrawBox(x, y, wf, h, col(aRel, r, g, b)) end
end

-- ==================== CARTEL SELECTOR DE IDIOMA (COMPACTO Y DINAMICO) ====================
local function dibujarBannerSelectorIdioma(sw, sh)
    if not (fuente and fuenteMenu) then return end

    local tit = "SELECT LANGUAGE / SELECCIONAR IDIOMA"
    local selEs = (config.ajustes.idioma == "es")

    local opt1 = (selEs and "> " or "  ") .. "[1] ESPANOL"
    local opt2 = (not selEs and "> " or "  ") .. "[2] ENGLISH"
    local opt3 = "[ESC] CERRAR"

    local wTit = renderGetFontDrawTextLength(fuente, tit)
    local wOpt1 = renderGetFontDrawTextLength(fuenteMenu, opt1) + 14
    local wOpt2 = renderGetFontDrawTextLength(fuenteMenu, opt2) + 14
    local wOpt3 = renderGetFontDrawTextLength(fuenteMenu, opt3) + 14

    local espaciado = 10
    local anchoBotones = wOpt1 + wOpt2 + wOpt3 + (espaciado * 2)
    local ancho = max(wTit + 36, anchoBotones + 36)
    local alto = 62
    local x = floor((sw - ancho) * 0.5)
    local y = floor(sh * 0.12)

    renderDrawBox(x - 2, y - 2, ancho + 4, alto + 4, col(240, 0, 0, 0))
    renderDrawBox(x, y, ancho, alto, col(235, 18, 18, 18))
    renderDrawBox(x, y, ancho, 2, col(255, 110, 185, 255))

    renderFontDrawText(fuente, tit, x + floor((ancho - wTit) * 0.5), y + 9, 0xFFFFFFFF)

    local btnStartX = x + floor((ancho - anchoBotones) * 0.5)
    local btnY = y + 33

    renderDrawBox(btnStartX, btnY, wOpt1, 18, selEs and col(70, 85, 255, 85) or col(35, 255, 255, 255))
    renderFontDrawText(fuenteMenu, opt1, btnStartX + 7, btnY + 3, selEs and 0xFF55FF55 or 0xFFAAAAAA)

    local btn2X = btnStartX + wOpt1 + espaciado
    renderDrawBox(btn2X, btnY, wOpt2, 18, not selEs and col(70, 85, 255, 255) or col(35, 255, 255, 255))
    renderFontDrawText(fuenteMenu, opt2, btn2X + 7, btnY + 3, not selEs and 0xFF55FFFF or 0xFFAAAAAA)

    local btn3X = btn2X + wOpt2 + espaciado
    renderDrawBox(btn3X, btnY, wOpt3, 18, col(25, 255, 255, 255))
    renderFontDrawText(fuenteMenu, opt3, btn3X + 7, btnY + 3, 0xFF888888)
end

-- ==================== ESTADO Y RENDERIZADO GENERAL ====================
local spread = 0.0
local ultimoT = os.clock()
local ultimoObjetivo = nil
local ultimoMuerto = false
local killAt = -10
local modoLentoActivo = false

-- ==================== COMPATIBILIDAD CON BULLET TIME / SALTOS ====================
-- Mod de tiempo bala (Frnchise Bullet Time): en camara lenta y en los saltos usa su propia camara, que no es
-- ninguno de los modos de apuntado de arriba, y la mira original del juego esta anulada por el parche (RET).
-- Resultado: no se dibujaba ninguna mira. Aqui se detecta la camara lenta y se dibuja la mira propia.
local COMPAT_BULLET_TIME = true
local UMBRAL_CAMARA_LENTA = 0.9
local ARMAS_DISPARO = { [38] = true }
for id = 22, 33 do ARMAS_DISPARO[id] = true end
-- Saltos del mod (salto lateral con disparo): se detecta CJ en el aire con un arma de fuego y se mantiene la mira
-- COLA_ATERRIZAJE segundos despues de tocar el suelo (cuando sigue apuntando tumbado). Efecto secundario: en un
-- salto normal con arma NO aparece la mira salvo que mantengas el boton derecho, hayas disparado hace poco o haya camara lenta.
local COMPAT_SALTO = true
local COLA_ATERRIZAJE = 1.5
local ultimoEnAire = -10
local SOSTENER_LENTA = 0.25  -- la camara lenta debe durar este tiempo (s) para contar: asi un tiron del juego (cambiar de arma, streaming) no la activa
local AIRE_MIN = 0.12        -- CJ debe estar en el aire este tiempo (s) para contar como salto: asi bajar un bordillo o caminar por una cuesta no la activa
local inicioLenta, inicioAire = nil, nil
local velMundo = 1.0
local ultimoGameT, ultimoRealT = nil, nil

-- Tumbado en el suelo tras un salto (shootdodge): la mira del juego esta anulada y la camara del mod no es un modo de
-- apuntado normal, asi que al quedar tumbado disparando la mira desaparecia. Se detecta que CJ esta tumbado (la cabeza
-- casi a la altura del cuerpo) durante VENTANA_TUMBADO segundos tras el ultimo salto, y se muestra la mira mientras
-- mantienes el boton derecho del raton o hasta MANTENER_TRAS_DISPARO segundos despues de disparar.
local COMPAT_TUMBADO = true
local VENTANA_TUMBADO = 15        -- segundos tras el salto en los que se considera que puede seguir tumbado
local ALTURA_TUMBADO = 0.40       -- cabeza a menos de esta altura sobre el centro del cuerpo = tumbado (de pie ronda 0.65)
local MANTENER_TRAS_DISPARO = 2.0 -- segundos que sigue la mira despues del ultimo disparo (para mando o si sueltas el boton)
local TUMBADO_SIEMPRE = false      -- true: mira siempre visible mientras estes tumbado con un arma, apuntes o no
local ultimoDisparoReal = -10

local function alturaCabeza()
    local ok, hx, hy, hz = pcall(getBonePosition, playerPed, 6)
    if not (ok and type(hz) == "number") then return nil end
    local _, _, pz = getCharCoordinates(playerPed)
    return hz - pz
end

local function actualizarVelMundo(ahora)
    local ok, gt = pcall(getGameTimer)
    if not ok or type(gt) ~= "number" then return end
    if ultimoGameT then
        local dReal = (ahora - ultimoRealT) * 1000
        local dGame = gt - ultimoGameT
        if dReal > 0.5 and dReal < 250 and dGame >= 0 then
            velMundo = velMundo + (min(1.5, dGame / dReal) - velMundo) * 0.35
        end
    end
    ultimoGameT, ultimoRealT = gt, ahora
end

local function procesarYRenderizarFrame()
    local ahora = os.clock()
    local dt = min(0.1, max(0.0001, ahora - ultimoT))
    ultimoT = ahora

    local jugando = isPlayerPlaying(playerHandle) and doesCharExist(playerPed)
    if not jugando or pausaActiva() then return end

    actualizarVelMundo(ahora)

    local sw, sh = getScreenResolution()
    K = max(0.6, min(3.0, sh / 1080))
    local armaActual = getCurrentCharWeapon(playerPed) or -1
    local miraOriginal = ARMAS_MIRA_ORIGINAL[armaActual] == true
    local camMode = MP.android and mpCamMode(armaActual) or readMemory(MEM_CAM_MODE, 1, false)

    if menuIdiomaActivo then
        dibujarBannerSelectorIdioma(sw, sh)
    end

    local modoValido = MODOS_APUNTADO[camMode] == true
        or (armaActual == ARMA_COUNTRY_RIFLE and MODOS_RIFLE[camMode] == true)
    if armaActual >= 0 and armaActual <= 15 then modoValido = false end  -- puños y armas cuerpo a cuerpo: sin mira
    local camaraLenta = false
    if COMPAT_BULLET_TIME then
        if velMundo < UMBRAL_CAMARA_LENTA then
            inicioLenta = inicioLenta or ahora
            camaraLenta = (ahora - inicioLenta) >= SOSTENER_LENTA
        else
            inicioLenta = nil
        end
    end
    local enAire = false
    if COMPAT_BULLET_TIME and COMPAT_SALTO then
        local okA, a = pcall(isCharInAir, playerPed)
        local okC, enCoche = pcall(isCharInAnyCar, playerPed)
        enAire = okA and a == true and not (okC and enCoche == true)
        if enAire then
            inicioAire = inicioAire or ahora
            if (ahora - inicioAire) < AIRE_MIN then enAire = false else ultimoEnAire = ahora end
        else
            inicioAire = nil
        end
    end
    -- El salto solo muestra la mira si hay intencion de apuntar: boton derecho, un disparo reciente o camara lenta.
    -- Asi un salto normal con el arma en la mano no la activa.
    local okS0, disp0 = pcall(isCharShooting, playerPed)
    if okS0 and disp0 == true then ultimoDisparoReal = ahora end
    local intencionApuntar = camaraLenta or isKeyDown(0x02) or (ahora - ultimoDisparoReal) < 0.35
    local saltoActivo = intencionApuntar and (enAire or (ahora - ultimoEnAire) < COLA_ATERRIZAJE)

    -- Mira de respaldo, sin depender del modo de camara ni de detectar el salto: con un arma de fuego, fuera de un
    -- vehiculo, se muestra mientras mantienes el boton derecho del raton o justo despues de disparar. En un apuntado
    -- normal ya se mostraba por el modo de camara, asi que esto solo afecta a los estados raros del mod (tumbado, etc.).
    local tumbado, altCab = false, nil
    local apuntaTumbado = false
    if COMPAT_BULLET_TIME and COMPAT_TUMBADO and ARMAS_DISPARO[armaActual] == true then
        local okS, disparando = pcall(isCharShooting, playerPed)
        if okS and disparando == true then ultimoDisparoReal = ahora end
        local okC, enCoche = pcall(isCharInAnyCar, playerPed)
        enCoche = okC and enCoche == true

        altCab = alturaCabeza()
        tumbado = altCab ~= nil and altCab < ALTURA_TUMBADO and (ahora - ultimoEnAire) < VENTANA_TUMBADO

        if not enCoche then
            local desdeDisparo = ahora - ultimoDisparoReal
            -- solo en la ventana posterior a un salto: andando normal con el arma en la mano no debe aparecer
            local ventanaSalto = (ahora - ultimoEnAire) < VENTANA_TUMBADO
            apuntaTumbado = ventanaSalto and ((TUMBADO_SIEMPRE and tumbado)
                or isKeyDown(0x02)
                or desdeDisparo < 0.35
                or (tumbado and desdeDisparo < MANTENER_TRAS_DISPARO))
        end
    end

    local compatBT = (camaraLenta or saltoActivo or apuntaTumbado) and ARMAS_DISPARO[armaActual] == true
    local apuntando = (not miraOriginal and modoValido) or compatBT
    local apuntandoHud = (MODOS_APUNTADO[camMode] == true or MODOS_RIFLE[camMode] == true) or compatBT

    if MODO_CALIBRACION and fuente then
        renderFontDrawText(fuente, string.format("cam=%s  vel=%.2f  arma=%s  aire=%s  tumbado=%s  altCab=%s  disp=%.1f  miraBT=%s", tostring(camMode), velMundo, tostring(armaActual), tostring(enAire), tostring(tumbado), altCab and string.format("%.2f", altCab) or "n/a", ahora - ultimoDisparoReal, tostring(compatBT)), 20, sh * 0.35, 0xFFFFFF00)
    end

    actualizarHUDMunicion(sw, sh, ahora, dt, apuntandoHud)
    tickEfectosHud(ahora, dt)
    actualizarHUDChaleco(sw, sh, ahora, dt, apuntandoHud)
    actualizarBarraSprint(sw, sh, ahora, dt)

    if apuntando then
        local cx = floor(sw * 0.5) + OFFSET_X
        local cy = floor(sh * 0.5) + OFFSET_Y
        local opacidad = config.ajustes.opacidad

        if MODO_CALIBRACION and fuente then
            local vel = (modoLentoActivo and 20 or 90) * dt
            if isKeyDown(0x26) then OFFSET_Y = OFFSET_Y - vel end
            if isKeyDown(0x28) then OFFSET_Y = OFFSET_Y + vel end
            if isKeyDown(0x25) then OFFSET_X = OFFSET_X - vel end
            if isKeyDown(0x27) then OFFSET_X = OFFSET_X + vel end
            OFFSET_X = max(-400, min(400, OFFSET_X))
            OFFSET_Y = max(-400, min(400, OFFSET_Y))

            renderDrawBox(cx - 24, cy, 48, 1, col(120, 0, 255, 0))
            renderDrawBox(cx, cy - 24, 1, 48, col(120, 0, 255, 0))

            local estadoVel = modoLentoActivo and T("calibVelLenta") or T("calibVelNormal")
            local LineasCalib = {
                T("calibTitulo"),
                T("calibFlechas"),
                string.format(T("calibShift"), estadoVel),
                T("calibIdioma"),
                T("calibBorrar"),
                T("calibEnter"),
                T("calibAvisoCheat"),
                "",
                string.format("X: %d | Y: %d", floor(OFFSET_X + 0.5), floor(OFFSET_Y + 0.5))
            }
            for idx, lin in ipairs(LineasCalib) do
                if #lin > 0 then
                    renderFontDrawText(fuente, lin, 20, sh * 0.45 + (idx - 1) * 18, 0xFF00FF00)
                end
            end

            cx = floor(sw * 0.5) + OFFSET_X
            cy = floor(sh * 0.5) + OFFSET_Y
        end

        local objetivo = objetivoActual()
        local ajM = ajusteMira(armaActual)
        local opM = ajM.op
        local color = col(opM, ajM.r, ajM.g, ajM.b)

        if objetivo then
            local muertoAhora = isCharDead(objetivo) or getCharHealth(objetivo) <= 0
            local pedEnCentro = crucetaSobrePed(objetivo, cx, cy)

            if config.ajustes.colorEnemigo and not muertoAhora and pedEnCentro and not esAliado(objetivo) then
                color = col(opM, 255, 70, 70)
            end

            if config.ajustes.hitmarker and objetivo == ultimoObjetivo and not ultimoMuerto and muertoAhora then
                killAt = ahora
            end
            ultimoObjetivo = objetivo
            ultimoMuerto = muertoAhora
        else
            if config.ajustes.hitmarker and ultimoObjetivo and doesCharExist(ultimoObjetivo) then
                if not ultimoMuerto and isCharDead(ultimoObjetivo) then
                    killAt = ahora
                end
            end
            ultimoObjetivo = nil
            ultimoMuerto = false
        end

        if config.ajustes.hitmarker and (ahora - killAt) < 0.4 then
            color = col(opM, 255, 0, 0)
        end

        if config.ajustes.expansion then
            if isCharShooting(playerPed) then
                spread = 12.0 * ajM.exp
            else
                spread = max(0.0, spread - 45 * dt)
            end
        else
            spread = 0.0
        end

        dibujarMiraEstilo(
            ajM.estilo, cx, cy,
            ajM.tam * K, ajM.grosor * K + 0.001, ajM.sep * K, spread * K,
            color, opM, ajM.punto, ajM.contorno
        )
    else
        spread = 0.0
        ultimoObjetivo = nil

        if MODO_CALIBRACION and fuente then
            local yAv = sh * 0.5
            for linea in T("avisoNoApunta"):gmatch("[^\n]+") do
                renderFontDrawText(fuente, linea, 20, yAv, 0xFF00FF00)
                yAv = yAv + 18
            end
        end
    end
end

-- ==================== MENU DE EFECTOS DE BALAS (mimgui) ====================
local FX_EFECTOS = {
    { key = "fxDestello",   t = "fxE_destello",   tip = "fxT_destello" },
    { key = "fxChispas",    t = "fxE_chispas",    tip = "fxT_chispas" },
    { key = "fxHumo",       t = "fxE_humo",       tip = "fxT_humo" },
    { key = "fxImpacto",    t = "fxE_impacto",    tip = "fxT_impacto" },
    { key = "fxBrasas",     t = "fxE_brasas",     tip = "fxT_brasas" },
    { key = "fxFragmentos", t = "fxE_fragmentos", tip = "fxT_fragmentos" },
    { key = "fxRetroceso",  t = "fxE_retroceso",  tip = "fxT_retroceso" },
    { key = "fxEstela",     t = "fxE_estela",     tip = "fxT_estela" },
    { key = "fxResplandor", t = "fxE_resplandor", tip = "fxT_resplandor" },
    { key = "fxHumoCas",    t = "fxE_humocas",    tip = "fxT_humocas" },
    { key = "fxPulso",      t = "fxE_pulso",      tip = "fxT_pulso" },
    { key = "fxRecarga",    t = "fxE_recarga",    tip = "fxT_recarga" }
}

local CH_EFECTOS = {
    { key = "chDestello",  t = "fxCh_destello",  tip = "fxChT_destello" },
    { key = "chOnda",      t = "fxCh_onda",      tip = "fxChT_onda" },
    { key = "chSacudida",  t = "fxCh_sacudida",  tip = "fxChT_sacudida" },
    { key = "chBarraDano", t = "fxCh_barra",     tip = "fxChT_barra" },
    { key = "chRotura",    t = "fxCh_rotura",    tip = "fxChT_rotura" }
}

local AJ_HUD = {
    { key = "hudMunicion", t = "ajMunicion" },
    { key = "chalecoHud",  t = "ajChaleco" },
    { key = "chalecoBarra", t = "ajBarra" }
}

local BARRA_EFECTOS = {
    { key = "chBarraBrillo", t = "fxBE_brillo", tip = "fxBT_brillo" },
    { key = "chBarraLatido", t = "fxBE_latido", tip = "fxBT_latido" },
    { key = "chBarraPunta",  t = "fxBE_punta",  tip = "fxBT_punta" }
}

local FX_PRESETS = {
    { n = "fxPre_vanilla", v = { fxCasquillo = 1, fxPaleta = 1, fxIntensidad = 1.0, fxDestello = true, fxChispas = false, fxHumo = false, fxImpacto = false, fxBrasas = false, fxFragmentos = false, fxRetroceso = false, fxEstela = true, fxRetrocesoModo = 1, fxResplandor = false, fxHumoCas = false, fxPulso = false, fxRecarga = false } },
    { n = "fxPre_arcade", v = { fxCasquillo = 2, fxPaleta = 1, fxIntensidad = 1.3, fxDestello = true, fxChispas = true, fxHumo = false, fxImpacto = true, fxBrasas = false, fxFragmentos = false, fxRetroceso = true, fxEstela = true, fxRetrocesoModo = 3, fxResplandor = true, fxHumoCas = false, fxPulso = true, fxRecarga = true } },
    { n = "fxPre_cine", v = { fxCasquillo = 4, fxPaleta = 2, fxIntensidad = 1.0, fxDestello = true, fxChispas = false, fxHumo = true, fxImpacto = false, fxBrasas = true, fxFragmentos = false, fxRetroceso = true, fxEstela = false, fxRetrocesoModo = 2, fxResplandor = false, fxHumoCas = true, fxPulso = false, fxRecarga = true } },
    { n = "fxPre_neon", v = { fxCasquillo = 3, fxPaleta = 3, fxIntensidad = 1.1, fxDestello = true, fxChispas = true, fxHumo = false, fxImpacto = true, fxBrasas = false, fxFragmentos = false, fxRetroceso = false, fxEstela = true, fxRetrocesoModo = 1, fxResplandor = true, fxHumoCas = false, fxPulso = true, fxRecarga = true } },
    { n = "fxPre_metralla", v = { fxCasquillo = 6, fxPaleta = 4, fxIntensidad = 1.2, fxDestello = true, fxChispas = true, fxHumo = true, fxImpacto = false, fxBrasas = false, fxFragmentos = true, fxRetroceso = true, fxEstela = true, fxRetrocesoModo = 3, fxResplandor = false, fxHumoCas = true, fxPulso = false, fxRecarga = false } },
    { n = "fxPre_limpio", v = { fxCasquillo = 7, fxPaleta = 2, fxIntensidad = 1.0, fxDestello = false, fxChispas = false, fxHumo = false, fxImpacto = false, fxBrasas = false, fxFragmentos = false, fxRetroceso = false, fxEstela = false, fxRetrocesoModo = 1, fxResplandor = false, fxHumoCas = false, fxPulso = false, fxRecarga = false } }
}

local MN = nil
if USAR_MIMGUI then
    MN = {
        ptr = imgui.new.bool(true), auto = imgui.new.bool(false), sl = imgui.new.float(1.0), chk = {},
        acento = imgui.ImVec4(0.43, 0.72, 1.0, 1.0), aviso = imgui.ImVec4(1.0, 0.75, 0.25, 1.0), errImpreso = false,
        demo = imgui.new.bool(false), demoSl = imgui.new.float(70),
        mira = { f = imgui.new.float(0), b = imgui.new.bool(false), col = imgui.new.float[3](1, 1, 1) }
    }
    for _, lista in ipairs({ FX_EFECTOS, CH_EFECTOS, AJ_HUD, BARRA_EFECTOS }) do
        for _, e in ipairs(lista) do MN.chk[e.key] = imgui.new.bool(false) end
    end
end

local function marcarCambioFx()
    menuFx.sucio = true
    menuFx.ultCambio = os.clock()
end

local function guardarFxSiHaceFalta(forzar)
    if menuFx.sucio and (forzar or os.clock() - menuFx.ultCambio > 0.8) then
        menuFx.sucio = false
        guardarConfig()
    end
end

local function cerrarMenuFx()
    TXT.previa = false
    menuFx.abierto = false
    menuFx.autoOn = false
    menuFx.demo = false
    guardarFxSiHaceFalta(true)
end

local function alternarMenuFx()
    if not USAR_MIMGUI then
        pcall(printStringNow, "MIMGUI NOT FOUND", 2500)
        return
    end
    if menuFx.abierto then
        cerrarMenuFx()
    else
        MN.ptr[0] = true
        menuFx.abierto = true
    end
end

-- casillas para una lista de opciones booleanas; porDefecto = valor si el .ini aun no lo tiene
local function casillasFx(lista, porDefecto)
    local A = config.ajustes
    for _, e in ipairs(lista) do
        local ptr = MN.chk[e.key]
        local v = A[e.key]
        if v == nil then v = porDefecto end
        ptr[0] = (v == true)
        if imgui.Checkbox(T(e.t) .. "##" .. e.key, ptr) then
            A[e.key] = ptr[0]
            marcarCambioFx()
        end
        if e.tip and imgui.IsItemHovered() then imgui.SetTooltip(T(e.tip)) end
    end
end

-- ---------- pestana GENERAL: idioma, HUD y calibracion ----------
local function pestanaGeneral()
    local A = config.ajustes

    imgui.TextColored(MN.acento, T("ajIdioma"))
    local idiomas = { { "es", "Espanol" }, { "en", "English" } }
    for i, it in ipairs(idiomas) do
        local sel = (A.idioma == it[1])
        imgui.PushStyleColor(imgui.Col.Button, imgui.ImVec4(0.43, 0.72, 1.0, sel and 0.85 or 0.25))
        if imgui.Button(it[2] .. "##lang" .. i, imgui.ImVec2(110, 24)) then
            A.idioma = it[1]
            A.idiomaElegido = true
            menuIdiomaActivo = false
            guardarConfig()
        end
        imgui.PopStyleColor()
        if i < #idiomas then imgui.SameLine() end
    end

    imgui.Separator()
    imgui.TextColored(MN.acento, T("ajHud"))
    casillasFx(AJ_HUD, true)

    imgui.Separator()
    imgui.TextColored(MN.acento, T("ajMira"))
    if MODO_CALIBRACION then
        if imgui.Button(T("ajCalibrarFin") .. "##calib") then
            MODO_CALIBRACION = false
            menuIdiomaActivo = false
            A.calibrado = true
            guardarConfig()
        end
    else
        if imgui.Button(T("ajCalibrar") .. "##calib") then
            MODO_CALIBRACION = true
            menuIdiomaActivo = false
            cerrarMenuFx()
        end
    end
    if imgui.IsItemHovered() then imgui.SetTooltip(T("ajCalibrarTip")) end
end

-- ---------- pestana BALAS: presets, efectos, casquillo, color e intensidad ----------
local function pestanaBalas()
    local A = config.ajustes

    imgui.TextColored(MN.acento, T("fxPresets"))
    for i, pre in ipairs(FX_PRESETS) do
        if imgui.Button(T(pre.n) .. "##pre" .. i) then
            for k, v in pairs(pre.v) do A[k] = v end
            marcarCambioFx()
        end
        if i < #FX_PRESETS then imgui.SameLine() end
    end

    imgui.Separator()
    imgui.BeginGroup()
    imgui.TextColored(MN.acento, T("fxEfectos"))
    casillasFx(FX_EFECTOS, false)
    imgui.EndGroup()

    imgui.SameLine(0, 36)
    imgui.BeginGroup()
    imgui.TextColored(MN.acento, T("fxCasquillo"))
    for i, est in ipairs(ESTILOS_CAS) do
        if imgui.Selectable(T("fxC_" .. est.id) .. "##cas" .. i, (A.fxCasquillo or 1) == i, 0, imgui.ImVec2(130, 0)) then
            A.fxCasquillo = i
            marcarCambioFx()
        end
    end
    imgui.EndGroup()

    imgui.Separator()
    imgui.TextColored(MN.acento, T("fxRetroDir"))
    for i, id in ipairs({ "h", "v", "a" }) do
        local sel = ((A.fxRetrocesoModo or 1) == i)
        imgui.PushStyleColor(imgui.Col.Button, imgui.ImVec4(0.43, 0.72, 1.0, sel and 0.85 or 0.25))
        if imgui.Button(T("fxR_" .. id) .. "##rdir" .. i, imgui.ImVec2(110, 22)) then
            A.fxRetrocesoModo = i
            if not A.fxRetroceso then A.fxRetroceso = true end
            marcarCambioFx()
            hud.probar = true
        end
        imgui.PopStyleColor()
        if i < 3 then imgui.SameLine() end
    end

    imgui.Separator()
    imgui.TextColored(MN.acento, T("casModo"))
    for i = 1, 3 do
        local sel = ((A.casModo or 1) == i)
        imgui.PushStyleColor(imgui.Col.Button, imgui.ImVec4(0.43, 0.72, 1.0, sel and 0.85 or 0.25))
        if imgui.Button(T("casM_" .. i) .. "##casm" .. i, imgui.ImVec2(130, 22)) then
            A.casModo = i
            marcarCambioFx()
            hud.probar = true
        end
        imgui.PopStyleColor()
        if i < 3 then imgui.SameLine() end
    end

    imgui.Separator()
    imgui.TextColored(MN.acento, T("fxColor"))
    for i, pal in ipairs(PALETAS) do
        local sel = ((A.fxPaleta or 1) == i)
        imgui.PushStyleColor(imgui.Col.Button, imgui.ImVec4(pal.c1[1] / 255, pal.c1[2] / 255, pal.c1[3] / 255, sel and 1.0 or 0.45))
        if imgui.Button((sel and "X" or " ") .. "##pal" .. i, imgui.ImVec2(34, 20)) then
            A.fxPaleta = i
            marcarCambioFx()
        end
        imgui.PopStyleColor()
        imgui.SameLine()
    end
    imgui.Text(T("fxP_" .. (PALETAS[A.fxPaleta or 1] or PALETAS[1]).id))

    MN.sl[0] = A.fxIntensidad or 1.0
    imgui.PushItemWidth(200)
    if imgui.SliderFloat("##fxint", MN.sl, 0.3, 2.0, "%.1f") then
        A.fxIntensidad = MN.sl[0]
        marcarCambioFx()
    end
    imgui.PopItemWidth()
    imgui.SameLine()
    imgui.Text(T("fxIntensidad"))

    imgui.Separator()
    imgui.TextColored(MN.acento, T("txtTitulo"))
    imgui.Text(T("txtEstilo"))
    for i = 1, 5 do
        local sel = ((A.txtEstilo or 1) == i)
        imgui.PushStyleColor(imgui.Col.Button, imgui.ImVec4(0.43, 0.72, 1.0, sel and 0.85 or 0.25))
        if imgui.Button(T("txtE_" .. i) .. "##txe" .. i, imgui.ImVec2(100, 22)) then
            A.txtEstilo = i
            marcarCambioFx()
        end
        imgui.PopStyleColor()
        if i < 5 then imgui.SameLine() end
    end
    imgui.Text(T("txtRecarga"))
    for i = 1, 5 do
        local sel = ((A.txtRecarga or 1) == i)
        imgui.PushStyleColor(imgui.Col.Button, imgui.ImVec4(0.43, 0.72, 1.0, sel and 0.85 or 0.25))
        if imgui.Button(T("txtR_" .. i) .. "##txr" .. i, imgui.ImVec2(100, 22)) then
            A.txtRecarga = i
            marcarCambioFx()
        end
        imgui.PopStyleColor()
        if i < 5 then imgui.SameLine() end
    end
    MN.txPop = MN.txPop or imgui.new.bool(true)
    MN.txPop[0] = (A.txtPop ~= false)
    if imgui.Checkbox(T("txtPop") .. "##txpop", MN.txPop) then A.txtPop = MN.txPop[0]; marcarCambioFx() end
    imgui.SameLine()
    MN.txPrev = MN.txPrev or imgui.new.bool(false)
    MN.txPrev[0] = TXT.previa
    if imgui.Checkbox(T("txtPrevia") .. "##txprev", MN.txPrev) then TXT.previa = MN.txPrev[0] end
    imgui.Text(T("txtFuente"))
    if imgui.Selectable(T("txtFuenteDef") .. "##txf0", (A.txtFuente or 0) == 0, 0, imgui.ImVec2(200, 0)) then
        A.txtFuente = 0; marcarCambioFx()
    end
    for i, fu in ipairs(TXT.fuentes) do
        if imgui.Selectable(fu.nombre .. "##txf" .. i, (A.txtFuente or 0) == i, 0, imgui.ImVec2(200, 0)) then
            A.txtFuente = i; marcarCambioFx()
        end
    end
    if #TXT.fuentes == 0 then imgui.TextDisabled(T("txtSinFuentes")) end

    imgui.Separator()
    if imgui.Button(T("fxProbar") .. "##probar") then hud.probar = true end
    imgui.SameLine()
    MN.auto[0] = menuFx.autoOn
    if imgui.Checkbox(T("fxAuto") .. "##auto", MN.auto) then menuFx.autoOn = MN.auto[0] end
    if not CLIP_ARMA[getCurrentCharWeapon(playerPed)] then
        imgui.TextColored(MN.aviso, T("fxSinArma"))
    end
end

-- ---------- pestana POSICION: posicion y escala de cada elemento del HUD ----------
local POS_DEF = { hudX = 50, hudAltura = 210, hudEscala = 1.0, chalecoX = 50, chalecoAltura = 290, chalecoEscala = 1.0, sprintX = 50, sprintAltura = 150 }

local function sliderCfg(clave, etiqueta, mn, mx, fmt)
    local A = config.ajustes
    MN.pos = MN.pos or {}
    local ptr = MN.pos[clave]
    if not ptr then ptr = imgui.new.float(0); MN.pos[clave] = ptr end
    ptr[0] = A[clave] or POS_DEF[clave]
    imgui.PushItemWidth(240)
    if imgui.SliderFloat("##pos" .. clave, ptr, mn, mx, fmt) then
        A[clave] = ptr[0]
        marcarCambioFx()
    end
    imgui.PopItemWidth()
    imgui.SameLine()
    imgui.Text(T(etiqueta))
end

local function pestanaPosicion()
    TXT.mostrar = true   -- mantiene visible el HUD de municion mientras se ajusta
    imgui.TextColored(MN.acento, T("posHud"))
    sliderCfg("hudX", "posX", 0, 900, "%.0f")
    sliderCfg("hudAltura", "posY", 0, 1000, "%.0f")
    sliderCfg("hudEscala", "posEsc", 0.5, 2.5, "%.2f")

    imgui.Separator()
    imgui.TextColored(MN.acento, T("posChaleco"))
    sliderCfg("chalecoX", "posX", 0, 900, "%.0f")
    sliderCfg("chalecoAltura", "posY", 0, 1000, "%.0f")
    sliderCfg("chalecoEscala", "posEsc", 0.5, 2.5, "%.2f")

    imgui.Separator()
    imgui.TextColored(MN.acento, T("posSprint"))
    sliderCfg("sprintX", "posX", 0, 900, "%.0f")
    sliderCfg("sprintAltura", "posY", 0, 1000, "%.0f")
    imgui.TextDisabled(T("posEscSprint"))

    imgui.Separator()
    if imgui.Button(T("posReset") .. "##posreset") then
        for k, v in pairs(POS_DEF) do config.ajustes[k] = v end
        marcarCambioFx()
    end
end

-- ---------- pestana CHALECO: efectos de impacto y barra (estilos + efectos) ----------
local function pestanaChaleco()
    local A = config.ajustes

    imgui.BeginGroup()
    imgui.TextColored(MN.acento, T("fxChaleco"))
    casillasFx(CH_EFECTOS, true)
    imgui.EndGroup()
    imgui.SameLine(0, 36)
    imgui.BeginGroup()
    if imgui.Button(T("fxProbarGolpe") .. "##pgolpe") then chaleco.probar = "golpe" end
    if imgui.Button(T("fxProbarRotura") .. "##protura") then chaleco.probar = "rotura" end
    imgui.EndGroup()

    imgui.Separator()
    imgui.BeginGroup()
    imgui.TextColored(MN.acento, T("fxBarra"))
    for i, id in ipairs(BARRA_ESTILOS) do
        if imgui.Selectable(T("fxB_" .. id) .. "##bar" .. i, (A.chEstiloBarra or 1) == i, 0, imgui.ImVec2(130, 0)) then
            A.chEstiloBarra = i
            marcarCambioFx()
        end
    end
    imgui.EndGroup()
    imgui.SameLine(0, 36)
    imgui.BeginGroup()
    imgui.TextColored(MN.acento, T("fxBEfectos"))
    casillasFx(BARRA_EFECTOS, false)
    MN.demo[0] = (menuFx.demo == true)
    if imgui.Checkbox(T("fxBVista") .. "##demo", MN.demo) then menuFx.demo = MN.demo[0] end
    if imgui.IsItemHovered() then imgui.SetTooltip(T("fxBVistaTip")) end
    MN.demoSl[0] = menuFx.demoPct or 70
    imgui.PushItemWidth(150)
    if imgui.SliderFloat("##demopct", MN.demoSl, 0, 100, "%.0f") then menuFx.demoPct = MN.demoSl[0] end
    imgui.PopItemWidth()
    imgui.EndGroup()
end

-- ---------- pestana MIRA: diseno y ajustes por tipo de arma ----------
local menuMira = { cat = 1 }

local function vistaPreviaMira(r)
    local pos = imgui.GetCursorScreenPos()
    local w, h = 170, 130
    imgui.Dummy(imgui.ImVec2(w, h))
    local dl = imgui.GetWindowDrawList()
    dl:AddRectFilled(pos, imgui.ImVec2(pos.x + w, pos.y + h), 0xFF2A221C, 8)
    local old = DL
    DL = dl
    local t = os.clock() % 1.6
    local sp = (t < 0.35) and (12.0 * r.exp * (1 - t / 0.35)) or 0
    pcall(dibujarMiraEstilo, r.estilo, pos.x + w * 0.5, pos.y + h * 0.5,
        r.tam * K, r.grosor * K + 0.001, r.sep * K, sp * K,
        col(r.op, r.r, r.g, r.b), r.op, r.punto, r.contorno)
    DL = old
end

local function pestanaMira()
    local A = config.ajustes
    local pref = CATS_MIRA[menuMira.cat] or "mP"

    for i, c in ipairs(CATS_MIRA) do
        local sel = (menuMira.cat == i)
        imgui.PushStyleColor(imgui.Col.Button, imgui.ImVec4(0.43, 0.72, 1.0, sel and 0.85 or 0.25))
        if imgui.Button(T("miraC_" .. c) .. "##mcat" .. i, imgui.ImVec2(130, 24)) then menuMira.cat = i end
        imgui.PopStyleColor()
        if i < #CATS_MIRA then imgui.SameLine() end
    end
    imgui.Separator()

    local r = ajusteMiraCat(pref)
    if A[pref .. "Estilo"] == nil then   -- fija los valores efectivos para poder editarlos
        A[pref .. "Estilo"], A[pref .. "Tam"], A[pref .. "Grosor"] = r.estilo, r.tam, r.grosor
        A[pref .. "Sep"], A[pref .. "Punto"] = r.sep, r.punto
    end

    imgui.BeginGroup()
    imgui.TextColored(MN.acento, T("miraEstilo"))
    for i, id in ipairs(ESTILOS_MIRA) do
        if imgui.Selectable(T("miraE_" .. id) .. "##mest" .. i, r.estilo == i, 0, imgui.ImVec2(160, 0)) then
            local d = EST_DEF[id]
            A[pref .. "Estilo"], A[pref .. "Tam"], A[pref .. "Grosor"], A[pref .. "Sep"] = i, d.tam, d.grosor, d.sep
            A[pref .. "Punto"] = (d.punto == true)
            marcarCambioFx()
        end
    end
    imgui.EndGroup()
    imgui.SameLine(0, 24)
    imgui.BeginGroup()
    imgui.TextColored(MN.acento, T("miraVista"))
    vistaPreviaMira(r)
    imgui.EndGroup()

    imgui.Separator()
    local f = MN.mira.f
    local function sl(txt, key, v, mn, mx, fmt)
        f[0] = v
        imgui.PushItemWidth(220)
        if imgui.SliderFloat("##msl" .. key, f, mn, mx, fmt) then
            A[pref .. key] = f[0]
            marcarCambioFx()
        end
        imgui.PopItemWidth()
        imgui.SameLine()
        imgui.Text(T(txt))
    end
    sl("miraTam", "Tam", r.tam, 1, 40, "%.1f")
    sl("miraGrosor", "Grosor", r.grosor, 0.5, 8, "%.1f")
    sl("miraSep", "Sep", r.sep, 0, 20, "%.1f")
    sl("miraExp", "Exp", r.exp, 0, 3, "%.1f")
    sl("miraOp", "Op", r.op, 40, 255, "%.0f")

    local b = MN.mira.b
    b[0] = r.punto
    if imgui.Checkbox(T("miraPunto") .. "##mpunto", b) then A[pref .. "Punto"] = b[0]; marcarCambioFx() end
    imgui.SameLine(0, 24)
    b[0] = r.contorno
    if imgui.Checkbox(T("miraContorno") .. "##mcont", b) then A[pref .. "Contorno"] = b[0]; marcarCambioFx() end

    local c = MN.mira.col
    c[0], c[1], c[2] = r.r / 255, r.g / 255, r.b / 255
    imgui.PushItemWidth(220)
    if imgui.ColorEdit3("##mcol", c) then
        A[pref .. "ColR"], A[pref .. "ColG"], A[pref .. "ColB"] = floor(c[0] * 255 + 0.5), floor(c[1] * 255 + 0.5), floor(c[2] * 255 + 0.5)
        marcarCambioFx()
    end
    imgui.PopItemWidth()
    imgui.SameLine()
    imgui.Text(T("miraColor"))
    imgui.TextDisabled(T("miraNotaEnemigo"))

    imgui.Separator()
    if imgui.Button(T("miraAplicarTodas") .. "##mall") then
        for _, c2 in ipairs(CATS_MIRA) do
            if c2 ~= pref then
                for _, k in ipairs(KEYS_MIRA) do A[c2 .. k] = A[pref .. k] end
            end
        end
        marcarCambioFx()
    end
    imgui.SameLine()
    if imgui.Button(T("miraRestaurar") .. "##mrest") then
        for _, k in ipairs(KEYS_MIRA) do A[pref .. k] = nil end
        marcarCambioFx()
    end
end

local function cuerpoMenuFx()
    if imgui.BeginTabBar("##tabsmenu") then
        if imgui.BeginTabItem(T("tabGeneral") .. "###tabgen") then
            pcall(pestanaGeneral)
            imgui.EndTabItem()
        end
        if imgui.BeginTabItem(T("tabBalas") .. "###tabbal") then
            pcall(pestanaBalas)
            imgui.EndTabItem()
        end
        if imgui.BeginTabItem(T("tabMira") .. "###tabmira") then
            pcall(pestanaMira)
            imgui.EndTabItem()
        end
        if imgui.BeginTabItem(T("tabPos") .. "###tabpos") then
            pcall(pestanaPosicion)
            imgui.EndTabItem()
        end
        if imgui.BeginTabItem(T("tabChaleco") .. "###tabcha") then
            pcall(pestanaChaleco)
            imgui.EndTabItem()
        end
        imgui.EndTabBar()
    end
    imgui.Separator()
    imgui.TextDisabled(T("ajAyuda"))
    if imgui.Button(T("fxCerrar") .. "##cerrar") then cerrarMenuFx() end
end

local function dibujarMenuFx()
    local sw, sh = getScreenResolution()
    imgui.SetNextWindowPos(imgui.ImVec2(sw * 0.5, sh * 0.42), imgui.Cond.FirstUseEver, imgui.ImVec2(0.5, 0.5))
    imgui.SetNextWindowSize(imgui.ImVec2(580, 0), imgui.Cond.Always)
    if imgui.Begin(T("ajTitulo") .. "###fxmenu", MN.ptr, imgui.WindowFlags.NoCollapse + imgui.WindowFlags.NoResize) then
        local ok, err = pcall(cuerpoMenuFx)
        if not ok and not MN.errImpreso then
            MN.errImpreso = true
            print("[HUD] menu: " .. tostring(err))
        end
    end
    imgui.End()
    if not MN.ptr[0] then cerrarMenuFx() end
    guardarFxSiHaceFalta(false)
end

if USAR_MIMGUI then
    imgui.OnInitialize(function()
        pcall(cargarTexturas)
        -- fuentes opcionales: cualquier .ttf/.otf que haya en moonloader/resource/fonts
        pcall(function()
            local lfs = require 'lfs'
            local dir = getWorkingDirectory() .. '\\resource\\fonts'
            local lista = {}
            for f in lfs.dir(dir) do
                local ext = f:match('%.(%w+)$')
                if ext and (ext:lower() == 'ttf' or ext:lower() == 'otf') then lista[#lista + 1] = f end
            end
            if #lista > 0 then
                table.sort(lista)
                local fonts = imgui.GetIO().Fonts
                fonts:AddFontDefault()
                for _, f in ipairs(lista) do
                    local ptr = fonts:AddFontFromFileTTF(dir .. '\\' .. f, 32.0)
                    if ptr then TXT.fuentes[#TXT.fuentes + 1] = { nombre = f:gsub('%.%w+$', ''), ptr = ptr } end
                end
            end
        end)
        -- estilo del menu a juego con el HUD: fondo oscuro y acento azul del chaleco
        pcall(function()
            local st = imgui.GetStyle()
            local c = st.Colors
            local ac = function(a) return imgui.ImVec4(0.43, 0.72, 1.0, a) end
            st.WindowRounding = 2
            st.FrameRounding = 2
            st.WindowPadding = imgui.ImVec2(12, 10)
            st.ItemSpacing = imgui.ImVec2(8, 6)
            c[imgui.Col.WindowBg] = imgui.ImVec4(0.07, 0.07, 0.07, 0.94)
            c[imgui.Col.TitleBg] = imgui.ImVec4(0.07, 0.07, 0.07, 1.0)
            c[imgui.Col.TitleBgActive] = imgui.ImVec4(0.10, 0.10, 0.10, 1.0)
            c[imgui.Col.FrameBg] = imgui.ImVec4(0.15, 0.15, 0.15, 1.0)
            c[imgui.Col.FrameBgHovered] = imgui.ImVec4(0.22, 0.22, 0.22, 1.0)
            c[imgui.Col.CheckMark] = ac(1.0)
            c[imgui.Col.SliderGrab] = ac(0.9)
            c[imgui.Col.SliderGrabActive] = ac(1.0)
            c[imgui.Col.Header] = ac(0.25)
            c[imgui.Col.HeaderHovered] = ac(0.40)
            c[imgui.Col.HeaderActive] = ac(0.55)
            c[imgui.Col.Button] = ac(0.25)
            c[imgui.Col.ButtonHovered] = ac(0.45)
            c[imgui.Col.ButtonActive] = ac(0.65)
            c[imgui.Col.Separator] = imgui.ImVec4(0.43, 0.72, 1.0, 0.35)
        end)
        pcall(function()
            local c = imgui.GetStyle().Colors
            c[imgui.Col.Tab] = imgui.ImVec4(0.43, 0.72, 1.0, 0.20)
            c[imgui.Col.TabHovered] = imgui.ImVec4(0.43, 0.72, 1.0, 0.50)
            c[imgui.Col.TabActive] = imgui.ImVec4(0.43, 0.72, 1.0, 0.45)
        end)
    end)

    imgui.OnFrame(
        function()
            return modListo and not MP.off and not pausaActiva() and isPlayerPlaying(playerHandle)
        end,
        function(self)
            self.HideCursor = true
            if not texIntentado then pcall(cargarTexturas) end
            DL = imgui.GetBackgroundDrawList()
            pcall(procesarYRenderizarFrame)
            DL = nil
        end
    )

    imgui.OnFrame(
        function()
            return modListo and menuFx.abierto and not pausaActiva()
        end,
        function(self)
            self.HideCursor = false
            self.LockPlayer = true
            dibujarMenuFx()
        end
    )
end

-- ==================== BUCLE PRINCIPAL ====================
function main()
    while not isPlayerPlaying(playerHandle) do
        wait(100)
    end

    fuente = renderCreateFont("Arial", 11, 5)
    fuenteMunicion = renderCreateFont("Arial", 17, 5)
    fuenteMenu = renderCreateFont("Arial", 9, 5)
    pcall(cargarSonidoCasquillo)

    if not USAR_MIMGUI then
        cargarTexturas()
    end

    local leido = readMemory(MEM_CROSSHAIR, 1, true)
    if leido ~= 0xC3 and leido ~= 0 then
        originalByte = leido
        if config.sistema.byteOriginal ~= leido then
            config.sistema.byteOriginal = leido
            guardarConfig()
        end
    end
    ponerParche(true)
    pcall(MP.hookCrosshair)

    -- MP: /ammohud = menu, /ammohud on|off
    if sampRegisterChatCommand then
        sampRegisterChatCommand('ammohud', function(arg)
            arg = tostring(arg or ''):lower():gsub('%s', '')
            if arg == 'off' or arg == 'on' then
                MP.off = (arg == 'off')
                if not MP.android then ponerParche(not MP.off) end
                pcall(printStringNow, 'Ammo HUD ' .. (MP.off and 'off' or 'on'), 1500)
            elseif arg == 'calib' or arg == 'calibrate' then
                MODO_CALIBRACION = not MODO_CALIBRACION
            else
                if USAR_MIMGUI then alternarMenuFx() end
            end
        end)
    end

    local frame = 0
    modListo = true

    while true do
        wait(0)
        frame = frame + 1

        local jugando = isPlayerPlaying(playerHandle) and doesCharExist(playerPed)
        local armaActual = jugando and getCurrentCharWeapon(playerPed) or -1
        local miraOriginal = ARMAS_MIRA_ORIGINAL[armaActual] == true

        if miraOriginal then
            ponerParche(false)
        elseif not parcheActivo then
            ponerParche(true)
        end

        if frame % 30 == 0 and parcheActivo and readMemory(MEM_CROSSHAIR, 1, true) ~= 0xC3 then
            writeMemory(MEM_CROSSHAIR, 1, 0xC3, true)
        end

        if not pausaActiva() and jugando then
            if isKeyDown(0x11) and wasKeyPressed(0x4D) then   -- Ctrl + M: menu unico
                if USAR_MIMGUI then
                    alternarMenuFx()
                else
                    -- sin mimgui no hay menu: Ctrl+M alterna la calibracion de la mira
                    MODO_CALIBRACION = not MODO_CALIBRACION
                    menuIdiomaActivo = MODO_CALIBRACION
                end
            end

            if menuIdiomaActivo then
                if wasKeyPressed(0x31) or wasKeyPressed(0x61) then
                    config.ajustes.idioma = "es"
                    config.ajustes.idiomaElegido = true
                    guardarConfig()
                    menuIdiomaActivo = false
                elseif wasKeyPressed(0x32) or wasKeyPressed(0x62) then
                    config.ajustes.idioma = "en"
                    config.ajustes.idiomaElegido = true
                    guardarConfig()
                    menuIdiomaActivo = false
                elseif wasKeyPressed(0x1B) then
                    menuIdiomaActivo = false
                end
            end

            if MODO_CALIBRACION then
                if wasKeyPressed(0x4C) then
                    config.ajustes.idioma = (config.ajustes.idioma == "es") and "en" or "es"
                    guardarConfig()
                end
                if wasKeyPressed(0x10) then modoLentoActivo = not modoLentoActivo end
                if wasKeyPressed(0x08) then OFFSET_X, OFFSET_Y = 0, 0 end
                if wasKeyPressed(0x0D) then
                    MODO_CALIBRACION = false
                    menuIdiomaActivo = false
                    config.ajustes.calibrado = true
                    guardarConfig()
                end
            end

            if not USAR_MIMGUI then
                procesarYRenderizarFrame()
            end
        end
    end
end

function onScriptTerminate(scr, quitGame)
    if scr == thisScript() then
        modListo = false
        pcall(guardarFxSiHaceFalta, true)
        if casSnd then pcall(releaseAudioStream, casSnd); casSnd = nil end
        if not USAR_MIMGUI and natReleaseTex then
            for _, tex in pairs(TEX) do
                pcall(natReleaseTex, tex)
            end
        end
        if fuente then pcall(renderReleaseFont, fuente); fuente = nil end
        if fuenteMunicion then pcall(renderReleaseFont, fuenteMunicion); fuenteMunicion = nil end
        if fuenteMenu then pcall(renderReleaseFont, fuenteMenu); fuenteMenu = nil end
        if originalByte ~= 0 then
            pcall(writeMemory, MEM_CROSSHAIR, 1, originalByte, true)
        end
    end
end