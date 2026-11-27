import net from "node:net";
import readline from "node:readline";

const port = Number(process.env.PORT || 6767);
const rooms = new Map();
let nextId = 1;
let nextNpcId = 1;
let nextVehicleId = 1;
const playersById = new Map();
const blockedAddresses = new Map();

function roomFor(name) { if (!rooms.has(name)) rooms.set(name, new Map()); return rooms.get(name); }
function leaderFor(room) { return room.values().next().value; }
function timePacket(time) { return `TIME|${time.hour}|${time.minute}|${time.day}\n`; }
function announceTimeAuthority(room) { const leader=leaderFor(room); if(leader) broadcast(room,`TIME_OWNER|${leader.id}\n`); else room.clock=null; }
function num(value, fallback) { const n = Number(value); return Number.isFinite(n) ? n : fallback; }
function clamp(value) { return Math.max(-3000, Math.min(3000, value)); }
function model(value) { const n = Number(value); return Number.isInteger(n) && n >= 0 && n < 20000 ? n : 0; }
const specialNames = new Set(["ANDRE","BBTHIN","BB","CAT","CESAR","COPGRL1","COPGRL2","COPGRL3","CLAUDE","CROGRL1","CROGRL2","CROGRL3","DWAYNE","EMMET","FORELLI","GANGRL1","GANGRL2","GANGRL3","GUNGRL1","GUNGRL2","GUNGRL3","HERN","JANITOR","JETHRO","JIZZY","KENDL","MACCER","MADDOGG","MECGRL1","MECGRL2","MECGRL3","NURGRL1","NURGRL2","NURGRL3","OGLOC","PAUL","PULASKI","ROSE","RYDER","RYDER1","RYDER2","RYDER3","SINDACO","SMOKE","BIGSMOKE","SMOKEV","SUZIE","SWEET","TBONE","TENPEN","TORINO","TRUTH","WUZIMU","ZERO"]);
function specialModelName(value) {
 let name = String(value || "").trim().toUpperCase();
 if (name === "TENPENNY") name = "TENPEN";
 if (name === "BIG_SMOKE" || name === "BIG SMOKE") name = "BIGSMOKE";
 return specialNames.has(name) ? name : "";
}
function virtualSpecialName(id) { return ({312:"KENDL",313:"TRUTH",314:"MACCER",315:"EMMET",316:"TBONE",317:"JETHRO",318:"ZERO",319:"ANDRE"})[id] || ""; }
function isSpecialNetworkModel(id) { return (id >= 290 && id <= 299) || (id >= 312 && id <= 319); }

function fireAnimationFields(group, anim) { const g = Number(group), a = Number(anim); return Number.isInteger(g) && g > 0 && g <= 0xFFFF && (a === 224 || a === 225) ? `|${g}|${a}` : ""; }
function weapon(value) { const n = Number(value); return Number.isInteger(n) && (n >= 0 && n <= 0x2E || n === 0x3A) ? n : 0; }
function move(value) { const n = Number(value); return Number.isInteger(n) && n >= 1 && n <= 7 ? n : 1; }
function color(value) { const n = Number(value); return Number.isInteger(n) && n >= 0 && n <= 7 ? n : 0; }
function send(socket, line) { if (!socket.destroyed) socket.write(line); }
function broadcast(room, line, exceptId = 0) {
 for (const player of room.values()) if (player.id !== exceptId) send(player.socket, line);
}



function snapshot(player) {
 const safeName = String(player.name || "CJ").replace(/[|\r\n]/g, "_").slice(0, 14);
 return `PLAYER|${player.id}|${player.modelId}|${player.animationState}|${player.x.toFixed(3)}|${player.y.toFixed(3)}|${player.z.toFixed(3)}|${player.heading.toFixed(3)}|${player.health}|${player.armour}|${player.weapon}|${player.vehicleId || 0}|${player.vehicleSeat || 0}|${safeName}|${player.color}|${player.specialModelName || ""}|${player.areaCode || 0}|${player.wanted || 0}|${player.vx || 0}|${player.vy || 0}|${player.vz || 0}\n`;
}
function clothesPacket(playerId, clothes) {
 if (!clothes) return "";
 return `CLOTHES|${playerId}|${clothes.modelKeys.join("|")}|${clothes.textureKeys.join("|")}|${clothes.fatStat.toFixed(5)}|${clothes.muscleStat.toFixed(5)}\n`;
}
function parseClothes(parts, playerId) {
 if (parts.length !== 31 || Number(parts[0]) !== playerId) return null;
 const modelKeys = parts.slice(1, 11).map(Number);
 const textureKeys = parts.slice(11, 29).map(Number);
 const fatStat = Number(parts[29]), muscleStat = Number(parts[30]);
 if (![...modelKeys, ...textureKeys].every(v => Number.isInteger(v) && v >= 0 && v <= 0xFFFFFFFF) ||
     !Number.isFinite(fatStat) || !Number.isFinite(muscleStat) || Math.abs(fatStat) > 1000 || Math.abs(muscleStat) > 1000) return null;
 return { modelKeys, textureKeys, fatStat, muscleStat };
}
function animationPacket(player) {
 const a = player.animation || {};
 return `ANIM2|${player.id}|${a.sequence >>> 0}|${a.locomotion}|${a.action}|${a.flags}|${a.weaponState}|${a.animGroup}|${a.phase.toFixed(4)}|${a.blend.toFixed(4)}|${a.aimYaw.toFixed(4)}|${a.aimPitch.toFixed(4)}\n`;
}
function associationPacket(player) {
 const a = player.associations || [];
 return `ANIM4|${player.id}|${a.length}${a.map(x => `|${x.group}|${x.animationId}|${x.flags}|${x.blend.toFixed(5)}|${x.blendDelta.toFixed(5)}|${x.currentTime.toFixed(5)}|${x.speed.toFixed(5)}|${x.timeStep.toFixed(5)}|${x.hierarchyHash || 0}`).join("")}\n`;
}
function parseAssociations(parts) {
 if (parts.length < 2) return null;
 const id = Number(parts[0]), count = Number(parts[1]);
 if (!Number.isInteger(id) || id <= 0 || !Number.isInteger(count) || count < 0 || count > 64 || parts.length !== 2 + count * 8) return null;
 const out = [];
 for (let i = 0; i < count; i++) {
 const o = 2 + i * 8, n = parts.slice(o, o + 8).map(Number);
 if (!n.every(Number.isFinite) || !Number.isInteger(n[0]) || n[0] < 0 || n[0] > 65535 || !Number.isInteger(n[1]) || n[1] < -32768 || n[1] > 32767 || !Number.isInteger(n[2]) || n[2] < 0 || n[2] > 65535 || Math.abs(n[3]) > 100 || Math.abs(n[4]) > 100 || Math.abs(n[5]) > 100000 || Math.abs(n[6]) > 100 || Math.abs(n[7]) > 100) return null;
 out.push({ group: n[0], animationId: n[1], flags: n[2], blend: n[3], blendDelta: n[4], currentTime: n[5], speed: n[6], timeStep: n[7] });
 }
 return { id, associations: out };
}
function parseAssociations4(parts) {
 if (parts.length < 2) return null;
 const id = Number(parts[0]), count = Number(parts[1]);
 if (!Number.isInteger(id) || id <= 0 || !Number.isInteger(count) || count < 0 || count > 64 || parts.length !== 2 + count * 9) return null;
 const out = [];
 for (let i = 0; i < count; i++) {
 const o = 2 + i * 9, n = parts.slice(o, o + 9).map(Number);
 if (!n.every(Number.isFinite) || !Number.isInteger(n[0]) || n[0] < 0 || n[0] > 65535 || !Number.isInteger(n[1]) || n[1] < -32768 || n[1] > 32767 || !Number.isInteger(n[2]) || n[2] < 0 || n[2] > 65535 || !Number.isInteger(n[8]) || n[8] < 0 || n[8] > 0xFFFFFFFF || Math.abs(n[3]) > 100 || Math.abs(n[4]) > 100 || Math.abs(n[5]) > 100000 || Math.abs(n[6]) > 100 || Math.abs(n[7]) > 100) return null;
 out.push({ group: n[0], animationId: n[1], flags: n[2], blend: n[3], blendDelta: n[4], currentTime: n[5], speed: n[6], timeStep: n[7], hierarchyHash: n[8] });
 }
 return { id, associations: out };
}

