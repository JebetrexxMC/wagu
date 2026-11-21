fx_version 'cerulean'
game 'gta5'

name 'fs-graphics'
author 'IDK_FORCE (FS-Graphics), MP port'
description 'FS-Graphics look for GTA SA: sky, clouds, water, nights, colours, street lights'
version '1.0-mp'

-- MP: stays stopped until /fsgraphics is typed (fivem_loader.lua starts it then)
start_on_command 'fsgraphics'

client_script 'client.lua'
