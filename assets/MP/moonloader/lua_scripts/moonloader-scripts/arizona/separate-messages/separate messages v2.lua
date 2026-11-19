script_name("Separate Messages")
script_version("2.5")
-- script_moonloader(16) moonloader v.0.26
-- fork of Separate Messages v2 https://www.blast.hk/threads/69714/
-- changed: 
-- unnecessary dependencies have been removed 
-- delay in sending to the chat has been fixed (anti-flood fix)
-- made the code more readable

local sampev = require 'lib.samp.events'

-- ÌÎÆÍÎ ÄÎÁÀÂÈÒÜ ÑÂÎÞ ÊÎÌÀÍÄÓ ÄËß ÏÅÐÅÍÎÑÀ ÒÓÒ, ÈËÈ ÓÁÐÀÒÜ ËÈØÍÞÞ
commands = {
   'c', 's', 'b', 'r', 'm', 'd', 'f', 'rb', 'fb', 'rt', 
   'pt', 'ft', 'cs', 't', 'ct', 'fam', 'vr', 'al',
}
isDivided = false
-- Çäåñü ìîæíî èçìåíèòü çàäåðæêó îòïðàâêè âòîðîé ÷àñòè ñîîáùåíèÿ â ÷àò 
-- ïî-óìîë÷àíèþ 1000 ìñ.
sendDelay = 1000

function sampev.onSendCommand(msg)
   if isDivided then 
      isDivided = false 
      return
   end
   
   local cmd, msg = msg:match("/(%S*) (.*)")
   if msg == nil then
      return 
   end

   -- Ðàöèÿ, ðàäèî, ÎÎÑ ÷àò, øåïîò, êðèê (ñ ïîääåðæêîé ïåðåíîñà ÎÎÑ-ñêîáîê)
   for i, v in ipairs(commands) do 
      if cmd == v then
         local length = msg:len()
         if msg:sub(1, 2) == "((" then
            msg = string.gsub(msg:sub(4), "%)%)", "")
            if length > 80 then 
               divide(msg, "/" .. cmd .. " (( ", " ))") 
               return false
            end
         else
            if length > 80 then 
               divide(msg, "/" .. cmd .. " ", "")
               return false
            end
         end
      end 
   end

   -- ÐÏ êîìàíäû
   if cmd == "me" or cmd == "do" then
      local length = msg:len()
      if length > 75 then 
         divide(msg, "/" .. cmd .. " ", "", "ext")
         return false
      end
   end
   
end

function sampev.onServerMessage(color, text)
   if color == -65281 and text:find(" %| Ïîëó÷àòåëü: ") then
      return {bit.tobit(0xFFCC00FF), text}
   end
end

function sampev.onSendChat(msg) -- IC 
   if isDivided then 
      isDivided = false
      return 
   end
   local length = msg:len()
   if length > 90 then
      divide(msg, "", "")
      return false
   end
end

function divide(msg, beginning, ending, doing)
   limit = 72
   
   local chatprefix = string.match(msg:sub(0, 1), "%p") -- Any punctuation character
   local firstpart, secondpart = string.match(msg:sub(1, limit), "(.*) (.*)")
   if not secondpart then 
      secondpart = ""
   end 
   if firstpart then
      firstpart, secondpart = firstpart .. "...", "..." .. secondpart .. msg:sub(limit + 1, msg:len())

      isDivided = true
      sampSendChat(beginning .. firstpart .. ending)
      if doing == "ext" then
         beginning = "/do "
         if secondpart:sub(-1) ~= "." then secondpart = secondpart .. "." end
      end
      isDivided = true
      lua_thread.create(function()
         wait(sendDelay)
         if chatprefix then
            sampSendChat(chatprefix .. beginning .. secondpart .. ending)
         else
            sampSendChat(beginning .. secondpart .. ending)
         end
      end)
   end
end

function main()
   if not isCleoLoaded() or not isSampfuncsLoaded() or not isSampLoaded() then return end
   while not isSampAvailable() do wait(100) end 
   while sampGetGamestate() ~= 3 or not sampIsLocalPlayerSpawned() do wait(0) end
   wait(-1)
end