function parseAnimation(parts) {
 if (parts.length !== 11) return null;
 const n = parts.map(Number);
 if (!n.every(Number.isFinite)) return null;
 const [id, sequence, locomotion, action, flags, weaponState, animGroup, phase, blend, aimYaw, aimPitch] = n;
 if (!Number.isInteger(sequence) || sequence < 0 || sequence > 0xFFFFFFFF || !Number.isInteger(locomotion) || locomotion < 1 || locomotion > 7 ||
 !Number.isInteger(action) || action < 0 || action > 17 || !Number.isInteger(flags) || flags < 0 || flags > 0xFFFF ||
 !Number.isInteger(weaponState) || weaponState < 0 || weaponState > 255 || !Number.isInteger(animGroup) || animGroup < 0 || animGroup > 0xFFFF ||
 phase < 0 || phase > 1 || blend < 0 || blend > 1 || Math.abs(aimYaw) > 10000 || Math.abs(aimPitch) > 10000) return null;
 return { id, sequence, locomotion, action, flags, weaponState, animGroup, phase, blend, aimYaw, aimPitch };
}



function npcSnapshot(n) {
 return `NPC_NET|${n.id}|${n.ownerId}|${n.localRef}|${n.modelId}|${n.x.toFixed(3)}|${n.y.toFixed(3)}|${n.z.toFixed(3)}|${n.heading.toFixed(3)}|${n.health}|${n.armour}|${n.weapon}|${n.moveState}|${n.vehicleId || 0}|${n.vehicleSeat || 0}|${n.specialModelName || ""}\n`;
}
function npcVitals(n) { return `NPC_VITALS|${n.id}|${n.ownerId}|${n.localRef}|${n.health}|${n.armour}\n`; }
function npcMotionPacket(n) { return `NPC_MOTION|${n.id}|${n.vx || 0}|${n.vy || 0}|${n.vz || 0}|${n.currentRotation || 0}|${n.aimingRotation || 0}|${n.lookingDirection || 0}|${n.moveState || 1}|${n.fightingStyle ?? 4}\n`; }
function npcAimPacket(n) { return `NPC_AIM|${n.id}|${n.aiming ? 1 : 0}|${n.firing ? 1 : 0}|${Number(n.aimX) || 0}|${Number(n.aimY) || 0}|${Number(n.aimZ) || 0}\n`; }
function npcAssociationsPacket(n) {
 const a = Array.isArray(n.associations) ? n.associations.slice(0, 32) : [];
 return `NPC_ANIM4|${n.id}|${a.length}` + a.map(x => `|${x.group}|${x.animationId}|${x.flags}|${x.blend}|${x.blendDelta}|${x.currentTime}|${x.speed}|${x.timeStep}|${x.hierarchyHash}`).join("") + "\n";
}
function vehicleKeyFor(playerId, localKey) { return `${playerId}:${localKey}`; }
function vehicleSnapshot(v) {
 return `VEHICLE|${v.id}|${v.modelId}|${v.ownerId}|${v.driverSeat}|${v.x.toFixed(3)}|${v.y.toFixed(3)}|${v.z.toFixed(3)}|${v.heading.toFixed(3)}|${v.vx.toFixed(4)}|${v.vy.toFixed(4)}|${v.vz.toFixed(4)}|${v.health.toFixed(1)}|${v.primaryColor}|${v.secondaryColor}|${v.engineOn}|${v.lightsOn}|${v.sirenOn}|${v.doorLock}|${v.dirt.toFixed(3)}|${v.tyreStatus}|${v.doorStatus}|${v.lightStatus}|${v.panelStatus}|${v.trailerId}|${v.rightX.toFixed(5)}|${v.rightY.toFixed(5)}|${v.rightZ.toFixed(5)}|${v.upX.toFixed(5)}|${v.upY.toFixed(5)}|${v.upZ.toFixed(5)}|${v.atX.toFixed(5)}|${v.atY.toFixed(5)}|${v.atZ.toFixed(5)}|${v.steering || 0}|${v.gasPedal || 0}|${v.brakePedal || 0}|${v.miscComponentAngle || 0}|${v.turretVertical || 0}|${v.turretHorizontal || 0}|${v.turnVx || 0}|${v.turnVy || 0}|${v.turnVz || 0}\n`;
}
function vehicleLookPacket(v) {
 const upgrades = Array.isArray(v.upgrades) ? v.upgrades.slice(0, 15) : Array(15).fill(-1);
 while (upgrades.length < 15) upgrades.push(-1);
 return `VEHICLE_LOOK|${v.id}|${Number.isInteger(v.paintjob) ? v.paintjob : -1}|${upgrades.join("|")}\n`;
}
function vehicleExtendedPacket(v) {
 const e = v.extended || {};
 return `VEHICLE_EX|${v.id}|${e.tertiaryColor || 0}|${e.quaternaryColor || 0}|${Number.isInteger(e.extraA) ? e.extraA : -1}|${Number.isInteger(e.extraB) ? e.extraB : -1}|${e.handbrake ? 1 : 0}|${e.landingGearValid ? 1 : 0}|${Number(e.landingGear) || 0}|${e.searchLightValid ? 1 : 0}|${Number(e.searchLightX) || 0}|${Number(e.searchLightY) || 0}|${Number(e.searchLightZ) || 0}|${Number(e.searchLightIntensity) || 0}|${e.searchLightEnabled ? 1 : 0}|${e.bikeLeanValid ? 1 : 0}|${Number(e.bikeLean) || 0}|${Number(e.bikeLeanForward) || 0}|${e.bmxValid ? 1 : 0}|${Number(e.bmxSprintLean) || 0}|${Number(e.bmxPedallingCharge) || 0}\n`;
}
function allocateVehicleId(room) {
 room.vehicles ??= new Map();
 for (let i = 0; i < 65535; i++) {
  const id = nextVehicleId++;
  if (nextVehicleId > 65535) nextVehicleId = 1;
  if (id && !room.vehicles.has(id)) return id;
 }
 return 0;
}
function removeVehicle(room, vehicle, exceptId = 0) {
 if (!vehicle) return;
 room.vehicles.delete(vehicle.id);
 if (room.vehicleKeys) for (const [key,id] of room.vehicleKeys) if (id===vehicle.id) room.vehicleKeys.delete(key);
 broadcast(room, `VEHICLE_REMOVE|${vehicle.id}\n`, exceptId);
}
function reconcileNpcHealth(n) {
 const now = Date.now();
 let health = 100, armour = 100, haveFresh = false;
 for (const report of n.healthReports.values()) {
 if (!report.receivedAtMs || now - report.receivedAtMs > 1000) continue;
 if (!haveFresh) { health = report.health; armour = report.armour; haveFresh = true; }
 else { health = Math.min(health, report.health); armour = Math.min(armour, report.armour); }
 }
 if (!haveFresh) return false;
 const changed = health !== n.health || armour !== n.armour;
 n.health = health;
 n.armour = armour;
 return changed;
}
function playerVitals(player) { return `PLAYER_VITALS|${player.id}|${player.health}|${player.armour}\n`; }
function reconcilePlayerVitals(player) {
 const now = Date.now();
 let health = 100, armour = 100, haveFresh = false;
 for (const report of player.healthReports.values()) {
 if (!report.receivedAtMs || now - report.receivedAtMs > 1000) continue;
 if (!haveFresh) { health = report.health; armour = report.armour; haveFresh = true; }
 else { health = Math.min(health, report.health); armour = Math.min(armour, report.armour); }
 }
 if (!haveFresh) return false;
 const changed = health !== player.health || armour !== player.armour;
 player.health = health;
 player.armour = armour;
 return changed;
}
function removePlayer(player, reason = "kicked") {
 if (!player || player.removed) return;
 player.removed = true;
 if (player.room.npcs) for (const [key,n] of player.room.npcs) if (n.ownerId===player.id) { broadcast(player.room, `NPC_REMOVE|${n.id}\n`, player.id); player.room.npcs.delete(key); }
 if (player.room.vehicles) for (const vehicle of [...player.room.vehicles.values()]) if (vehicle.ownerId===player.id) removeVehicle(player.room, vehicle, player.id);
 if (player.room.vehicleKeys) for (const key of [...player.room.vehicleKeys.keys()]) if (key.startsWith(`${player.id}:`)) player.room.vehicleKeys.delete(key);
 for (const other of player.room.values()) {
   if (other.healthReports.delete(player.id) && reconcilePlayerVitals(other))
     broadcast(player.room, playerVitals(other));
 }
 if (player.room.npcs) for (const n of player.room.npcs.values()) {
   if (n.healthReports?.delete(player.id) && reconcileNpcHealth(n)) {
     broadcast(player.room, npcSnapshot(n), n.ownerId);
     broadcast(player.room, npcVitals(n));
   }
 }
 player.room.delete(player.id);
 announceTimeAuthority(player.room);
 playersById.delete(player.id);
 player.socket.destroy();
 console.log(`PLAYER_REMOVE id=${player.id} reason=${reason}`);
 broadcast(player.room, `REMOVE|${player.id}\n`);
}
function adminCommand(line) {
 const parts = line.trim().split(/\s+/);
 const command = (parts[0] || "").toLowerCase();
 const id = Number(parts[1]);
 if (command === "/kick" && Number.isInteger(id)) {
 const player = playersById.get(id);
 if (!player) return console.log(`ADMIN player ${id} was not found`);
 removePlayer(player, `Player ${id} was kicked by the server`);
 return;
 }
 if (command === "/timeout" && Number.isInteger(id)) {
 const seconds = Math.max(1, Math.min(86400, Number(parts[2] || 60)));
 const player = playersById.get(id);
 if (!player) return console.log(`ADMIN player ${id} was not found`);
 blockedAddresses.set(player.address, Date.now() + seconds * 1000);
 removePlayer(player, `Player ${id} timed out for ${seconds}s`);
 return;
 }
 if (command === "/players") {
 console.log([...playersById.values()].map(p => `${p.id}:${p.name}`).join(" ") || "No players connected");
 return;
 }
 if (command === "/help") {
 console.log("Commands: /players, /kick <playerid>, /timeout <playerid> [seconds]");
 return;
 }
 if (line.trim()) console.log("Unknown command. Use /help");
}

