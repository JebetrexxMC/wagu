-- alloader compatibility module for MP (MonetLoader inside the MP mod).
-- Scripts written for ReSA's AL Loader (Alyn Lua Loader) do `require 'alloader'`
-- for touch_events / platform / version. AL Loader itself defines MONET_VERSION,
-- so its scripts are MonetLoader scripts; this module fills the AL-only names.
local touch_events = { POP = 1, PUSH = 2, MOVE = 3 }   -- same values as AL Loader 1.1.8
AL_PLATFORM  = AL_PLATFORM  or 'android'
AL_VERSION   = AL_VERSION   or (MONET_VERSION and ('monet-' .. tostring(MONET_VERSION))) or 'mp'
AL_DPI_SCALE = AL_DPI_SCALE or MONET_DPI_SCALE or 1.0
AL_LIBRARY   = AL_LIBRARY   or 'MP'
return { touch_events = touch_events, platform = AL_PLATFORM, version = AL_VERSION }
