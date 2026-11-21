fx_version 'cerulean'
game 'gta5'

author 'MP'
description 'Example FiveM resource: speed / street HUD with DrawText and DrawRect'
version '1.0'

-- MP: stays stopped until /hudexample is typed (fivem_loader.lua starts it then)
start_on_command 'hudexample'

client_scripts {
    'client.lua',
}