const server = net.createServer(socket => {
 let player;
 let buffer = "";
 socket.setNoDelay(true);
 const address = socket.remoteAddress || "unknown";

 socket.on("data", data => {
 buffer += data.toString();
 if (buffer.length > 65536) { socket.destroy(); return; }
 let end;
 while ((end = buffer.indexOf("\n")) !== -1) {
 const line = buffer.slice(0, end).trim();
 buffer = buffer.slice(end + 1);
 if (!line || line.length > 4096) continue;
 const [type, ...parts] = line.split("|");

 if (type === "JOIN" && !player) {
 const blockedUntil = blockedAddresses.get(address) || 0;
 if (blockedUntil > Date.now()) { socket.destroy(); continue; }
 if (blockedUntil) blockedAddresses.delete(address);
	 const room = roomFor("default"); room.vehicles ??= new Map(); room.vehicleKeys ??= new Map();
 player = { id: nextId++, name: (parts[0] || "CJ").slice(0, 14), color: color(parts[1]),
 x: 0, y: 0, z: 3, heading: 0, vx: 0, vy: 0, vz: 0, areaCode: 0, wanted: 0, health: 100, armour: 0, weapon: 0,
 modelId: 7, specialModelName: "", animationState: 1, vehicleId: 0, vehicleSeat: 0,
 socket, address, room, removed: false,
 healthReports: new Map(), vitalsInitialized: false, healthReportResumeAtMs: 0,
 animation: { sequence: 0, locomotion: 1, action: 1, flags: 0x30, weaponState: 0, animGroup: 0, phase: 0, blend: 1, aimYaw: 0, aimPitch: 0 }, associations: [], clothes: null,
 input: { fire: 0, jump: 0, enterVehicle: 0, moveX: 0, moveY: 0, aimX: 0, aimY: 0 } };
 room.set(player.id, player);
 playersById.set(player.id, player);
 send(socket, `WELCOME|${player.id}\n`);
	 for (const other of room.values()) if (other.id !== player.id) { send(socket, snapshot(other)); if (other.clothes) send(socket, clothesPacket(other.id, other.clothes)); send(socket, animationPacket(other)); send(socket, associationPacket(other)); }
	 if (room.vehicles) for (const v of room.vehicles.values()) { send(socket, vehicleSnapshot(v)); send(socket, vehicleLookPacket(v)); send(socket, vehicleExtendedPacket(v)); }
	 if (room.npcs) for (const n of room.npcs.values()) if (n.ownerId !== player.id) { send(socket,npcSnapshot(n)); send(socket,npcMotionPacket(n)); send(socket,npcAimPacket(n)); send(socket,npcAssociationsPacket(n)); }


 broadcast(room, `NAME|${player.id}|${String(player.name).replace(/[|\r\n]/g, "_").slice(0, 14)}|${player.color}\n`, player.id);
 announceTimeAuthority(room);
	 if(room.clock) send(socket,timePacket(room.clock));
	 console.log(`PLAYER_JOIN id=${player.id} name=${player.name}`);
	 continue;
 }
 if (!player) continue;

 if (type === "TIME" && parts.length === 3) {
 const hour=Number(parts[0]), minute=Number(parts[1]), day=Number(parts[2]);
 if(leaderFor(player.room)?.id===player.id&&Number.isInteger(hour)&&hour>=0&&hour<=23&&Number.isInteger(minute)&&minute>=0&&minute<=59&&Number.isInteger(day)&&day>=1&&day<=31){
 player.room.clock={hour,minute,day};
 broadcast(player.room,timePacket(player.room.clock),player.id);
 }
 } else if (type === "INPUT") {
 const oldWeapon = player.weapon;
 const oldColor = player.color;
 const oldModelId = player.modelId;
 const oldSpecialName = player.specialModelName;
 player.x = clamp(num(parts[0], player.x)); player.y = clamp(num(parts[1], player.y)); player.z = clamp(num(parts[2], player.z));
 player.heading = num(parts[3], player.heading);

 const incomingArea = Number(parts[21]);
 if (Number.isInteger(incomingArea) && incomingArea >= 0 && incomingArea <= 255) player.areaCode = incomingArea;
 const incomingWanted = Number(parts[22]);
 if (parts.length > 22 && Number.isInteger(incomingWanted)) player.wanted = Math.max(0, Math.min(6, incomingWanted));
 if (parts.length >= 26) {
   const velocity = parts.slice(23, 26).map(Number);
   if (velocity.every(Number.isFinite)) {
     player.vx = Math.max(-150, Math.min(150, velocity[0]));
     player.vy = Math.max(-150, Math.min(150, velocity[1]));
     player.vz = Math.max(-150, Math.min(150, velocity[2]));
   }
 }
 player.input.fire = num(parts[4], 0) ? 1 : 0; player.input.jump = num(parts[5], 0) ? 1 : 0; player.input.enterVehicle = num(parts[6], 0) ? 1 : 0;
 player.input.moveX = num(parts[7], 0); player.input.moveY = num(parts[8], 0); player.input.aimX = num(parts[9], 0); player.input.aimY = num(parts[10], 0);
 player.modelId = model(parts[11]); player.animationState = move(parts[12]);
	 player.specialModelName = isSpecialNetworkModel(player.modelId) && parts.length > 17
	 ? specialModelName(parts[17]) : "";
	 if (!player.specialModelName && player.modelId >= 312 && player.modelId <= 319) player.specialModelName = virtualSpecialName(player.modelId);
	 const claimedVehicleId = Number(parts[18] || 0), claimedSeat = Number(parts[19] || 0), localVehicleKey = Number(parts[20] || 0);
	 let resolvedVehicleId = 0;
	 if (player.room.vehicles && Number.isInteger(claimedVehicleId) && claimedVehicleId > 0 && player.room.vehicles.has(claimedVehicleId)) resolvedVehicleId = claimedVehicleId;
	 if (!resolvedVehicleId && player.room.vehicleKeys && Number.isInteger(localVehicleKey) && localVehicleKey > 0) resolvedVehicleId = player.room.vehicleKeys.get(vehicleKeyFor(player.id, localVehicleKey)) || 0;
	 player.vehicleId = resolvedVehicleId;
	 player.vehicleSeat = resolvedVehicleId && Number.isInteger(claimedSeat) && claimedSeat >= 0 && claimedSeat <= 8 ? claimedSeat : 0;
	 const ownerHealth = parts.length > 14 ? Math.max(0, Math.min(100, Math.trunc(num(parts[14], player.health)))) : player.health;
 const ownerArmour = parts.length > 15 ? Math.max(0, Math.min(100, Math.trunc(num(parts[15], player.armour)))) : player.armour;
 const previousOwnerReport = player.healthReports.get(player.id);
 const ownerRespawned = player.vitalsInitialized && parts.length > 15 && previousOwnerReport &&
 previousOwnerReport.health === 0 && ownerHealth > 0;
 if (!player.vitalsInitialized || ownerRespawned) {
 if (ownerRespawned) {
 player.healthReports.clear();
 player.healthReportResumeAtMs = Date.now() + 1000;
 }
 player.health = ownerHealth;
 player.armour = ownerArmour;
 player.vitalsInitialized = true;
 }
 player.healthReports.set(player.id, { health: ownerHealth, armour: ownerArmour, receivedAtMs: Date.now() });
 if (ownerRespawned || reconcilePlayerVitals(player)) broadcast(player.room, playerVitals(player));
 if (parts.length > 13) player.weapon = weapon(parts[13]);
 if (parts.length > 16) player.color = color(parts[16]);
 if (player.weapon !== oldWeapon || player.color !== oldColor ||
 player.modelId !== oldModelId || player.specialModelName !== oldSpecialName) {
 console.log(`WEAPON_SERVER_RECEIVE id=${player.id} weapon=${player.weapon}`);
 console.log(`WEAPON_SERVER_STORED id=${player.id} weapon=${player.weapon}`);
 const packet = snapshot(player);
 for (const other of player.room.values()) if (other.id !== player.id && !other.socket.destroyed) {
 console.log(`WEAPON_SERVER_BROADCAST from=${player.id} to=${other.id} weapon=${player.weapon}`);
 send(other.socket, packet);
 }
 }
 } else if (type === "CLOTHES") {
 const clothes = parseClothes(parts, player.id);
 if (clothes) {
  player.clothes = clothes;
  broadcast(player.room, clothesPacket(player.id, clothes), player.id);
 }
 } else if (type === "ANIM3") {
 const parsed = parseAssociations(parts);
 if (parsed && parsed.id === player.id) { player.associations = parsed.associations; broadcast(player.room, associationPacket(player), player.id); }
 } else if (type === "ANIM4") {
 const parsed = parseAssociations4(parts);
 if (parsed && parsed.id === player.id) { player.associations = parsed.associations; broadcast(player.room, associationPacket(player), player.id); }
 } else if (type === "ANIM2") {
 const animation = parseAnimation(parts);
 if (animation && animation.id === player.id && animation.sequence >= player.animation.sequence) {
 player.animation = animation;
 player.animationState = animation.locomotion;
 broadcast(player.room, animationPacket(player), player.id);
 }
 } else if (type === "PLAYER_HEALTH" && parts.length === 3) {
 const targetId = Number(parts[0]), health = Number(parts[1]), armour = Number(parts[2]);
 const target = player.room.get(targetId);
 if (Number.isInteger(targetId) && targetId > 0 && Number.isInteger(health) && health >= 0 && health <= 100 &&
 Number.isInteger(armour) && armour >= 0 && armour <= 100 && target && target.vitalsInitialized &&
 target.id !== player.id && Date.now() >= target.healthReportResumeAtMs) {
 target.healthReports.set(player.id, { health, armour, receivedAtMs: Date.now() });
 if (reconcilePlayerVitals(target)) broadcast(player.room, playerVitals(target));
 }
 } else if (type === "PLAYER_HIT" && parts.length === 7) {
 const targetId = Number(parts[0]), w = Number(parts[1]), damage = Number(parts[2]);
 const piece = Number(parts[3]), direction = Number(parts[4]);
 const fightSet = Number(parts[5]), fightMove = Number(parts[6]);
 const target = player.room.get(targetId);
 const validFight = (fightSet === 255 && fightMove === 255) ||
   (Number.isInteger(fightSet) && fightSet >= 4 && fightSet <= 16 && Number.isInteger(fightMove) && fightMove >= 0 && fightMove <= 4);
 if (target && target.id !== player.id && target.vitalsInitialized && target.health > 0 &&
     Number.isInteger(targetId) && targetId > 0 && Number.isInteger(w) && w >= 0 && w <= 15 &&
     Number.isInteger(damage) && damage > 0 && damage <= 10000 &&
     Number.isInteger(piece) && piece >= 0 && piece <= 9 &&
     Number.isInteger(direction) && direction >= 0 && direction <= 3 && validFight &&
     Date.now() >= target.healthReportResumeAtMs) {
   send(target.socket, `PLAYER_HIT|${player.id}|${w}|${damage}|${piece}|${direction}|${fightSet}|${fightMove}\n`);
 }
 } else if (type === "BONES2" && parts.length >= 2) {

 const count = Number(parts[1]);
 if (Number.isInteger(count) && count >= 0 && count <= 19 && parts.length === 2 + count * 5)
 broadcast(player.room, `BONES2|${player.id}|${count}|${parts.slice(2).join("|")}\n`, player.id);
	 } else if (type === "VEHICLE_STATE" && (parts.length === 32 || parts.length === 38 || parts.length === 41)) {
	 const key=Number(parts[0]), modelId=Number(parts[1]), n=parts.slice(2).map(Number);
	 if (Number.isInteger(key) && key>0 && key<=0xFFFFFFFF && Number.isInteger(modelId) && modelId>=400 && modelId<20000 && n.every(Number.isFinite) && (n.length===30 || n.length===36 || n.length===39) && n.every(v=>Math.abs(v)<1000000)) {
		 player.room.vehicles ??= new Map(); player.room.vehicleKeys ??= new Map();
		 const sourceKey=vehicleKeyFor(player.id,key), mapped=player.room.vehicleKeys.get(sourceKey);
		 if (mapped === 0) return;
		 let vehicle = mapped ? player.room.vehicles.get(mapped) : null;
		 if (mapped && !vehicle) player.room.vehicleKeys.delete(sourceKey);
		 if (vehicle && vehicle.ownerId !== player.id) return;
		 if (vehicle && vehicle.modelId !== modelId) { removeVehicle(player.room,vehicle,player.id); vehicle=null; }
		 if (!vehicle) {
			 const id=allocateVehicleId(player.room); if(!id) return;
			 vehicle={id,ownerId:player.id,localKey:key,paintjob:-1,upgrades:Array(15).fill(-1),extended:{}}; player.room.vehicleKeys.set(sourceKey,id);
			 player.room.vehicles.set(id,vehicle);
		 }
		 const clampNum=(v,lo,hi)=>Math.max(lo,Math.min(hi,v));
		 vehicle.modelId=modelId; vehicle.x=clampNum(n[0],-3000,3000); vehicle.y=clampNum(n[1],-3000,3000); vehicle.z=clampNum(n[2],-1000,10000); vehicle.heading=n[3]; vehicle.vx=clampNum(n[4],-150,150); vehicle.vy=clampNum(n[5],-150,150); vehicle.vz=clampNum(n[6],-150,150); vehicle.health=clampNum(n[7],0,1000); vehicle.driverSeat=Math.trunc(clampNum(n[8],0,7));
		 vehicle.primaryColor=Math.trunc(clampNum(n[9],0,255)); vehicle.secondaryColor=Math.trunc(clampNum(n[10],0,255)); vehicle.engineOn=n[11]?1:0; vehicle.lightsOn=n[12]?1:0; vehicle.sirenOn=n[13]?1:0; vehicle.doorLock=Math.trunc(clampNum(n[14],0,7)); vehicle.dirt=clampNum(n[15],0,15); vehicle.tyreStatus=Math.trunc(clampNum(n[16],0,255)); vehicle.doorStatus=Math.trunc(clampNum(n[17],0,0xFFFFFF)); vehicle.lightStatus=Math.trunc(clampNum(n[18],0,0xFFFFFFFF)); vehicle.panelStatus=Math.trunc(clampNum(n[19],0,0xFFFFFFFF)); vehicle.trailerId=Math.trunc(clampNum(n[20],0,65535));
		 [vehicle.rightX,vehicle.rightY,vehicle.rightZ,vehicle.upX,vehicle.upY,vehicle.upZ,vehicle.atX,vehicle.atY,vehicle.atZ]=n.slice(21,30).map(v=>clampNum(v,-2,2));
         vehicle.steering=parts.length>=38?clampNum(n[30],-1,1):0; vehicle.gasPedal=parts.length>=38?clampNum(n[31],-1,1):0; vehicle.brakePedal=parts.length>=38?clampNum(n[32],0,1):0; vehicle.miscComponentAngle=parts.length>=38?Math.trunc(clampNum(n[33],0,65535)):0; vehicle.turretVertical=parts.length>=38?clampNum(n[34],-10,10):0; vehicle.turretHorizontal=parts.length>=38?clampNum(n[35],-10,10):0;
         vehicle.turnVx=parts.length===41?clampNum(n[36],-20,20):0; vehicle.turnVy=parts.length===41?clampNum(n[37],-20,20):0; vehicle.turnVz=parts.length===41?clampNum(n[38],-20,20):0;
		 vehicle.lastSeen=Date.now();
		 if (!mapped) send(socket,`VEHICLE_ID|${key}|${vehicle.id}\n`);
		 broadcast(player.room,vehicleSnapshot(vehicle),player.id);
	 }
     } else if (type === "VEHICLE_LOOK" && parts.length === 17) {
         const key = Number(parts[0]);
         const paintjob = Number(parts[1]);
         const upgrades = parts.slice(2).map(Number);
         if (Number.isInteger(key) && key > 0 && key <= 0xFFFFFFFF &&
             Number.isInteger(paintjob) && paintjob >= -1 && paintjob <= 3 &&
             upgrades.length === 15 && upgrades.every(v => Number.isInteger(v) && v >= -1 && v < 20000)) {
             const id = player.room.vehicleKeys?.get(vehicleKeyFor(player.id, key));
             const vehicle = id ? player.room.vehicles?.get(id) : null;
             if (vehicle && vehicle.ownerId === player.id) {
                 vehicle.paintjob = paintjob;
                 vehicle.upgrades = upgrades;
                 broadcast(player.room, vehicleLookPacket(vehicle), player.id);
             }
         }
     } else if (type === "VEHICLE_EX" && parts.length === 20) {
         const key = Number(parts[0]);
         const n = parts.slice(1).map(Number);
         if (Number.isInteger(key) && key > 0 && key <= 0xFFFFFFFF && n.every(Number.isFinite) &&
             n.every(v => Math.abs(v) < 1000000) &&
             [0,1,2,3,4,5,7,12,13,16].every(i => Number.isInteger(n[i])) &&
             n[0] >= 0 && n[0] <= 255 && n[1] >= 0 && n[1] <= 255 &&
             n[2] >= -128 && n[2] <= 127 && n[3] >= -128 && n[3] <= 127 &&
             (n[4] === 0 || n[4] === 1) && (n[5] === 0 || n[5] === 1) &&
             (n[7] === 0 || n[7] === 1) && (n[12] === 0 || n[12] === 1) &&
             (n[13] === 0 || n[13] === 1) && (n[16] === 0 || n[16] === 1)) {
             const id = player.room.vehicleKeys?.get(vehicleKeyFor(player.id, key));
             const vehicle = id ? player.room.vehicles?.get(id) : null;
             if (vehicle && vehicle.ownerId === player.id) {
                 const clampNum = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
                 vehicle.extended = {
                     tertiaryColor: Math.trunc(n[0]), quaternaryColor: Math.trunc(n[1]),
                     extraA: Math.trunc(n[2]), extraB: Math.trunc(n[3]), handbrake: n[4] ? 1 : 0,
                     landingGearValid: !!n[5], landingGear: clampNum(n[6], 0, 1),
                     searchLightValid: !!n[7], searchLightX: clampNum(n[8], -30000, 30000),
                     searchLightY: clampNum(n[9], -30000, 30000), searchLightZ: clampNum(n[10], -30000, 30000),
                     searchLightIntensity: clampNum(n[11], 0, 100), searchLightEnabled: n[12] ? 1 : 0,
                     bikeLeanValid: !!n[13], bikeLean: clampNum(n[14], -10, 10),
                     bikeLeanForward: clampNum(n[15], -10, 10), bmxValid: !!n[16],
                     bmxSprintLean: clampNum(n[17], -10, 10), bmxPedallingCharge: clampNum(n[18], 0, 100)
                 };
                 broadcast(player.room, vehicleExtendedPacket(vehicle), player.id);
             }
         }
	 } else if (type === "VEHICLE_CLAIM" && parts.length === 3) {
	 const id=Number(parts[0]), seat=Number(parts[1]), key=Number(parts[2]);
	 const vehicle=player.room.vehicles?.get(id);
	 if(vehicle&&Number.isInteger(id)&&id>0&&Number.isInteger(seat)&&seat===0&&Number.isInteger(key)&&key>0&&key<=0xFFFFFFFF){
		 const newKey=vehicleKeyFor(player.id,key), existing=player.room.vehicleKeys.get(newKey);
		 if(existing && existing!==id) return;


         const claimNow=Date.now();
         const recentCarjackIntent=Number.isFinite(vehicle.lastDriverEnterIntentAt) &&
           claimNow>=vehicle.lastDriverEnterIntentAt && claimNow-vehicle.lastDriverEnterIntentAt<=8000;
         vehicle.carjackGraceUntil=recentCarjackIntent?claimNow+8000:0;
         vehicle.lastDriverEnterIntentAt=0;
         if(!recentCarjackIntent && player.room.npcs) for (const [npcKey,n] of [...player.room.npcs.entries()]) {
           if (n.vehicleId === id && n.vehicleSeat === 0) {
             player.room.npcs.delete(npcKey);
             broadcast(player.room, `NPC_REMOVE|${n.id}\n`);
           }
         }
		 vehicle.ownerId=player.id; vehicle.localKey=key; vehicle.driverSeat=0; player.room.vehicleKeys.set(newKey,id); player.vehicleId=id; player.vehicleSeat=0;
		 send(socket,`VEHICLE_ID|${key}|${id}\n`); broadcast(player.room,vehicleSnapshot(vehicle),player.id);
	 }
	 } else if (type === "VEHICLE_ENTER" && parts.length === 2) {
	 const id=Number(parts[0]), seat=Number(parts[1]);
	 const vehicle=player.room.vehicles?.get(id);
	 if(vehicle&&Number.isInteger(seat)&&seat>=0&&seat<=8){
	   if(seat===0) vehicle.lastDriverEnterIntentAt=Date.now();
	   broadcast(player.room,`VEHICLE_ENTER|${player.id}|${id}|${seat}\n`,player.id);
	 }
	 } else if (type === "VEHICLE_REMOVE_KEY" && parts.length === 1) {
	 const key=Number(parts[0]), id=player.room.vehicleKeys?.get(vehicleKeyFor(player.id,key));
	 const vehicle=id&&player.room.vehicles?.get(id);
	 if(Number.isInteger(key)&&key>0&&vehicle&&vehicle.ownerId===player.id) removeVehicle(player.room,vehicle,player.id);
	 } else if (type === "NPC_STATE" && parts.length >= 10 && parts.length <= 14) {



 const ref=Number(parts[0]), modelId=Number(parts[1]); const vals=parts.slice(2,10).map(Number);
 const weaponId=Number(parts[8]), moveState=Number(parts[9]);
 const hasVehicle = parts.length >= 13;
 const vehicleKey = hasVehicle ? Number(parts[10]) : 0;
 const vehicleSeat = hasVehicle ? Number(parts[11]) : 0;
	 const specialName = (parts.length === 11 || parts.length === 13 || parts.length === 14) && isSpecialNetworkModel(modelId)
	 ? (specialModelName(parts.length===11?parts[10]:parts[12]) || virtualSpecialName(modelId)) : "";
 if(Number.isInteger(ref)&&ref>0&&Number.isInteger(modelId)&&modelId>=0&&modelId<20000&&vals.length===8&&vals.every(Number.isFinite)&&Number.isInteger(weaponId)&&weaponId>=0&&weaponId<=0x3A&&Number.isInteger(moveState)&&moveState>=1&&moveState<=7){
 if(!player.room.npcs) player.room.npcs=new Map();
 const key=`${player.id}:${ref}`, incomingHealth=Math.max(0,Math.min(100,Math.trunc(vals[4]))), incomingArmour=Math.max(0,Math.min(100,Math.trunc(vals[5])));
	 const directVehicleId = parts.length >= 14 ? Number(parts[13]) : 0;
	 const npcVehicleId = Number.isInteger(directVehicleId) && directVehicleId > 0 && directVehicleId <= 65535 && player.room.vehicles?.has(directVehicleId) ? directVehicleId : (Number.isInteger(vehicleKey) && vehicleKey > 0 ? (player.room.vehicleKeys?.get(vehicleKeyFor(player.id, vehicleKey)) || 0) : 0);
 const npcVehicleSeat = Number.isFinite(vehicleSeat) ? Math.max(0, Math.min(8, Math.trunc(vehicleSeat))) : 0;


     if (npcVehicleId && npcVehicleSeat === 0 && player.room.vehicles?.get(npcVehicleId)?.ownerId !== player.id) {
       const vehicle=player.room.vehicles?.get(npcVehicleId);
       if(vehicle && Number.isFinite(vehicle.carjackGraceUntil) && Date.now()<vehicle.carjackGraceUntil) {
         continue;
       }
       const stale = player.room.npcs.get(key);
       if (stale) {
         player.room.npcs.delete(key);
         broadcast(player.room, `NPC_REMOVE|${stale.id}\n`, player.id);
       }
       continue;
     }
 let n=player.room.npcs.get(key);
 if(!n){
 n={id:nextNpcId++,ownerId:player.id,localRef:ref,modelId,specialModelName:specialName,x:vals[0],y:vals[1],z:vals[2],heading:vals[3],health:incomingHealth,armour:incomingArmour,weapon:weaponId,moveState,vehicleId:npcVehicleId,vehicleSeat:npcVehicleSeat,vx:0,vy:0,vz:0,currentRotation:0,aimingRotation:0,lookingDirection:0,fightingStyle:4,aiming:false,firing:false,aimX:0,aimY:0,aimZ:0,associations:[],healthReports:new Map()};
 player.room.npcs.set(key,n);
 } else {
 n.modelId=modelId;n.specialModelName=specialName;n.x=vals[0];n.y=vals[1];n.z=vals[2];n.heading=vals[3];
 n.weapon=weaponId;n.moveState=moveState;n.vehicleId=npcVehicleId;n.vehicleSeat=npcVehicleSeat;
 }
 n.healthReports.set(player.id,{health:incomingHealth,armour:incomingArmour,receivedAtMs:Date.now()});
 const vitalsChanged=reconcileNpcHealth(n);
 broadcast(player.room,npcSnapshot(n),player.id);
 if(vitalsChanged) broadcast(player.room,npcVitals(n));
 }
 } else if (type === "NPC_MOTION" && parts.length === 9) {
 const ref=Number(parts[0]), v=parts.slice(1,7).map(Number), moveState=Number(parts[7]), fightingStyle=Number(parts[8]);
 const n=Number.isInteger(ref)&&ref>0?player.room.npcs?.get(`${player.id}:${ref}`):null;
 if(n&&v.every(Number.isFinite)&&v.every(x=>Math.abs(x)<=100000)&&Number.isInteger(moveState)&&moveState>=0&&moveState<=7&&Number.isInteger(fightingStyle)&&fightingStyle>=0&&fightingStyle<=255){
   [n.vx,n.vy,n.vz,n.currentRotation,n.aimingRotation,n.lookingDirection]=v.map((x,i)=>i<3?Math.max(-100,Math.min(100,x)):x);
   n.moveState=moveState; n.fightingStyle=fightingStyle; broadcast(player.room,npcMotionPacket(n),player.id);
 }
 } else if (type === "NPC_AIM" && parts.length === 6) {
 const ref=Number(parts[0]), aiming=Number(parts[1]), firing=Number(parts[2]), aim=parts.slice(3,6).map(Number);
 const n=Number.isInteger(ref)&&ref>0?player.room.npcs?.get(`${player.id}:${ref}`):null;
 if(n&&(aiming===0||aiming===1)&&(firing===0||firing===1)&&(!firing||aiming)&&aim.every(Number.isFinite)&&aim.every(x=>Math.abs(x)<=100000)){
   n.aiming=aiming===1;n.firing=firing===1;[n.aimX,n.aimY,n.aimZ]=aim;
   broadcast(player.room,npcAimPacket(n),player.id);
 }
 } else if (type === "NPC_ANIM4" && parts.length >= 2) {
 const ref=Number(parts[0]), count=Number(parts[1]), n=Number.isInteger(ref)&&ref>0?player.room.npcs?.get(`${player.id}:${ref}`):null;
 if(n&&Number.isInteger(count)&&count>=0&&count<=32&&parts.length===2+count*9){
   const associations=[]; let valid=true;
   for(let i=0;i<count;i++){
     const o=2+i*9, a={group:Number(parts[o]),animationId:Number(parts[o+1]),flags:Number(parts[o+2]),blend:Number(parts[o+3]),blendDelta:Number(parts[o+4]),currentTime:Number(parts[o+5]),speed:Number(parts[o+6]),timeStep:Number(parts[o+7]),hierarchyHash:Number(parts[o+8])};
     if(!Number.isInteger(a.group)||a.group<0||a.group>65535||!Number.isInteger(a.animationId)||a.animationId< -32768||a.animationId>32767||!Number.isInteger(a.flags)||a.flags<0||a.flags>65535||!Number.isInteger(a.hierarchyHash)||a.hierarchyHash<0||a.hierarchyHash>0xFFFFFFFF||![a.blend,a.blendDelta,a.currentTime,a.speed,a.timeStep].every(Number.isFinite)||Math.abs(a.blend)>100||Math.abs(a.blendDelta)>100||Math.abs(a.currentTime)>100000||Math.abs(a.speed)>100||Math.abs(a.timeStep)>100){valid=false;break;}
     associations.push(a);
   }
   if(valid){n.associations=associations;broadcast(player.room,npcAssociationsPacket(n),player.id);}
 }
 } else if (type === "NPC_HEALTH" && parts.length === 3) {
 const id=Number(parts[0]), health=Number(parts[1]), armour=Number(parts[2]);
 const npcs=player.room.npcs;
 if(Number.isInteger(id)&&id>0&&Number.isInteger(health)&&health>=0&&health<=100&&Number.isInteger(armour)&&armour>=0&&armour<=100&&npcs){
 const npc=[...npcs.values()].find(n=>n.id===id);
 if(npc&&npc.ownerId!==player.id){
 npc.healthReports.set(player.id,{health,armour,receivedAtMs:Date.now()});
 if(reconcileNpcHealth(npc)){
 broadcast(player.room,npcSnapshot(npc),npc.ownerId);
 broadcast(player.room,npcVitals(npc));
 }
 }
 }
 } else if (type === "NPC_REMOVE" && parts.length >= 1) {
 const ref=Number(parts[0]); if(Number.isInteger(ref)&&ref>0&&player.room.npcs){const key=`${player.id}:${ref}`,n=player.room.npcs.get(key);if(n){player.room.npcs.delete(key);broadcast(player.room,`NPC_REMOVE|${n.id}\n`,player.id);}}
 } else if (type === "EXPX" && parts.length === 4) {
  const expType = Number(parts[0]), xyz = parts.slice(1, 4).map(Number);
  if (Number.isInteger(expType) && expType >= 0 && expType <= 12 && xyz.every(Number.isFinite) &&
      Math.abs(xyz[0]) <= 3000 && Math.abs(xyz[1]) <= 3000 && Math.abs(xyz[2]) <= 10000) {
   broadcast(player.room, `EXPX|${player.id}|${expType}|${xyz.map(v => v.toFixed(3)).join("|")}\n`, player.id);
  }
 } else if (type === "SHOTX" && (parts.length === 11 || parts.length === 12)) {
  const kind=Number(parts[0]), w=weapon(parts[1]), hasVehicleId=parts.length===12;
  const vehicleId=hasVehicleId?Number(parts[2]):0, valueStart=hasVehicleId?3:2;
  const values=parts.slice(valueStart,valueStart+7).map(Number); let group=Number(parts[valueStart+7]), anim=Number(parts[valueStart+8]);
  if(Number.isInteger(kind)&&kind>=0&&kind<=2&&Number.isInteger(vehicleId)&&vehicleId>=0&&vehicleId<=0xFFFF&&values.length===7&&values.every(Number.isFinite)&&Math.abs(values[0])<=3000&&Math.abs(values[1])<=3000&&Math.abs(values[2])<=10000&&Math.abs(values[3])<=3000&&Math.abs(values[4])<=3000&&Math.abs(values[5])<=10000&&values[6]>=0&&values[6]<=5){
   if(!(Number.isInteger(group)&&group>0&&group<=0xFFFF&&(anim===224||anim===225))){group=0;anim=0;}
   broadcast(player.room,`SHOTX|${player.id}|${kind}|${w}|${kind===2?vehicleId:0}|${values.map(v=>v.toFixed(3)).join("|")}|${group}|${anim}\n`,player.id);
  }
 } else if (type === "SHOT" && (parts.length === 7 || parts.length === 9)) {
 const w = weapon(parts[0]); const values = parts.slice(1, 7).map(Number); const fireAnim = parts.length === 9 ? fireAnimationFields(parts[7], parts[8]) : "";
 if (values.every(Number.isFinite)) {
  broadcast(player.room, `SHOT|${player.id}|${w}|${values.map(v => v.toFixed(3)).join("|")}${fireAnim}\n`, player.id);
 }
 }
 }
 });

 socket.on("close", () => {
	 if (player && !player.removed) {
	 player.removed = true;
	 if (player.room.npcs) for (const [key,n] of player.room.npcs) if (n.ownerId===player.id) { broadcast(player.room, `NPC_REMOVE|${n.id}\n`, player.id); player.room.npcs.delete(key); }
	 if (player.room.vehicles) for (const vehicle of [...player.room.vehicles.values()]) if (vehicle.ownerId===player.id) removeVehicle(player.room, vehicle, player.id);
	 if (player.room.vehicleKeys) for (const key of [...player.room.vehicleKeys.keys()]) if (key.startsWith(`${player.id}:`)) player.room.vehicleKeys.delete(key);
     if (player.room.npcs) for (const n of player.room.npcs.values()) {
       if (n.healthReports?.delete(player.id) && reconcileNpcHealth(n)) {
         broadcast(player.room,npcSnapshot(n),n.ownerId); broadcast(player.room,npcVitals(n));
       }
     }
 player.room.delete(player.id);
 announceTimeAuthority(player.room);
 playersById.delete(player.id);
 console.log(`PLAYER_LEAVE id=${player.id}`);
 broadcast(player.room, `REMOVE|${player.id}\n`);
 }
 });
 socket.on("error", () => socket.destroy());
});

server.on("error", error => console.error(`Server error: ${error.code || error.message}`));
process.on("uncaughtException", error => console.error(`Fatal server error: ${error.stack || error.message}`));
process.on("unhandledRejection", error => console.error(`Fatal server error: ${error}`));
const consoleInput = readline.createInterface({ input: process.stdin, output: process.stdout });
consoleInput.on("line", adminCommand);
console.log("Admin commands enabled: /help");
let lastVehicleBroadcast = 0;
setInterval(() => { const now=Date.now(); for (const room of rooms.values()) { for (const player of room.values()) for (const other of room.values()) if (other.id !== player.id) send(other.socket, snapshot(player)); if (room.npcs) for (const n of room.npcs.values()) for (const other of room.values()) if (other.id !== n.ownerId) send(other.socket, npcSnapshot(n)); if (now-lastVehicleBroadcast>=25 && room.vehicles) for (const v of room.vehicles.values()) for (const other of room.values()) if (other.id !== v.ownerId) send(other.socket,vehicleSnapshot(v)); } if(now-lastVehicleBroadcast>=25) lastVehicleBroadcast=now; }, 15);
server.listen(port, "0.0.0.0", () => console.log(`CoopFusion32 server listening on ${port}`));
