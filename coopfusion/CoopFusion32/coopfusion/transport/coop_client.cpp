#include "coop_client.h"
#include "../diagnostics/weapon_log.h"
#include "../world/special_models.h"
#include <android/log.h>
#include <arpa/inet.h>
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

namespace CoopFusionNet {

namespace {
constexpr char kTag[] = "CoopFusion32";
constexpr unsigned int kRadarSpriteCount = 64;

void logSocketError(const char* operation) {
    __android_log_print(ANDROID_LOG_ERROR, kTag, "%s: %s",
                        operation, std::strerror(errno));
}

bool isFiniteNumber(float value) {
    return std::isfinite(value);
}

float normalizeHeadingDegrees(float heading) {
    heading = std::fmod(heading, 360.0f);
    if (heading < 0.0f) {
        heading += 360.0f;
    }
    return heading;
}
}

void CoopClient::Configure(const char* host, int port, const char* nickname) {
    m_host = host ? host : "127.0.0.1";
    m_port = port > 0 ? port : 6767;
    m_nickname = nickname ? nickname : "CJ";
}

bool CoopClient::Connect() {
    Disconnect();

    addrinfo addressHints{};
    addressHints.ai_family = AF_INET;
    addressHints.ai_socktype = SOCK_STREAM;

    addrinfo* addressInfo = nullptr;
    const std::string portText = std::to_string(m_port);
    const int addressLookupStatus = getaddrinfo(
        m_host.c_str(), portText.c_str(), &addressHints, &addressInfo);

    if (addressLookupStatus != 0) {
        __android_log_print(
            ANDROID_LOG_ERROR, kTag,
            "getaddrinfo(%s:%s) failed: %s",
            m_host.c_str(), portText.c_str(), gai_strerror(addressLookupStatus));
        return false;
    }

    const int socketDescriptor = socket(
        addressInfo->ai_family,
        addressInfo->ai_socktype,
        addressInfo->ai_protocol);

    if (socketDescriptor < 0) {
        logSocketError("socket");
        freeaddrinfo(addressInfo);
        return false;
    }

    if (connect(socketDescriptor, addressInfo->ai_addr,
                addressInfo->ai_addrlen) < 0) {
        const std::string operation = "connect " + m_host + ":" + portText;
        logSocketError(operation.c_str());
        close(socketDescriptor);
        freeaddrinfo(addressInfo);
        return false;
    }

    freeaddrinfo(addressInfo);
    m_socket.store(socketDescriptor);
    m_connected = true;
    {
        std::scoped_lock lock(m_mutex);
        m_haveSentClothesDesc = false;
        m_lastClothesSendMs = 0;
    }

    __android_log_print(
        ANDROID_LOG_INFO, kTag,
        "Connected to co-op server %s:%d as %s",
        m_host.c_str(), m_port, m_nickname.c_str());

    SendLine("JOIN|" + m_nickname + "|" +
             std::to_string(m_colorIndex.load()) + "\n");
    return true;
}





void CoopClient::ShutdownSocket() {
    const int socketDescriptor = m_socket.load();
    if (socketDescriptor >= 0) {
        shutdown(socketDescriptor, SHUT_RDWR);
    }
}

void CoopClient::CloseSocket() {
    const int socketDescriptor = m_socket.exchange(-1);
    if (socketDescriptor >= 0) {
        close(socketDescriptor);
    }
}

void CoopClient::Disconnect() {
    ShutdownSocket();
    m_connected = false;

    std::scoped_lock lock(m_mutex);
    m_remotePlayers.clear();
    m_pendingRemoteClothes.clear();
    m_remoteNpcs.clear();
    m_remoteVehicles.clear();
    m_removedVehicles.clear();
    m_removedPlayers.clear();
    m_shots.clear();
    m_damages.clear();
    m_playerVitals.clear();
    m_vehicleEnters.clear();
    m_localVehicleNetworkId = 0;
    m_localVehicleIdsByKey.clear();
    m_rx.clear();
}

void CoopClient::Pump() {
 if (!m_connected) return;
 const int fd = m_socket.load();
 if (fd < 0) { m_connected = false; return; }
 char b[4096];
 auto n = recv(fd, b, sizeof(b), MSG_DONTWAIT);
 if (n > 0) {
 m_rx.append(b, (size_t)n);
 if (m_rx.size() > 65536) { m_rx.clear(); return; }
 size_t e;
 while ((e = m_rx.find('\n')) != std::string::npos) {
 auto line = m_rx.substr(0, e);
 m_rx.erase(0, e + 1);
 if (line.size() < 4096) ParseLine(line);
 }
 } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
 CloseSocket();
 m_connected = false;
 return;
 }

	uint16_t modelId = 0, vehicleNetworkId = 0; uint32_t vehicleKey = 0; uint8_t vehicleSeat = 0; char specialName[16]{}; LocalInput input{};
	{
	 std::scoped_lock lock(m_mutex);
	 modelId = m_modelId;
	 vehicleNetworkId = m_localVehicleNetworkId; vehicleKey = m_localVehicleKey; vehicleSeat = m_localVehicleSeat;
	 std::memcpy(specialName, m_specialModelName, sizeof(specialName));
	 input = m_input;
	 m_input.enterVehicle = false;
	 m_input.jump = false;
	}
 char p[384];
	std::snprintf(p, sizeof(p), "INPUT|%.3f|%.3f|%.3f|%.3f|%d|%d|%d|%.3f|%.3f|%.3f|%.3f|%u|%u|%u|%u|%u|%u|%s|%u|%u|%u|%u|%u|%.4f|%.4f|%.4f\n",
	 m_x.load(), m_y.load(), m_z.load(), m_heading.load(), input.fire, input.jump, input.enterVehicle,
	 input.moveX, input.moveY, input.aimX, input.aimY, modelId, m_animationState.load(),
	 m_weaponType.load(), m_health.load(), m_armour.load(), m_colorIndex.load(), specialName,
	 vehicleNetworkId, vehicleSeat, vehicleKey, static_cast<unsigned>(m_localAreaCode.load()),
	 static_cast<unsigned>(m_wanted.load()), m_vx.load(), m_vy.load(), m_vz.load());
	 if (m_weaponType.load() != m_lastLoggedSentWeapon) { WeaponFileLog("WEAPON_SEND id=local weapon=%u", m_weaponType.load()); m_lastLoggedSentWeapon = m_weaponType.load(); }
	 SendLine(p);

    const uint64_t clothesNow = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    PlayerClothesDesc clothesToSend{};
    bool sendClothes = false;
    const uint16_t clothesPlayerId = m_localId.load();
    {
        std::scoped_lock lock(m_mutex);
        if (m_haveLocalClothesDesc && clothesPlayerId) {
            clothesToSend = m_localClothesDesc;
            const bool changed = !m_haveSentClothesDesc ||
                std::memcmp(&m_lastSentClothesDesc, &clothesToSend, sizeof(clothesToSend)) != 0;
            sendClothes = changed || clothesNow - m_lastClothesSendMs >= 1500;
            if (sendClothes) {
                m_lastSentClothesDesc = clothesToSend;
                m_haveSentClothesDesc = true;
                m_lastClothesSendMs = clothesNow;
            }
        }
    }
    if (sendClothes) {
        std::string packet = "CLOTHES|" + std::to_string(clothesPlayerId);
        char field[32];
        for (uint32_t key : clothesToSend.modelKeys) {
            std::snprintf(field, sizeof(field), "|%u", static_cast<unsigned>(key));
            packet += field;
        }
        for (uint32_t key : clothesToSend.textureKeys) {
            std::snprintf(field, sizeof(field), "|%u", static_cast<unsigned>(key));
            packet += field;
        }
        std::snprintf(field, sizeof(field), "|%.5f|%.5f\n", clothesToSend.fatStat, clothesToSend.muscleStat);
        packet += field;
        SendLine(packet);
    }
	 const uint64_t vehicleNow = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
	 if (vehicleNow - m_lastVehicleSendMs >= 50) {
		 RemoteVehicle vehicle{}; VehicleAppearanceState appearance{}; VehicleExtendedState extended{}; uint32_t key = 0; uint8_t localSeat = 0; bool active = false;
		 { std::scoped_lock lock(m_mutex); active = m_localVehicleActive; key = m_localVehicleKey;
		   localSeat=m_localVehicleSeat; vehicle.driverSeat=localSeat;
		   vehicle.modelId = m_localVehicleModel; vehicle.x=m_localVehicleX; vehicle.y=m_localVehicleY; vehicle.z=m_localVehicleZ; vehicle.heading=m_localVehicleHeading;
		   vehicle.vx=m_localVehicleVx; vehicle.vy=m_localVehicleVy; vehicle.vz=m_localVehicleVz; vehicle.health=m_localVehicleHealth;
		   vehicle.primaryColor=m_vehiclePrimary; vehicle.secondaryColor=m_vehicleSecondary; vehicle.engineOn=m_vehicleEngine; vehicle.lightsOn=m_vehicleLights; vehicle.sirenOn=m_vehicleSiren; vehicle.doorLock=m_vehicleDoorLock; vehicle.dirt=m_vehicleDirt; vehicle.tyreStatus=m_vehicleTyres; vehicle.doorStatus=m_vehicleDoors; vehicle.lightStatus=m_vehicleLightDamage; vehicle.panelStatus=m_vehiclePanelDamage; vehicle.trailerId=m_vehicleTrailerId;
		   vehicle.rightX=m_vehicleRightX; vehicle.rightY=m_vehicleRightY; vehicle.rightZ=m_vehicleRightZ; vehicle.upX=m_vehicleUpX; vehicle.upY=m_vehicleUpY; vehicle.upZ=m_vehicleUpZ; vehicle.atX=m_vehicleAtX; vehicle.atY=m_vehicleAtY; vehicle.atZ=m_vehicleAtZ;
           vehicle.steering=m_vehicleSteering; vehicle.gasPedal=m_vehicleGasPedal; vehicle.brakePedal=m_vehicleBrakePedal; vehicle.miscComponentAngle=m_vehicleMiscComponentAngle; vehicle.turretVertical=m_vehicleTurretVertical; vehicle.turretHorizontal=m_vehicleTurretHorizontal; vehicle.turnVx=m_vehicleTurnVx; vehicle.turnVy=m_vehicleTurnVy; vehicle.turnVz=m_vehicleTurnVz; vehicle.orientationValid=m_vehicleOrientationValid; appearance=m_localVehicleAppearance; extended=m_localVehicleExtended; }
         if (active && key) {
             SendVehicleState(key, vehicle);
             bool sendLook = false;
             {
                 std::scoped_lock lock(m_mutex);
                 const bool changed = !m_haveSentVehicleAppearance ||
                     appearance.paintjob != m_lastSentVehicleAppearance.paintjob ||
                     appearance.upgrades != m_lastSentVehicleAppearance.upgrades;
                 sendLook = appearance.valid && (changed || vehicleNow - m_lastVehicleLookSendMs >= 1500);
                 if (sendLook) {
                     m_lastSentVehicleAppearance = appearance;
                     m_haveSentVehicleAppearance = true;
                     m_lastVehicleLookSendMs = vehicleNow;
                 }
             }
             if (sendLook) SendVehicleLook(key, appearance);
             if (extended.valid) SendVehicleExtended(key, extended);
         }
		 m_lastVehicleSendMs = vehicleNow;
	 }

 AnimationProfile animation{}; { std::scoped_lock lock(m_mutex); animation = m_animationProfile; }
 const uint64_t animationNow = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
 if (animationNow - m_lastAnimationSendMs >= 66) {
 char ap[256];
 std::snprintf(ap, sizeof(ap), "ANIM2|%u|%u|%u|%u|%u|%u|%u|%.4f|%.4f|%.4f|%.4f\n",
 static_cast<unsigned>(m_localId.load()), animation.sequence,
 static_cast<unsigned>(animation.locomotion), static_cast<unsigned>(animation.action),
 static_cast<unsigned>(animation.flags), static_cast<unsigned>(animation.weaponState),
 static_cast<unsigned>(animation.animGroup), animation.phase, animation.blend,
 animation.aimYaw, animation.aimPitch);
 SendLine(ap); m_lastAnimationSendMs = animationNow;
 }
 if (animationNow - m_lastAssociationSendMs >= 66) {
 uint8_t associationCount = 0; std::array<AnimationAssociationState,64> associations{};
 { std::scoped_lock lock(m_mutex); associationCount = m_animationAssociationCount; associations = m_animationAssociations; }
 char header[96]; std::snprintf(header, sizeof(header), "ANIM4|%u|%u", static_cast<unsigned>(m_localId.load()), static_cast<unsigned>(associationCount));
 std::string packet(header); char field[128];
 for (uint8_t i = 0; i < associationCount; ++i) {
 const auto& a = associations[i];
 std::snprintf(field, sizeof(field), "|%u|%d|%u|%.5f|%.5f|%.5f|%.5f|%.5f|%u", a.group, a.animationId, a.flags, a.blend, a.blendDelta, a.currentTime, a.speed, a.timeStep, a.hierarchyHash);
 packet += field;
 }
 packet += "\n"; SendLine(packet); m_lastAssociationSendMs = animationNow;
 }
}

void CoopClient::SetLocalTransform(float x, float y, float z, float h, float vx, float vy, float vz) {
    if (!isFiniteNumber(x) || !isFiniteNumber(y) || !isFiniteNumber(z) || !isFiniteNumber(h) ||
        !isFiniteNumber(vx) || !isFiniteNumber(vy) || !isFiniteNumber(vz)) return;
    m_x.store(x); m_y.store(y); m_z.store(z); m_heading.store(h);
    m_vx.store(std::max(-150.0f,std::min(150.0f,vx)));
    m_vy.store(std::max(-150.0f,std::min(150.0f,vy)));
    m_vz.store(std::max(-150.0f,std::min(150.0f,vz)));
}
void CoopClient::SetLocalAreaCode(uint8_t areaCode) { m_localAreaCode.store(areaCode); }
void CoopClient::SetLocalVehicle(uint32_t key, uint16_t model, float x, float y, float z, float heading, float vx, float vy, float vz, float health, uint8_t seat) {
    std::scoped_lock lock(m_mutex);
    if (key != m_localVehicleKey) {
        m_haveSentVehicleAppearance = false;
        m_lastVehicleLookSendMs = 0;
        auto known = m_localVehicleIdsByKey.find(key);
        m_localVehicleNetworkId = key && known != m_localVehicleIdsByKey.end() ? known->second : 0;
    }
    m_localVehicleActive = key != 0 && seat == 0;
    m_localVehicleKey = key; m_localVehicleModel = model;
    m_localVehicleX=x; m_localVehicleY=y; m_localVehicleZ=z; m_localVehicleHeading=heading;
    m_localVehicleVx=vx; m_localVehicleVy=vy; m_localVehicleVz=vz; m_localVehicleHealth=health; m_localVehicleSeat=seat;
    if (!key) { m_localVehicleNetworkId=0; m_vehicleOrientationValid=false; }
}
void CoopClient::SetLocalVehicleNetworkId(uint16_t id) {
    std::scoped_lock lock(m_mutex);
    m_localVehicleNetworkId = id;
}
uint16_t CoopClient::LocalVehicleNetworkIdForKey(uint32_t key) const {
    if (!key) return 0;
    std::scoped_lock lock(m_mutex);
    auto it = m_localVehicleIdsByKey.find(key);
    return it == m_localVehicleIdsByKey.end() ? 0 : it->second;
}
void CoopClient::SetLocalVehicleDetails(uint8_t primary,uint8_t secondary,uint8_t engine,uint8_t lights,uint8_t siren,uint8_t doorLock,float dirt,uint8_t tyres,uint32_t doors,uint32_t lightsDamage,uint32_t panels,uint16_t trailerId) {
	 std::scoped_lock lock(m_mutex); m_vehiclePrimary=primary; m_vehicleSecondary=secondary; m_vehicleEngine=engine; m_vehicleLights=lights; m_vehicleSiren=siren; m_vehicleDoorLock=doorLock; m_vehicleDirt=std::isfinite(dirt)?std::max(0.0f,std::min(15.0f,dirt)):0.0f; m_vehicleTyres=tyres; m_vehicleDoors=doors; m_vehicleLightDamage=lightsDamage; m_vehiclePanelDamage=panels; m_vehicleTrailerId=trailerId;
}
void CoopClient::SetLocalVehicleOrientation(float rx,float ry,float rz,float ux,float uy,float uz,float ax,float ay,float az) {
	 if (!isFiniteNumber(rx)||!isFiniteNumber(ry)||!isFiniteNumber(rz)||!isFiniteNumber(ux)||!isFiniteNumber(uy)||!isFiniteNumber(uz)||!isFiniteNumber(ax)||!isFiniteNumber(ay)||!isFiniteNumber(az)) return;
	 std::scoped_lock lock(m_mutex); m_vehicleRightX=rx; m_vehicleRightY=ry; m_vehicleRightZ=rz; m_vehicleUpX=ux; m_vehicleUpY=uy; m_vehicleUpZ=uz; m_vehicleAtX=ax; m_vehicleAtY=ay; m_vehicleAtZ=az; m_vehicleOrientationValid=true;
}
void CoopClient::SetLocalVehicleControls(float steering,float gas,float brake,uint16_t misc,float turretV,float turretH) {
 if (!isFiniteNumber(steering)||!isFiniteNumber(gas)||!isFiniteNumber(brake)||!isFiniteNumber(turretV)||!isFiniteNumber(turretH)) return;
 std::scoped_lock lock(m_mutex); m_vehicleSteering=std::max(-1.0f,std::min(1.0f,steering)); m_vehicleGasPedal=std::max(-1.0f,std::min(1.0f,gas)); m_vehicleBrakePedal=std::max(0.0f,std::min(1.0f,brake)); m_vehicleMiscComponentAngle=misc; m_vehicleTurretVertical=std::max(-10.0f,std::min(10.0f,turretV)); m_vehicleTurretHorizontal=std::max(-10.0f,std::min(10.0f,turretH));
}
void CoopClient::SetLocalVehicleTurnSpeed(float x,float y,float z) {
 if(!isFiniteNumber(x)||!isFiniteNumber(y)||!isFiniteNumber(z)) return;
 std::scoped_lock lock(m_mutex); m_vehicleTurnVx=std::max(-20.0f,std::min(20.0f,x)); m_vehicleTurnVy=std::max(-20.0f,std::min(20.0f,y)); m_vehicleTurnVz=std::max(-20.0f,std::min(20.0f,z));
}
void CoopClient::SetLocalVehicleAppearance(int8_t paintjob, const int16_t* upgrades) {
    if (!upgrades) return;
    VehicleAppearanceState appearance{};
    appearance.paintjob = (paintjob >= -1 && paintjob <= 3) ? paintjob : -1;
    for (size_t i = 0; i < appearance.upgrades.size(); ++i) {
        const int value = upgrades[i];
        appearance.upgrades[i] = (value >= -1 && value < static_cast<int>(kModelInfoCapacity))
            ? static_cast<int16_t>(value) : -1;
    }
    appearance.valid = true;
    std::scoped_lock lock(m_mutex);
    m_localVehicleAppearance = appearance;
}
void CoopClient::SetLocalVehicleExtended(const VehicleExtendedState& extended) {
    std::scoped_lock lock(m_mutex);
    m_localVehicleExtended = extended;
}

void CoopClient::SetLocalAnimationState(uint8_t s) { m_animationState.store(s <= kMoveSprint ? s : kMoveStill); }
void CoopClient::SetLocalModel(uint16_t id, const char* specialName) {
 char normalized[16]{};
 coop_special::NormalizeName(specialName, normalized, sizeof(normalized));
 uint16_t oldModel = 0; char oldSpecial[16]{};
 { std::scoped_lock lock(m_mutex); oldModel = m_modelId; std::memcpy(oldSpecial, m_specialModelName, sizeof(oldSpecial)); m_modelId = (id < kModelInfoCapacity || (id >= coop_special::kFirstVirtualSpecialIndex && id <= coop_special::kLastVirtualSpecialIndex)) ? id : 0;
   if (!normalized[0] && id >= coop_special::kFirstVirtualSpecialIndex && id <= coop_special::kLastVirtualSpecialIndex)
       coop_special::NormalizeName(coop_special::VirtualSpecialName(id), normalized, sizeof(normalized));
   std::memcpy(m_specialModelName, normalized, sizeof(m_specialModelName)); }
 if (oldModel != m_modelId || std::strcmp(oldSpecial, normalized) != 0) WeaponFileLog("LOCAL_MODEL_CAPTURE old=%hu new=%hu special=%s", oldModel, m_modelId, normalized);
}
void CoopClient::SetLocalWeaponType(uint8_t w) { if (w <= 0x3A) m_weaponType.store(w); }
void CoopClient::SetLocalWanted(uint8_t wanted) { m_wanted.store(wanted > 6 ? 6 : wanted); }
void CoopClient::SetLocalHealthArmour(uint16_t health, uint16_t armour) { m_health.store(health > 100 ? 100 : health); m_armour.store(armour > 100 ? 100 : armour); }
void CoopClient::SetLocalColorIndex(uint8_t c) { m_colorIndex.store(c <= 7 ? c : 0); }
void CoopClient::SetLocalClothesDesc(const PlayerClothesDesc& clothes) {
    if (!std::isfinite(clothes.fatStat) || !std::isfinite(clothes.muscleStat)) return;
    std::scoped_lock lock(m_mutex);
    m_localClothesDesc = clothes;
    m_haveLocalClothesDesc = true;
}
void CoopClient::SetInput(const LocalInput& i) { std::scoped_lock lock(m_mutex); m_input = i; }


void CoopClient::SendBoneSnapshot(const NetworkBone* bones, uint8_t count) {
 if (!bones || count == 0 || count > 19) return;
 std::string p = "BONES2|" + std::to_string((unsigned)m_localId.load()) + "|" + std::to_string(count);
 char b[96];
 for (uint8_t i = 0; i < count; ++i) {
 std::snprintf(b, sizeof(b), "|%u|%.5f|%.5f|%.5f|%.5f", bones[i].index, bones[i].qx, bones[i].qy, bones[i].qz, bones[i].qw);
 p += b;
 }
 p += "\n";
 SendLine(p);
}

void CoopClient::SendNpcState(uint32_t localRef, uint16_t modelId, float x, float y, float z, float heading, uint16_t health, uint16_t armour, uint8_t weapon, uint8_t moveState, uint32_t vehicleKey, uint8_t vehicleSeat, const char* specialModelName, uint16_t vehicleNetworkId) {
 char normalized[coop_special::kNameCapacity]{};
 if (coop_special::IsSpecialNetworkIndex(modelId)) {
 coop_special::NormalizeName(specialModelName, normalized, sizeof(normalized));
 if (!normalized[0] && modelId >= coop_special::kFirstVirtualSpecialIndex && modelId <= coop_special::kLastVirtualSpecialIndex)
 coop_special::NormalizeName(coop_special::VirtualSpecialName(modelId), normalized, sizeof(normalized));
 }
 char p[384];
	 std::snprintf(p, sizeof(p), "NPC_STATE|%u|%u|%.3f|%.3f|%.3f|%.3f|%u|%u|%u|%u|%u|%u|%s|%u\n",
	 localRef, modelId, x, y, z, heading, health, armour, weapon, moveState, vehicleKey, vehicleSeat, normalized, vehicleNetworkId);
 SendLine(p);
}
void CoopClient::SendNpcVehicleState(uint32_t key,const RemoteVehicle& state) {
    if (!key) return;
    SendVehicleState(key, state);
    if (!state.appearance.valid) return;
    const uint64_t now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    bool sendLook = false;
    {
        std::scoped_lock lock(m_mutex);
        auto& last = m_lastNpcVehicleLookSendMs[key];
        sendLook = now - last >= 1000;
        if (sendLook) last = now;
    }
    if (sendLook) SendVehicleLook(key, state.appearance);
    if (state.extended.valid) SendVehicleExtended(key, state.extended);
}
void CoopClient::SendNpcVehicleRemove(uint32_t key) { if (key) SendLine("VEHICLE_REMOVE_KEY|" + std::to_string(key) + "\n"); }
void CoopClient::SendNpcMotion(uint32_t localRef, float vx, float vy, float vz,
                               float currentRotation, float aimingRotation,
                               float lookingDirection, uint8_t moveState, uint8_t fightingStyle) {
 if (!localRef || !isFiniteNumber(vx) || !isFiniteNumber(vy) || !isFiniteNumber(vz) ||
     !isFiniteNumber(currentRotation) || !isFiniteNumber(aimingRotation) || !isFiniteNumber(lookingDirection)) return;
 char p[256];
 std::snprintf(p, sizeof(p), "NPC_MOTION|%u|%.4f|%.4f|%.4f|%.4f|%.4f|%.4f|%u|%u\n",
     localRef, vx, vy, vz, currentRotation, aimingRotation, lookingDirection,
     static_cast<unsigned>(moveState), static_cast<unsigned>(fightingStyle));
 SendLine(p);
}
void CoopClient::SendNpcAim(uint32_t localRef, bool aiming, bool firing, float x, float y, float z) {
 if (!localRef || !isFiniteNumber(x) || !isFiniteNumber(y) || !isFiniteNumber(z) ||
     std::fabs(x) > 100000.0f || std::fabs(y) > 100000.0f || std::fabs(z) > 100000.0f) return;
 const bool active = aiming || firing;
 char p[192];
 std::snprintf(p, sizeof(p), "NPC_AIM|%u|%u|%u|%.3f|%.3f|%.3f\n",
     localRef, active ? 1u : 0u, firing ? 1u : 0u, x, y, z);
 SendLine(p);
}
void CoopClient::SendNpcAnimationAssociations(uint32_t localRef,
        const AnimationAssociationState* states, uint8_t count) {
 if (!localRef || (!states && count) || count > 32) return;
 std::string packet = "NPC_ANIM4|" + std::to_string(localRef) + "|" + std::to_string(count);
 char field[160];
 for (uint8_t i = 0; i < count; ++i) {
   const auto& a = states[i];
   if (!std::isfinite(a.blend) || !std::isfinite(a.blendDelta) || !std::isfinite(a.currentTime) ||
       !std::isfinite(a.speed) || !std::isfinite(a.timeStep)) return;
   std::snprintf(field, sizeof(field), "|%u|%d|%u|%.5f|%.5f|%.5f|%.5f|%.5f|%u",
       a.group, a.animationId, a.flags, a.blend, a.blendDelta, a.currentTime,
       a.speed, a.timeStep, a.hierarchyHash);
   packet += field;
 }
 packet += "\n";
 SendLine(packet);
}
void CoopClient::SendNpcHealth(uint32_t npcId, uint16_t health, uint16_t armour) { if (!npcId) return; char p[96]; std::snprintf(p, sizeof(p), "NPC_HEALTH|%u|%u|%u\n", npcId, std::min<uint16_t>(100, health), std::min<uint16_t>(100, armour)); SendLine(p); }
void CoopClient::SendPlayerHealth(uint16_t playerId, uint16_t health, uint16_t armour) { if (!playerId) return; char p[96]; std::snprintf(p, sizeof(p), "PLAYER_HEALTH|%u|%u|%u\n", playerId, std::min<uint16_t>(100, health), std::min<uint16_t>(100, armour)); SendLine(p); }
void CoopClient::SendPlayerHit(uint16_t targetId, uint8_t weapon, uint16_t damage,
                               uint8_t piece, uint8_t direction, uint8_t fightSet, uint8_t fightMove) {
 if (!targetId || weapon > 15 || !damage || damage > 10000 || piece > 9 || direction > 3 ||
     !((fightSet == 0xFF && fightMove == 0xFF) || (fightSet >= 4 && fightSet <= 16 && fightMove <= 4))) return;
 char p[128];
 std::snprintf(p, sizeof(p), "PLAYER_HIT|%u|%u|%u|%u|%u|%u|%u\n", targetId, weapon, damage, piece, direction, fightSet, fightMove);
 SendLine(p);
}
void CoopClient::SendNpcRemove(uint32_t localRef) { SendLine("NPC_REMOVE|" + std::to_string(localRef) + "\n"); }
void CoopClient::SendMissionMarker(uint16_t sourceSlot, uint8_t type, uint8_t display, uint8_t sprite,
                                    uint16_t scale, uint32_t color, float x, float y, float z, float radius) {
  if (sourceSlot > 174 || (display != 1 && display != 3) ||
      !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(radius)) return;
  char p[256];
  std::snprintf(p, sizeof(p), "MISSION_MARKER|%u|%u|%u|%u|%u|%u|%.3f|%.3f|%.3f|%.3f\n",
                sourceSlot, type, display, sprite, scale, color, x, y, z,
                std::max(0.1f, std::min(100.0f, radius)));
  SendLine(p);
}
void CoopClient::SendMissionMarkerRemove(uint16_t sourceSlot) {
  if (sourceSlot <= 174) SendLine("MISSION_MARKER_REMOVE|" + std::to_string(sourceSlot) + "\n");
}

void CoopClient::SendShot(uint8_t w, float x, float y, float z, float dx, float dy, float dz, uint16_t animGroup, uint16_t animId) {
 char p[224];
 int n = std::snprintf(p, sizeof(p), "SHOT|%u|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f", w, x, y, z, dx, dy, dz);
 if (animGroup != 0 && (animId == kAnimationFireId || animId == kAnimationCrouchFireId) && n > 0 && n < static_cast<int>(sizeof(p)) - 24)
  n += std::snprintf(p + n, sizeof(p) - n, "|%u|%u", static_cast<unsigned>(animGroup), static_cast<unsigned>(animId));
 std::snprintf(p + n, sizeof(p) - n, "\n");
 WeaponFileLog("SHOT_PACKET_SEND_LEGACY weapon=%u", w); SendLine(p);
}

void CoopClient::SendShotEvent(uint8_t kind, uint8_t w, float x, float y, float z,
                               float bx, float by, float bz, float force,
                               uint16_t animGroup, uint16_t animId, uint16_t vehicleId) {
    if (kind > 2 || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        !std::isfinite(bx) || !std::isfinite(by) || !std::isfinite(bz) || !std::isfinite(force)) return;
    force = std::max(0.0f, std::min(5.0f, force));
    if (kind != 2) vehicleId = 0;
    char p[320];
    int n = std::snprintf(p, sizeof(p), "SHOTX|%u|%u|%u|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f",
        static_cast<unsigned>(kind), static_cast<unsigned>(w), static_cast<unsigned>(vehicleId), x, y, z, bx, by, bz, force);
    if (!(animGroup && (animId == kAnimationFireId || animId == kAnimationCrouchFireId))) { animGroup = 0; animId = 0; }
    if (n < 0 || n >= static_cast<int>(sizeof(p))) return;
    n += std::snprintf(p + n, sizeof(p) - static_cast<size_t>(n), "|%u|%u", static_cast<unsigned>(animGroup), static_cast<unsigned>(animId));
    if (n < 0 || n >= static_cast<int>(sizeof(p))) return;
    std::snprintf(p + n, sizeof(p) - static_cast<size_t>(n), "\n");
    WeaponFileLog("SHOTX_SEND kind=%u weapon=%u vehicle=%u force=%.2f", static_cast<unsigned>(kind), static_cast<unsigned>(w), static_cast<unsigned>(vehicleId), force);
    SendLine(p);
}
void CoopClient::SendExplosionEvent(uint8_t type, float x, float y, float z) {
    if (type > 12 || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        std::fabs(x) > 3000.0f || std::fabs(y) > 3000.0f || std::fabs(z) > 10000.0f) return;
    char packet[128];
    std::snprintf(packet, sizeof(packet), "EXPX|%u|%.3f|%.3f|%.3f\n",
                  static_cast<unsigned>(type), x, y, z);
    WeaponFileLog("EXPLOSION_SEND type=%u pos=(%.2f,%.2f,%.2f)", static_cast<unsigned>(type), x, y, z);
    SendLine(packet);
}

void CoopClient::SendVehicleEnter(uint16_t id,uint8_t seat) { if (id) SendLine("VEHICLE_ENTER|" + std::to_string(id) + "|" + std::to_string(seat) + "\n"); }
void CoopClient::SendVehicleClaim(uint16_t id,uint8_t seat,uint32_t key) { if (id && key) SendLine("VEHICLE_CLAIM|" + std::to_string(id) + "|" + std::to_string(seat) + "|" + std::to_string(key) + "\n"); }

std::vector<RemotePlayer> CoopClient::GetRemotePlayers() { std::scoped_lock l(m_mutex); return m_remotePlayers; }
std::vector<RemoteNpc> CoopClient::GetRemoteNpcs() { std::scoped_lock l(m_mutex); return m_remoteNpcs; }
std::vector<uint32_t> CoopClient::DrainRemovedNpcs() { std::scoped_lock l(m_mutex); auto x = std::move(m_removedNpcs); m_removedNpcs.clear(); return x; }

std::vector<RemoteMissionMarker> CoopClient::GetRemoteMissionMarkers() { std::scoped_lock l(m_mutex); return m_remoteMissionMarkers; }
std::vector<uint32_t> CoopClient::DrainRemovedMissionMarkers() { std::scoped_lock l(m_mutex); auto x = std::move(m_removedMissionMarkers); m_removedMissionMarkers.clear(); return x; }std::vector<RemoteVehicle> CoopClient::GetRemoteVehicles() { std::scoped_lock l(m_mutex); return m_remoteVehicles; }
std::vector<uint16_t> CoopClient::DrainRemovedVehicles() { std::scoped_lock l(m_mutex); auto x = std::move(m_removedVehicles); m_removedVehicles.clear(); return x; }
std::vector<RemotePlayerRemoved> CoopClient::DrainRemovedPlayers() { std::scoped_lock l(m_mutex); auto x = std::move(m_removedPlayers); m_removedPlayers.clear(); return x; }
std::vector<RemoteShot> CoopClient::DrainShots() { std::scoped_lock l(m_mutex); auto x = std::move(m_shots); m_shots.clear(); return x; }
std::vector<RemoteExplosion> CoopClient::DrainExplosions() { std::scoped_lock l(m_mutex); auto x = std::move(m_explosions); m_explosions.clear(); return x; }
std::vector<RemoteDamage> CoopClient::DrainDamages() { std::scoped_lock l(m_mutex); auto x = std::move(m_damages); m_damages.clear(); return x; }
std::vector<RemoteVehicleEnter> CoopClient::DrainVehicleEnters() { std::scoped_lock l(m_mutex); auto x = std::move(m_vehicleEnters); m_vehicleEnters.clear(); return x; }
std::vector<NpcSpawnTest> CoopClient::DrainNpcSpawnTests() { std::scoped_lock l(m_mutex); auto x = std::move(m_npcSpawnTests); m_npcSpawnTests.clear(); return x; }
uint16_t CoopClient::LocalId() const { return m_localId; }
bool CoopClient::IsConnected() const { return m_connected; }

void CoopClient::SendLine(const std::string& s) {

    std::scoped_lock sendLock(m_sendMutex);
	 const int fd = m_socket.load();
 if (fd < 0) return;
 size_t sent = 0;
 while (sent < s.size()) {
 auto n = send(fd, s.data() + sent, s.size() - sent, MSG_NOSIGNAL);
 if (n <= 0) { logSocketError("send"); return; }
	 sent += (size_t)n;
	 }
}

void CoopClient::SendVehicleState(uint32_t localKey,const RemoteVehicle& v) {
	 if (!localKey || v.modelId < 400 || v.modelId >= kModelInfoCapacity || !isFiniteNumber(v.x) || !isFiniteNumber(v.y) || !isFiniteNumber(v.z) || !isFiniteNumber(v.heading) || !isFiniteNumber(v.vx) || !isFiniteNumber(v.vy) || !isFiniteNumber(v.vz) || !isFiniteNumber(v.health) || !isFiniteNumber(v.turnVx) || !isFiniteNumber(v.turnVy) || !isFiniteNumber(v.turnVz) || !isFiniteNumber(v.steering) || !isFiniteNumber(v.gasPedal) || !isFiniteNumber(v.brakePedal) || !isFiniteNumber(v.turretVertical) || !isFiniteNumber(v.turretHorizontal)) return;
	 char p[768];
	 std::snprintf(p,sizeof(p),"VEHICLE_STATE|%u|%u|%.3f|%.3f|%.3f|%.3f|%.4f|%.4f|%.4f|%.1f|%u|%u|%u|%u|%u|%u|%u|%.3f|%u|%u|%u|%u|%u|%.5f|%.5f|%.5f|%.5f|%.5f|%.5f|%.5f|%.5f|%.5f|%.4f|%.4f|%.4f|%u|%.4f|%.4f|%.4f|%.4f|%.4f\n",
	 localKey,v.modelId,v.x,v.y,v.z,v.heading,v.vx,v.vy,v.vz,v.health,v.driverSeat,v.primaryColor,v.secondaryColor,v.engineOn,v.lightsOn,v.sirenOn,v.doorLock,v.dirt,v.tyreStatus,v.doorStatus,v.lightStatus,v.panelStatus,v.trailerId,
	 v.orientationValid?v.rightX:1.0f,v.orientationValid?v.rightY:0.0f,v.orientationValid?v.rightZ:0.0f,v.orientationValid?v.upX:0.0f,v.orientationValid?v.upY:1.0f,v.orientationValid?v.upZ:0.0f,v.orientationValid?v.atX:0.0f,v.orientationValid?v.atY:0.0f,v.orientationValid?v.atZ:1.0f,
     v.steering,v.gasPedal,v.brakePedal,v.miscComponentAngle,v.turretVertical,v.turretHorizontal,v.turnVx,v.turnVy,v.turnVz);
	 SendLine(p);
}

void CoopClient::SendVehicleLook(uint32_t localKey, const VehicleAppearanceState& appearance) {
    if (!localKey || !appearance.valid || appearance.paintjob < -1 || appearance.paintjob > 3) return;
    char packet[256];
    int used = std::snprintf(packet, sizeof(packet), "VEHICLE_LOOK|%u|%d", localKey,
                             static_cast<int>(appearance.paintjob));
    if (used <= 0 || static_cast<size_t>(used) >= sizeof(packet)) return;
    for (int16_t upgrade : appearance.upgrades) {
        if (upgrade < -1 || upgrade >= static_cast<int16_t>(kModelInfoCapacity)) return;
        const int written = std::snprintf(packet + used, sizeof(packet) - static_cast<size_t>(used),
                                          "|%d", static_cast<int>(upgrade));
        if (written <= 0 || static_cast<size_t>(written) >= sizeof(packet) - static_cast<size_t>(used)) return;
        used += written;
    }
    if (static_cast<size_t>(used) + 2 >= sizeof(packet)) return;
    packet[used++] = '\n';
    packet[used] = '\0';
    SendLine(packet);
}

void CoopClient::SendVehicleExtended(uint32_t localKey, const VehicleExtendedState& e) {
    if (!localKey || !e.valid) return;
    const float vals[] = {e.landingGear, e.searchLightX, e.searchLightY, e.searchLightZ,
        e.searchLightIntensity, e.bikeLean, e.bikeLeanForward, e.bmxSprintLean, e.bmxPedallingCharge};
    for (float v : vals) if (!std::isfinite(v)) return;
    char p[512];
    std::snprintf(p, sizeof(p),
        "VEHICLE_EX|%u|%u|%u|%d|%d|%u|%u|%.5f|%u|%.4f|%.4f|%.4f|%.4f|%u|%u|%.4f|%.4f|%u|%.4f|%.4f\n",
        localKey, e.tertiaryColor, e.quaternaryColor, e.extraA, e.extraB, e.handbrake,
        e.landingGearValid ? 1u : 0u, e.landingGear,
        e.searchLightValid ? 1u : 0u, e.searchLightX, e.searchLightY, e.searchLightZ,
        e.searchLightIntensity, e.searchLightEnabled,
        e.bikeLeanValid ? 1u : 0u, e.bikeLean, e.bikeLeanForward,
        e.bmxValid ? 1u : 0u, e.bmxSprintLean, e.bmxPedallingCharge);
    SendLine(p);
}

void CoopClient::ParseLine(const std::string& line) {
 if (line.rfind("CLOTHES|", 0) == 0) {
     std::vector<std::string> f;
     size_t begin = 8;
     while (begin <= line.size()) {
         size_t end = line.find('|', begin);
         if (end == std::string::npos) end = line.size();
         f.emplace_back(line.substr(begin, end - begin));
         if (end == line.size()) break;
         begin = end + 1;
     }
     if (f.size() != 31) return;
     char* end = nullptr;
     const unsigned long parsedId = std::strtoul(f[0].c_str(), &end, 10);
     if (!end || *end || parsedId == 0 || parsedId > 65535 || parsedId == m_localId.load()) return;
     PlayerClothesDesc clothes{};
     for (size_t i = 0; i < 10; ++i) {
         end = nullptr; unsigned long v = std::strtoul(f[1 + i].c_str(), &end, 10);
         if (!end || *end || v > 0xFFFFFFFFUL) return;
         clothes.modelKeys[i] = static_cast<uint32_t>(v);
     }
     for (size_t i = 0; i < 18; ++i) {
         end = nullptr; unsigned long v = std::strtoul(f[11 + i].c_str(), &end, 10);
         if (!end || *end || v > 0xFFFFFFFFUL) return;
         clothes.textureKeys[i] = static_cast<uint32_t>(v);
     }
     end = nullptr; clothes.fatStat = std::strtof(f[29].c_str(), &end);
     if (!end || *end || !std::isfinite(clothes.fatStat) || std::fabs(clothes.fatStat) > 1000.0f) return;
     end = nullptr; clothes.muscleStat = std::strtof(f[30].c_str(), &end);
     if (!end || *end || !std::isfinite(clothes.muscleStat) || std::fabs(clothes.muscleStat) > 1000.0f) return;
     std::scoped_lock lock(m_mutex);
     auto it = std::find_if(m_remotePlayers.begin(), m_remotePlayers.end(),
         [&](const RemotePlayer& p) { return p.id == static_cast<uint16_t>(parsedId); });
     if (it != m_remotePlayers.end()) {
         it->clothes = clothes;
         it->clothesValid = true;
     } else {
         m_pendingRemoteClothes[static_cast<uint16_t>(parsedId)] = clothes;
     }
     return;
 }
 if (line.rfind("WELCOME|", 0) == 0) {
 unsigned id = 0;
 if (std::sscanf(line.c_str(), "WELCOME|%u", &id) == 1 && id <= 65535) {
 m_localId = (uint16_t)id; m_timeAuthorityId = 0; m_hasRemoteGameTime = false;
 m_sentGameTime = false; m_forceGameTimeSend = false; m_lastGameTimeSendMs = 0;
 std::scoped_lock l(m_mutex); m_gameTimes.clear(); m_pendingRemoteClothes.clear(); m_pendingVehicleLooks.clear(); m_pendingVehicleExtended.clear(); m_explosions.clear();
 }
 return;
 }
	 if (line.rfind("VEHICLE_ID|", 0) == 0) {
		 unsigned key=0,id=0;
		 if (std::sscanf(line.c_str(),"VEHICLE_ID|%u|%u",&key,&id)==2 && key && id && id<=65535) {
			 std::scoped_lock lock(m_mutex);
             m_localVehicleIdsByKey[static_cast<uint32_t>(key)] = static_cast<uint16_t>(id);
			 if (key==m_localVehicleKey) {
                 m_localVehicleNetworkId=static_cast<uint16_t>(id);




                 const uint16_t localId = m_localId.load();
                 for (auto& vehicle : m_remoteVehicles) {
                     if (vehicle.id == static_cast<uint16_t>(id)) {
                         vehicle.ownerId = localId;
                         vehicle.driverSeat = 0;
                     }
                 }
             }
		 }
		 return;
	 }
	 if (line.rfind("TIME_OWNER|", 0) == 0) {
 unsigned id = 0;
 if (std::sscanf(line.c_str(), "TIME_OWNER|%u", &id) == 1 && id <= 65535) {
 m_timeAuthorityId = (uint16_t)id;
 if (id && id == m_localId.load()) m_forceGameTimeSend = true;
 }
 return;
 }
 if (line.rfind("TIME|", 0) == 0) {
 unsigned hour = 0, minute = 0, day = 0;
 if (std::sscanf(line.c_str(), "TIME|%u|%u|%u", &hour, &minute, &day) == 3 && hour <= 23 && minute <= 59 && day >= 1 && day <= 31) {
 RemoteGameTime t{static_cast<uint8_t>(hour), static_cast<uint8_t>(minute), static_cast<uint8_t>(day)};
 if (!m_hasRemoteGameTime || t.hour != m_lastRemoteGameTime.hour || t.minute != m_lastRemoteGameTime.minute || t.day != m_lastRemoteGameTime.day) {
 std::scoped_lock l(m_mutex); m_gameTimes.push_back(t); m_lastRemoteGameTime = t; m_hasRemoteGameTime = true;
 }
 }
 return;
 }
 if (line.rfind("NPC_VITALS|", 0) == 0) {
 RemoteNpcVitals e{}; unsigned owner = 0;
 if (std::sscanf(line.c_str(), "NPC_VITALS|%u|%u|%u|%hu|%hu", &e.id, &owner, &e.localRef, &e.health, &e.armour) == 5 && e.id && owner <= 65535 && e.localRef && e.health <= 100 && e.armour <= 100) {
 e.ownerId = (uint16_t)owner; std::scoped_lock l(m_mutex); m_npcVitals.push_back(e);
 }
 return;
 }
 if (line.rfind("PLAYER_VITALS|", 0) == 0) {
 RemotePlayerVitals e{};
 if (std::sscanf(line.c_str(), "PLAYER_VITALS|%hu|%hu|%hu", &e.id, &e.health, &e.armour) == 3 && e.id && e.health <= 100 && e.armour <= 100) {
 std::scoped_lock l(m_mutex); m_playerVitals.push_back(e);
 if (e.id != m_localId.load()) {
 auto it = std::find_if(m_remotePlayers.begin(), m_remotePlayers.end(), [&](const RemotePlayer& p){ return p.id == e.id; });
 if (it != m_remotePlayers.end()) { it->health = e.health; it->armour = e.armour; }
 }
 }
 return;
 }
 if (line.rfind("NPC_DAMAGE|", 0) == 0) {
 RemoteNpcDamage e{}; unsigned w = 0;
 if (std::sscanf(line.c_str(), "NPC_DAMAGE|%u|%u|%hu|%hu|%u", &e.id, &e.localRef, &e.health, &e.armour, &w) == 5 && e.id && e.localRef && e.health <= 100 && e.armour <= 100 && w <= 0x3A) {
 e.weapon = (uint8_t)w; std::scoped_lock l(m_mutex); m_npcDamages.push_back(e);
 }
 return;
 }
 if (line.rfind("NPC_TEST|", 0) == 0) {
 NpcSpawnTest t{}; int threat = 1; unsigned weapon = 1;
 int n = std::sscanf(line.c_str(), "NPC_TEST|%d|%f|%f|%f|%f|%u|%d", &t.modelId, &t.x, &t.y, &t.z, &t.heading, &weapon, &threat);
 if (n != 7) n = std::sscanf(line.c_str(), "NPC_TEST|%d|%f|%f|%f|%f|%d", &t.modelId, &t.x, &t.y, &t.z, &t.heading, &threat);
 if ((n == 7 || n == 6) && weapon <= 0x3A) {
 t.weapon = (uint8_t)weapon; t.threat = threat != 0;
 std::scoped_lock l(m_mutex); m_npcSpawnTests.push_back(t);
 __android_log_print(ANDROID_LOG_INFO, kTag, "NPC_TEST packet queued: model=%d weapon=%u pos=(%.1f,%.1f,%.1f)", t.modelId, weapon, t.x, t.y, t.z);
 }
 return;
 }
 if (line.rfind("BONES2|", 0) == 0) {
 std::vector<std::string> f; size_t b = 7;
 while (b <= line.size()) { size_t e = line.find('|', b); if (e == std::string::npos) e = line.size(); f.emplace_back(line.substr(b, e - b)); if (e == line.size()) break; b = e + 1; }
 if (f.size() >= 2) {
 unsigned id = (unsigned)std::strtoul(f[0].c_str(), nullptr, 10), count = (unsigned)std::strtoul(f[1].c_str(), nullptr, 10);
 if (id && id <= 65535 && count <= 19 && f.size() == 2 + count * 5) {
 std::scoped_lock l(m_mutex);
 for (auto& rp : m_remotePlayers) if (rp.id == id) {
 rp.boneCount = (uint8_t)count;
 for (unsigned i = 0; i < count; ++i) {
 size_t o = 2 + i * 5;
 rp.bones[i].index = (uint8_t)std::strtoul(f[o].c_str(), nullptr, 10);
 rp.bones[i].qx = std::strtof(f[o+1].c_str(), nullptr);
 rp.bones[i].qy = std::strtof(f[o+2].c_str(), nullptr);
 rp.bones[i].qz = std::strtof(f[o+3].c_str(), nullptr);
 rp.bones[i].qw = std::strtof(f[o+4].c_str(), nullptr);
 }
 break;
 }
 }
 }
 return;
 }
 if (line.rfind("NPC_NET|", 0) == 0) {
 RemoteNpc n{}; unsigned id=0, owner=0, ref=0, model=0, weapon=0, move=1, vehicle=0, seat=0; char specialName[16]{};
 int c = std::sscanf(line.c_str(), "NPC_NET|%u|%u|%u|%u|%f|%f|%f|%f|%hu|%hu|%u|%u|%u|%u|%15[^|\r\n]",
 &id, &owner, &ref, &model, &n.x, &n.y, &n.z, &n.heading, &n.health, &n.armour, &weapon, &move, &vehicle, &seat, specialName);
 if ((c == 12 || c == 14 || c == 15) && id && owner <= 65535 && ref && model < kModelInfoCapacity && weapon <= 0x3A && move <= 7 && vehicle <= 65535 && seat <= 8 &&
 isFiniteNumber(n.x) && isFiniteNumber(n.y) && isFiniteNumber(n.z) && isFiniteNumber(n.heading)) {
 n.id = id; n.ownerId = (uint16_t)owner; n.localRef = ref; n.modelId = (uint16_t)model;
 n.weapon = (uint8_t)weapon; n.moveState = (uint8_t)move; n.vehicleId = (uint16_t)vehicle; n.vehicleSeat = (uint8_t)seat;
 if (c == 15 && coop_special::IsSpecialNetworkIndex(n.modelId))
 coop_special::NormalizeName(specialName, n.specialModelName, sizeof(n.specialModelName));
 if (!n.specialModelName[0] && n.modelId >= coop_special::kFirstVirtualSpecialIndex && n.modelId <= coop_special::kLastVirtualSpecialIndex)
 coop_special::NormalizeName(coop_special::VirtualSpecialName(n.modelId), n.specialModelName, sizeof(n.specialModelName));
 std::scoped_lock l(m_mutex);
 auto it = std::find_if(m_remoteNpcs.begin(), m_remoteNpcs.end(), [&](const RemoteNpc& q){ return q.id == n.id; });
 n.receivedAtMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
 if (it == m_remoteNpcs.end()) m_remoteNpcs.push_back(n);
 else {
   it->ownerId=n.ownerId; it->localRef=n.localRef; it->modelId=n.modelId;
   std::snprintf(it->specialModelName,sizeof(it->specialModelName),"%s",n.specialModelName);
   it->x=n.x; it->y=n.y; it->z=n.z; it->heading=n.heading; it->health=n.health;
   it->armour=n.armour; it->weapon=n.weapon; it->moveState=n.moveState;
   it->vehicleId=n.vehicleId; it->vehicleSeat=n.vehicleSeat; it->receivedAtMs=n.receivedAtMs;
 }
 }
 return;
 }
 if (line.rfind("NPC_MOTION|", 0) == 0) {
   unsigned id=0, move=0, style=0; RemoteNpc temp{};
   const int c=std::sscanf(line.c_str(),"NPC_MOTION|%u|%f|%f|%f|%f|%f|%f|%u|%u",&id,&temp.vx,&temp.vy,&temp.vz,&temp.currentRotation,&temp.aimingRotation,&temp.lookingDirection,&move,&style);
   if(c==9 && id && move<=7 && style<=255 && isFiniteNumber(temp.vx)&&isFiniteNumber(temp.vy)&&isFiniteNumber(temp.vz)&&isFiniteNumber(temp.currentRotation)&&isFiniteNumber(temp.aimingRotation)&&isFiniteNumber(temp.lookingDirection)) {
     std::scoped_lock l(m_mutex); auto it=std::find_if(m_remoteNpcs.begin(),m_remoteNpcs.end(),[&](const RemoteNpc& q){return q.id==id;});
     if(it!=m_remoteNpcs.end()){it->vx=std::max(-100.0f,std::min(100.0f,temp.vx));it->vy=std::max(-100.0f,std::min(100.0f,temp.vy));it->vz=std::max(-100.0f,std::min(100.0f,temp.vz));it->currentRotation=temp.currentRotation;it->aimingRotation=temp.aimingRotation;it->lookingDirection=temp.lookingDirection;it->moveState=static_cast<uint8_t>(move);it->fightingStyle=static_cast<uint8_t>(style);it->receivedAtMs=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
   }
   return;
 }
 if (line.rfind("NPC_AIM|", 0) == 0) {
   unsigned id=0, aiming=0, firing=0; RemoteNpc temp{};
   const int c=std::sscanf(line.c_str(), "NPC_AIM|%u|%u|%u|%f|%f|%f", &id, &aiming, &firing, &temp.aimX, &temp.aimY, &temp.aimZ);
   if(c==6 && id && aiming<=1 && firing<=1 && (!firing || aiming) &&
      isFiniteNumber(temp.aimX) && isFiniteNumber(temp.aimY) && isFiniteNumber(temp.aimZ) &&
      std::fabs(temp.aimX)<=100000.0f && std::fabs(temp.aimY)<=100000.0f && std::fabs(temp.aimZ)<=100000.0f) {
     std::scoped_lock l(m_mutex);
     auto it=std::find_if(m_remoteNpcs.begin(),m_remoteNpcs.end(),[&](const RemoteNpc& q){return q.id==id;});
     if(it!=m_remoteNpcs.end()) {
       it->aiming=aiming!=0; it->firing=firing!=0;
       it->aimX=temp.aimX; it->aimY=temp.aimY; it->aimZ=temp.aimZ;
       it->receivedAtMs=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
     }
   }
   return;
 }
 if (line.rfind("PLAYER_HIT|", 0) == 0) {
 RemotePlayerHit e{}; unsigned weapon=0, piece=0, direction=0, fightSet=0xFF, fightMove=0xFF;
 if (std::sscanf(line.c_str(), "PLAYER_HIT|%hu|%u|%hu|%u|%u|%u|%u",
       &e.attackerId, &weapon, &e.damage, &piece, &direction, &fightSet, &fightMove) == 7 &&
     e.attackerId && e.attackerId != m_localId.load() && weapon <= 15 && e.damage > 0 && e.damage <= 10000 &&
     piece <= 9 && direction <= 3 &&
     ((fightSet == 0xFF && fightMove == 0xFF) || (fightSet >= 4 && fightSet <= 16 && fightMove <= 4))) {
     e.weapon=static_cast<uint8_t>(weapon); e.piece=static_cast<uint8_t>(piece);
     e.direction=static_cast<uint8_t>(direction); e.fightSet=static_cast<uint8_t>(fightSet);
     e.fightMove=static_cast<uint8_t>(fightMove);
     std::scoped_lock l(m_mutex); m_playerHits.push_back(e);
 }
 return;
 }
 if (line.rfind("NPC_ANIM4|", 0) == 0) {
   std::vector<std::string> f; size_t b=10; while(b<=line.size()){size_t e=line.find('|',b);if(e==std::string::npos)e=line.size();f.emplace_back(line.substr(b,e-b));if(e==line.size())break;b=e+1;}
   if(f.size()>=2){char* e=nullptr;const unsigned id=std::strtoul(f[0].c_str(),&e,10);const unsigned count=std::strtoul(f[1].c_str(),&e,10);
     if(id&&count<=32&&f.size()==2+count*9){std::array<AnimationAssociationState,64> states{};bool valid=true;
       for(unsigned i=0;i<count;++i){const size_t o=2+i*9;auto& a=states[i];const unsigned long group=std::strtoul(f[o].c_str(),&e,10);const long anim=std::strtol(f[o+1].c_str(),&e,10);const unsigned long flags=std::strtoul(f[o+2].c_str(),&e,10);a.group=static_cast<uint16_t>(group);a.animationId=static_cast<int16_t>(anim);a.flags=static_cast<uint16_t>(flags);a.blend=std::strtof(f[o+3].c_str(),&e);a.blendDelta=std::strtof(f[o+4].c_str(),&e);a.currentTime=std::strtof(f[o+5].c_str(),&e);a.speed=std::strtof(f[o+6].c_str(),&e);a.timeStep=std::strtof(f[o+7].c_str(),&e);const unsigned long hash=std::strtoul(f[o+8].c_str(),&e,10);a.hierarchyHash=static_cast<uint32_t>(hash);
         valid &= group<=65535&&anim>=-32768&&anim<=32767&&flags<=65535&&hash<=0xFFFFFFFFul&&std::isfinite(a.blend)&&std::isfinite(a.blendDelta)&&std::isfinite(a.currentTime)&&std::isfinite(a.speed)&&std::isfinite(a.timeStep)&&std::fabs(a.blend)<=100&&std::fabs(a.blendDelta)<=100&&std::fabs(a.currentTime)<=100000&&std::fabs(a.speed)<=100&&std::fabs(a.timeStep)<=100;
       }
       if(valid){std::scoped_lock l(m_mutex);auto it=std::find_if(m_remoteNpcs.begin(),m_remoteNpcs.end(),[&](const RemoteNpc& q){return q.id==id;});if(it!=m_remoteNpcs.end()){it->animationAssociationCount=static_cast<uint8_t>(count);it->animationAssociations=states;}}
     }
   }
   return;
 }
 if (line.rfind("MISSION_MARKER|", 0) == 0) {
 RemoteMissionMarker m{}; unsigned owner=0, slot=0, type=0, display=0, sprite=0, scale=1, color=0;
 int c = std::sscanf(line.c_str(), "MISSION_MARKER|%u|%u|%u|%u|%u|%u|%u|%u|%f|%f|%f|%f",
     &m.id, &owner, &slot, &type, &display, &sprite, &scale, &color,
     &m.x, &m.y, &m.z, &m.radius);
 if (c == 12 && m.id && owner <= 65535 && slot <= 65535 && type <= 8 &&
     (display == 1 || display == 3) && sprite < kRadarSpriteCount &&
     scale <= 65535 && std::isfinite(m.x) && std::isfinite(m.y) &&
     std::isfinite(m.z) && std::isfinite(m.radius)) {
   m.ownerId = static_cast<uint16_t>(owner);
   m.sourceSlot = static_cast<uint16_t>(slot);
   m.type = static_cast<uint8_t>(type);
   m.display = static_cast<uint8_t>(display);
   m.sprite = static_cast<uint8_t>(sprite);
   m.scale = static_cast<uint16_t>(scale);
   m.color = color;
   m.radius = std::max(0.1f, std::min(100.0f, m.radius));
   std::scoped_lock l(m_mutex);
   auto it = std::find_if(m_remoteMissionMarkers.begin(), m_remoteMissionMarkers.end(),
       [&](const RemoteMissionMarker& q){ return q.id == m.id; });
   if (it == m_remoteMissionMarkers.end()) m_remoteMissionMarkers.push_back(m);
   else *it = m;
 }
 return;
 }
 if (line.rfind("MISSION_MARKER_REMOVE|", 0) == 0) {
 unsigned id = 0;
 if (std::sscanf(line.c_str(), "MISSION_MARKER_REMOVE|%u", &id) == 1 && id) {
   std::scoped_lock l(m_mutex);
   m_remoteMissionMarkers.erase(std::remove_if(m_remoteMissionMarkers.begin(), m_remoteMissionMarkers.end(),
     [&](const RemoteMissionMarker& m){ return m.id == id; }), m_remoteMissionMarkers.end());
   m_removedMissionMarkers.push_back(id);
 }
 return;
 }
 if (line.rfind("NPC_REMOVE|", 0) == 0) {
 unsigned id = 0;
 if (std::sscanf(line.c_str(), "NPC_REMOVE|%u", &id) == 1 && id) {
 std::scoped_lock l(m_mutex);
 m_remoteNpcs.erase(std::remove_if(m_remoteNpcs.begin(), m_remoteNpcs.end(), [&](const RemoteNpc& n){ return n.id == id; }), m_remoteNpcs.end());
 m_removedNpcs.push_back(id);
 }
 return;
 }
 if (line.rfind("REMOVE|", 0) == 0) {
 RemotePlayerRemoved e{};
 if (std::sscanf(line.c_str(), "REMOVE|%hu", &e.id) == 1) {
 std::scoped_lock l(m_mutex);
 m_remotePlayers.erase(std::remove_if(m_remotePlayers.begin(), m_remotePlayers.end(), [&](const RemotePlayer& p){ return p.id == e.id; }), m_remotePlayers.end());
 m_pendingRemoteClothes.erase(e.id);
 m_removedPlayers.push_back(e);
 }
 return;
 }
 if (line.rfind("EXPX|", 0) == 0) {
     RemoteExplosion e{};
     unsigned type = 0;
     if (std::sscanf(line.c_str(), "EXPX|%hu|%u|%f|%f|%f", &e.id, &type, &e.x, &e.y, &e.z) == 5 &&
         e.id != 0 && type <= 12 && isFiniteNumber(e.x) && isFiniteNumber(e.y) && isFiniteNumber(e.z) &&
         std::fabs(e.x) <= 3000.0f && std::fabs(e.y) <= 3000.0f && std::fabs(e.z) <= 10000.0f) {
         e.type = static_cast<uint8_t>(type);
         std::scoped_lock lock(m_mutex);
         if (e.id != m_localId.load()) {
             if (m_explosions.size() >= 64) m_explosions.erase(m_explosions.begin());
             m_explosions.push_back(e);
         }
     }
     return;
 }
 if (line.rfind("SHOTX|", 0) == 0) {
 RemoteShot e{}; unsigned kind = 0, w = 0, group = 0, anim = 0; unsigned short vehicleId = 0;
 const size_t pipeCount = static_cast<size_t>(std::count(line.begin(), line.end(), '|'));
 int c = 0;
 if (pipeCount >= 13) {
     c = std::sscanf(line.c_str(), "SHOTX|%hu|%u|%u|%hu|%f|%f|%f|%f|%f|%f|%f|%u|%u",
         &e.id, &kind, &w, &vehicleId, &e.x, &e.y, &e.z, &e.dx, &e.dy, &e.dz, &e.force, &group, &anim);
     if (c == 13) e.vehicleId = static_cast<uint16_t>(vehicleId);
     else c = 0;
 } else {
     c = std::sscanf(line.c_str(), "SHOTX|%hu|%u|%u|%f|%f|%f|%f|%f|%f|%f|%u|%u",
         &e.id, &kind, &w, &e.x, &e.y, &e.z, &e.dx, &e.dy, &e.dz, &e.force, &group, &anim);
 }
 if ((c == 13 || c == 12) && e.id && kind <= 2 && w <= 255 && std::isfinite(e.force) && e.force >= 0.0f && e.force <= 5.0f &&
     isFiniteNumber(e.x) && isFiniteNumber(e.y) && isFiniteNumber(e.z) && isFiniteNumber(e.dx) && isFiniteNumber(e.dy) && isFiniteNumber(e.dz)) {
     e.kind = static_cast<uint8_t>(kind); e.weapon = static_cast<uint8_t>(w); e.force = std::min(5.0f, e.force);
     if (e.kind != 2) e.vehicleId = 0;
     if (group && group <= 0xFFFF && (anim == kAnimationFireId || anim == kAnimationCrouchFireId)) { e.animGroup = static_cast<uint16_t>(group); e.animId = static_cast<uint16_t>(anim); }
     std::scoped_lock l(m_mutex); if (e.id != m_localId.load()) m_shots.push_back(e);
 }
 return;
 }
 if (line.rfind("SHOT|", 0) == 0) {
 RemoteShot e{}; unsigned w = 0, group = 0, anim = 0;
 const int shotFields = std::sscanf(line.c_str(), "SHOT|%hu|%u|%f|%f|%f|%f|%f|%f|%u|%u", &e.id, &w, &e.x, &e.y, &e.z, &e.dx, &e.dy, &e.dz, &group, &anim);
 if ((shotFields == 8 || shotFields == 10) && w <= 255 &&
 isFiniteNumber(e.x) && isFiniteNumber(e.y) && isFiniteNumber(e.z) && isFiniteNumber(e.dx) && isFiniteNumber(e.dy) && isFiniteNumber(e.dz)) {
 if (shotFields == 10 && group != 0 && group <= 0xFFFF && (static_cast<int>(anim) == kAnimationFireId || static_cast<int>(anim) == kAnimationCrouchFireId)) {
    e.animGroup = static_cast<uint16_t>(group); e.animId = static_cast<uint16_t>(anim);
 }
 e.kind = 3; e.weapon = (uint8_t)w; std::scoped_lock l(m_mutex); if (e.id != m_localId.load()) m_shots.push_back(e);
 }
 return;
 }
 if (line.rfind("DAMAGE|", 0) == 0) {
 RemoteDamage e{}; unsigned w = 0;
 if (std::sscanf(line.c_str(), "DAMAGE|%hu|%hu|%hu|%u", &e.targetId, &e.health, &e.armour, &w) == 4 && w <= 255 && e.health <= 100 && e.armour <= 100) {
 e.weapon = (uint8_t)w;
 WeaponFileLog("DAMAGE_PACKET_RECEIVE target=%hu health=%hu armour=%hu weapon=%u", e.targetId, e.health, e.armour, e.weapon);
 std::scoped_lock l(m_mutex); m_damages.push_back(e);
 }
 return;
 }
 if (line.rfind("VEHICLE_ENTER|", 0) == 0) {
 RemoteVehicleEnter e{}; unsigned seat = 0;
 if (std::sscanf(line.c_str(), "VEHICLE_ENTER|%hu|%hu|%u", &e.id, &e.vehicleId, &seat) == 3 &&
     e.id != 0 && e.vehicleId != 0 && seat <= 8 && e.id != m_localId.load()) {
     e.seat = static_cast<uint8_t>(seat);
     std::scoped_lock l(m_mutex);
     m_vehicleEnters.push_back(e);
 }
 return;
 }
 if (line.rfind("VEHICLE_REMOVE|", 0) == 0) {
 uint16_t id = 0;
 if (std::sscanf(line.c_str(), "VEHICLE_REMOVE|%hu", &id) == 1) {
 std::scoped_lock l(m_mutex);
 m_remoteVehicles.erase(std::remove_if(m_remoteVehicles.begin(), m_remoteVehicles.end(), [&](const RemoteVehicle& v){ return v.id == id; }), m_remoteVehicles.end());
 m_pendingVehicleLooks.erase(id);
 m_pendingVehicleExtended.erase(id);
 for (auto it = m_localVehicleIdsByKey.begin(); it != m_localVehicleIdsByKey.end();) {
     if (it->second == id) it = m_localVehicleIdsByKey.erase(it); else ++it;
 }
 if (m_localVehicleNetworkId == id) m_localVehicleNetworkId = 0;
 m_removedVehicles.push_back(id);
 }
 return;
 }
 if (line.rfind("VEHICLE_EX|", 0) == 0) {
     std::vector<std::string> fields;
     size_t begin = 11;
     while (begin <= line.size()) {
         size_t end = line.find('|', begin);
         if (end == std::string::npos) end = line.size();
         fields.emplace_back(line.substr(begin, end - begin));
         if (end == line.size()) break;
         begin = end + 1;
     }
     if (fields.size() != 20) return;
     auto integer = [&](size_t i, long lo, long hi, long& out) {
         char* end = nullptr; out = std::strtol(fields[i].c_str(), &end, 10);
         return end && *end == '\0' && out >= lo && out <= hi;
     };
     auto real = [&](size_t i, float& out) {
         char* end = nullptr; out = std::strtof(fields[i].c_str(), &end);
         return end && *end == '\0' && std::isfinite(out);
     };
     long id=0, tertiary=0, quaternary=0, extraA=0, extraB=0, handbrake=0;
     long gearValid=0, lightValid=0, lightEnabled=0, bikeValid=0, bmxValid=0;
     VehicleExtendedState e{};
     if (!integer(0,1,65535,id) || !integer(1,0,255,tertiary) || !integer(2,0,255,quaternary) ||
         !integer(3,-128,127,extraA) || !integer(4,-128,127,extraB) || !integer(5,0,1,handbrake) ||
         !integer(6,0,1,gearValid) || !real(7,e.landingGear) || !integer(8,0,1,lightValid) ||
         !real(9,e.searchLightX) || !real(10,e.searchLightY) || !real(11,e.searchLightZ) ||
         !real(12,e.searchLightIntensity) || !integer(13,0,1,lightEnabled) ||
         !integer(14,0,1,bikeValid) || !real(15,e.bikeLean) || !real(16,e.bikeLeanForward) ||
         !integer(17,0,1,bmxValid) || !real(18,e.bmxSprintLean) || !real(19,e.bmxPedallingCharge)) return;
     e.tertiaryColor=static_cast<uint8_t>(tertiary); e.quaternaryColor=static_cast<uint8_t>(quaternary);
     e.extraA=static_cast<int8_t>(extraA); e.extraB=static_cast<int8_t>(extraB); e.handbrake=static_cast<uint8_t>(handbrake);
     e.landingGearValid=gearValid!=0; e.searchLightValid=lightValid!=0; e.searchLightEnabled=static_cast<uint8_t>(lightEnabled);
     e.bikeLeanValid=bikeValid!=0; e.bmxValid=bmxValid!=0; e.valid=true;
     e.landingGear=std::max(0.0f,std::min(1.0f,e.landingGear));
     e.searchLightIntensity=std::max(0.0f,std::min(100.0f,e.searchLightIntensity));
     e.bikeLean=std::max(-10.0f,std::min(10.0f,e.bikeLean));
     e.bikeLeanForward=std::max(-10.0f,std::min(10.0f,e.bikeLeanForward));
     e.bmxSprintLean=std::max(-10.0f,std::min(10.0f,e.bmxSprintLean));
     e.bmxPedallingCharge=std::max(0.0f,std::min(100.0f,e.bmxPedallingCharge));
     std::scoped_lock lock(m_mutex);
     auto it=std::find_if(m_remoteVehicles.begin(),m_remoteVehicles.end(),[&](const RemoteVehicle& v){return v.id==static_cast<uint16_t>(id);});
     if (it!=m_remoteVehicles.end()) it->extended=e; else m_pendingVehicleExtended[static_cast<uint16_t>(id)]=e;
     return;
 }
 if (line.rfind("VEHICLE_LOOK|", 0) == 0) {
     std::vector<std::string> fields;
     size_t begin = 13;
     while (begin <= line.size()) {
         size_t end = line.find('|', begin);
         if (end == std::string::npos) end = line.size();
         fields.emplace_back(line.substr(begin, end - begin));
         if (end == line.size()) break;
         begin = end + 1;
     }
     if (fields.size() != 17) return;
     char* end = nullptr;
     const unsigned long id = std::strtoul(fields[0].c_str(), &end, 10);
     if (!end || *end || id == 0 || id > 65535) return;
     const long paintjob = std::strtol(fields[1].c_str(), &end, 10);
     if (!end || *end || paintjob < -1 || paintjob > 3) return;
     VehicleAppearanceState appearance{};
     appearance.paintjob = static_cast<int8_t>(paintjob);
     for (size_t i = 0; i < appearance.upgrades.size(); ++i) {
         const long value = std::strtol(fields[i + 2].c_str(), &end, 10);
         if (!end || *end || value < -1 || value >= static_cast<long>(kModelInfoCapacity)) return;
         appearance.upgrades[i] = static_cast<int16_t>(value);
     }
     appearance.valid = true;
     std::scoped_lock lock(m_mutex);
     auto it = std::find_if(m_remoteVehicles.begin(), m_remoteVehicles.end(),
         [&](const RemoteVehicle& vehicle) { return vehicle.id == static_cast<uint16_t>(id); });
     if (it != m_remoteVehicles.end()) it->appearance = appearance;
     else m_pendingVehicleLooks[static_cast<uint16_t>(id)] = appearance;
     return;
 }
 if (line.rfind("VEHICLE|", 0) == 0) {
 RemoteVehicle v{}; unsigned model=0, owner=0, seat=0, primary=0, secondary=0, engine=0, lights=0, siren=0, lock=0, tyres=0, trailer=0, misc=0;
 int n = std::sscanf(line.c_str(), "VEHICLE|%hu|%u|%u|%u|%f|%f|%f|%f|%f|%f|%f|%f|%u|%u|%u|%u|%u|%u|%f|%u|%u|%u|%u|%u|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%u|%f|%f|%f|%f|%f",
 &v.id, &model, &owner, &seat, &v.x, &v.y, &v.z, &v.heading, &v.vx, &v.vy, &v.vz, &v.health,
 &primary, &secondary, &engine, &lights, &siren, &lock, &v.dirt, &tyres, &v.doorStatus, &v.lightStatus, &v.panelStatus, &trailer,
 &v.rightX, &v.rightY, &v.rightZ, &v.upX, &v.upY, &v.upZ, &v.atX, &v.atY, &v.atZ,
 &v.steering,&v.gasPedal,&v.brakePedal,&misc,&v.turretVertical,&v.turretHorizontal,&v.turnVx,&v.turnVy,&v.turnVz);
	 if ((n == 12 || n == 24 || n == 33 || n == 39 || n == 42) && v.id && model >= 400 && model < kModelInfoCapacity && owner <= 65535 && seat <= 7 &&
 isFiniteNumber(v.x) && isFiniteNumber(v.y) && isFiniteNumber(v.z) && isFiniteNumber(v.heading) && isFiniteNumber(v.vx) && isFiniteNumber(v.vy) && isFiniteNumber(v.vz) && std::isfinite(v.health) &&
 (n < 33 || (isFiniteNumber(v.rightX) && isFiniteNumber(v.rightY) && isFiniteNumber(v.rightZ) && isFiniteNumber(v.upX) && isFiniteNumber(v.upY) && isFiniteNumber(v.upZ) && isFiniteNumber(v.atX) && isFiniteNumber(v.atY) && isFiniteNumber(v.atZ))) &&
 (n < 39 || (isFiniteNumber(v.steering) && isFiniteNumber(v.gasPedal) && isFiniteNumber(v.brakePedal) && isFiniteNumber(v.turretVertical) && isFiniteNumber(v.turretHorizontal))) &&
     (n != 42 || (isFiniteNumber(v.turnVx) && isFiniteNumber(v.turnVy) && isFiniteNumber(v.turnVz)))) {
 v.modelId = (uint16_t)model; v.ownerId = (uint16_t)owner; v.driverSeat = (uint8_t)seat;
 v.vx = std::max(-150.0f, std::min(150.0f, v.vx));
 v.vy = std::max(-150.0f, std::min(150.0f, v.vy));
 v.vz = std::max(-150.0f, std::min(150.0f, v.vz));
 v.receivedAtMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
 if (n >= 24) {
 v.primaryColor=(uint8_t)primary; v.secondaryColor=(uint8_t)secondary; v.engineOn=(uint8_t)engine; v.lightsOn=(uint8_t)lights; v.sirenOn=(uint8_t)siren;
 v.doorLock=(uint8_t)lock; v.tyreStatus=(uint8_t)tyres; v.trailerId=(uint16_t)trailer; if (n >= 33) v.orientationValid = true;
 if (n >= 39) {
   v.steering=std::max(-1.0f,std::min(1.0f,v.steering));
   v.gasPedal=std::max(-1.0f,std::min(1.0f,v.gasPedal));
   v.brakePedal=std::max(0.0f,std::min(1.0f,v.brakePedal));
   v.miscComponentAngle=static_cast<uint16_t>(std::min<unsigned>(misc,65535));
   v.turretVertical=std::max(-10.0f,std::min(10.0f,v.turretVertical));
   v.turretHorizontal=std::max(-10.0f,std::min(10.0f,v.turretHorizontal));
 }
 if (n == 42) {
   v.turnVx=std::max(-20.0f,std::min(20.0f,v.turnVx));
   v.turnVy=std::max(-20.0f,std::min(20.0f,v.turnVy));
   v.turnVz=std::max(-20.0f,std::min(20.0f,v.turnVz));
 }
 }
 std::scoped_lock l(m_mutex);
 auto pendingLook = m_pendingVehicleLooks.find(v.id);
 if (pendingLook != m_pendingVehicleLooks.end()) {
     v.appearance = pendingLook->second;
     m_pendingVehicleLooks.erase(pendingLook);
 }
 auto pendingExtended = m_pendingVehicleExtended.find(v.id);
 if (pendingExtended != m_pendingVehicleExtended.end()) {
     v.extended = pendingExtended->second;
     m_pendingVehicleExtended.erase(pendingExtended);
 }
 auto it = std::find_if(m_remoteVehicles.begin(), m_remoteVehicles.end(), [&](const RemoteVehicle& q){ return q.id == v.id; });
 if (it == m_remoteVehicles.end()) m_remoteVehicles.push_back(v);
 else {
     if (!v.appearance.valid) v.appearance = it->appearance;
     if (!v.extended.valid) v.extended = it->extended;
     *it = v;
 }
 }
 return;
 }
 if (line.rfind("NAME|", 0) == 0) {
 uint16_t id = 0; unsigned color = 0; char remoteName[15] = {0};
 const int parsed = std::sscanf(line.c_str(), "NAME|%hu|%14[^|\r\n]|%u", &id, remoteName, &color);
 if ((parsed == 2 || parsed == 3) && id != 0) {
 std::scoped_lock l(m_mutex);
 auto it = std::find_if(m_remotePlayers.begin(), m_remotePlayers.end(), [&](const RemotePlayer& x){ return x.id == id; });
 if (it != m_remotePlayers.end()) { std::snprintf(it->name, sizeof(it->name), "%s", remoteName); if (parsed == 3 && color <= 7) it->colorIndex = (uint8_t)color; }
 }
 return;
 }
 if (line.rfind("ANIM4|", 0) == 0) {
 std::vector<std::string> f; size_t b = 6;
 while (b <= line.size()) { size_t e = line.find('|', b); if (e == std::string::npos) e = line.size(); f.emplace_back(line.substr(b, e - b)); if (e == line.size()) break; b = e + 1; }
 if (f.size() >= 2) {
 char* e = nullptr; const unsigned id = std::strtoul(f[0].c_str(), &e, 10); const unsigned count = std::strtoul(f[1].c_str(), &e, 10);
 if (id && count <= 64 && f.size() == 2 + count * 9) {
 std::array<AnimationAssociationState,64> states{}; bool valid = true;
 for (unsigned i = 0; i < count; ++i) {
 const size_t o = 2 + i * 9; auto& a = states[i];
 const unsigned long group = std::strtoul(f[o].c_str(), &e, 10);
 const long animId = std::strtol(f[o+1].c_str(), &e, 10);
 const unsigned long flags = std::strtoul(f[o+2].c_str(), &e, 10);
 a.group = static_cast<uint16_t>(group); a.animationId = static_cast<int16_t>(animId); a.flags = static_cast<uint16_t>(flags);
 a.blend = std::strtof(f[o+3].c_str(), &e); a.blendDelta = std::strtof(f[o+4].c_str(), &e);
 a.currentTime = std::strtof(f[o+5].c_str(), &e); a.speed = std::strtof(f[o+6].c_str(), &e); a.timeStep = std::strtof(f[o+7].c_str(), &e);
 const unsigned long hash = std::strtoul(f[o+8].c_str(), &e, 10); a.hierarchyHash = static_cast<uint32_t>(hash);
 valid &= group <= 65535 && animId >= -32768 && animId <= 32767 && flags <= 65535 && hash <= 0xFFFFFFFFul &&
 std::isfinite(a.blend) && std::isfinite(a.blendDelta) && std::isfinite(a.currentTime) && std::isfinite(a.speed) && std::isfinite(a.timeStep) &&
 std::fabs(a.blend) <= 100 && std::fabs(a.blendDelta) <= 100 && std::fabs(a.currentTime) <= 100000 && std::fabs(a.speed) <= 100 && std::fabs(a.timeStep) <= 100;
 }
 if (valid) {
 std::scoped_lock l(m_mutex);
 auto it = std::find_if(m_remotePlayers.begin(), m_remotePlayers.end(), [&](const RemotePlayer& x){ return x.id == id; });
 if (it != m_remotePlayers.end()) { it->animationAssociationCount = static_cast<uint8_t>(count); it->animationAssociations = states; }
 }
 }
 }
 return;
 }
 if (line.rfind("ANIM3|", 0) == 0) {
 std::vector<std::string> f; size_t b = 6;
 while (b <= line.size()) { size_t e = line.find('|', b); if (e == std::string::npos) e = line.size(); f.emplace_back(line.substr(b, e - b)); if (e == line.size()) break; b = e + 1; }
 if (f.size() >= 2) {
 char* e = nullptr; const unsigned id = std::strtoul(f[0].c_str(), &e, 10); const unsigned count = std::strtoul(f[1].c_str(), &e, 10);
 if (id && count <= 64 && f.size() == 2 + count * 8) {
 std::array<AnimationAssociationState,64> states{}; bool valid = true;
 for (unsigned i = 0; i < count; ++i) {
 const size_t o = 2 + i * 8; auto& a = states[i];
 a.group = static_cast<uint16_t>(std::strtoul(f[o].c_str(), &e, 10));
 a.animationId = static_cast<int16_t>(std::strtol(f[o+1].c_str(), &e, 10));
 a.flags = static_cast<uint16_t>(std::strtoul(f[o+2].c_str(), &e, 10));
 a.blend = std::strtof(f[o+3].c_str(), &e); a.blendDelta = std::strtof(f[o+4].c_str(), &e);
 a.currentTime = std::strtof(f[o+5].c_str(), &e); a.speed = std::strtof(f[o+6].c_str(), &e); a.timeStep = std::strtof(f[o+7].c_str(), &e);
 valid &= std::isfinite(a.blend) && std::isfinite(a.blendDelta) && std::isfinite(a.currentTime) && std::isfinite(a.speed) && std::isfinite(a.timeStep);
 }
 if (valid) {
 std::scoped_lock l(m_mutex);
 auto it = std::find_if(m_remotePlayers.begin(), m_remotePlayers.end(), [&](const RemotePlayer& x){ return x.id == id; });
 if (it != m_remotePlayers.end()) { it->animationAssociationCount = static_cast<uint8_t>(count); it->animationAssociations = states; }
 }
 }
 }
 return;
 }
 if (line.rfind("ANIM2|", 0) == 0) {
 std::vector<std::string> f; size_t b = 6;
 while (b <= line.size()) { size_t e = line.find('|', b); if (e == std::string::npos) e = line.size(); f.emplace_back(line.substr(b, e - b)); if (e == line.size()) break; b = e + 1; }
 if (f.size() == 11) {
 char* e = nullptr; const unsigned id = std::strtoul(f[0].c_str(), &e, 10);
 AnimationProfile profile{};
 profile.sequence = static_cast<uint32_t>(std::strtoul(f[1].c_str(), &e, 10));
 profile.locomotion = static_cast<uint8_t>(std::strtoul(f[2].c_str(), &e, 10));
 profile.action = static_cast<AnimationAction>(std::strtoul(f[3].c_str(), &e, 10));
 profile.flags = static_cast<uint16_t>(std::strtoul(f[4].c_str(), &e, 10));
 profile.weaponState = static_cast<uint8_t>(std::strtoul(f[5].c_str(), &e, 10));
 profile.animGroup = static_cast<uint16_t>(std::strtoul(f[6].c_str(), &e, 10));
 profile.phase = std::strtof(f[7].c_str(), &e); profile.blend = std::strtof(f[8].c_str(), &e);
 profile.aimYaw = std::strtof(f[9].c_str(), &e); profile.aimPitch = std::strtof(f[10].c_str(), &e);
 if (id && profile.locomotion <= kMoveSprint && static_cast<unsigned>(profile.action) <= static_cast<unsigned>(AnimationAction::Swim) &&
 std::isfinite(profile.phase) && std::isfinite(profile.blend) && std::isfinite(profile.aimYaw) && std::isfinite(profile.aimPitch)) {
 profile.phase = std::max(0.0f, std::min(1.0f, profile.phase)); profile.blend = std::max(0.0f, std::min(1.0f, profile.blend));
 std::scoped_lock l(m_mutex);
 auto it = std::find_if(m_remotePlayers.begin(), m_remotePlayers.end(), [&](const RemotePlayer& x){ return x.id == id; });
 if (it != m_remotePlayers.end() && profile.sequence >= it->animation.sequence) { it->animation = profile; it->animationState = profile.locomotion; }
 }
 }
 return;
 }
 if (line.rfind("PLAYER|", 0) != 0) return;
 std::vector<std::string> fields;
 size_t begin = 7;
 while (begin <= line.size()) {
 size_t end = line.find('|', begin);
 if (end == std::string::npos) end = line.size();
 fields.emplace_back(line.substr(begin, end - begin));
 if (end == line.size()) break;
 begin = end + 1;
 }

 RemotePlayer p{};
 unsigned a = 0, w = 0, vehicleId = 0, vehicleSeat = 0;
 bool hasName = false;
 auto get = [&](size_t i) -> const char* { return i < fields.size() ? fields[i].c_str() : ""; };

 if (fields.size() >= 12) {
 char* e = nullptr;
 p.id = (uint16_t)std::strtoul(get(0), &e, 10); if (!e || *e) return;
 const unsigned long parsedModel = std::strtoul(get(1), &e, 10); if (!e || *e) return;
 if (parsedModel >= kModelInfoCapacity && !(parsedModel >= coop_special::kFirstVirtualSpecialIndex && parsedModel <= coop_special::kLastVirtualSpecialIndex)) { WeaponFileLog("REMOTE_MODEL_REJECT id=%hu model=%lu fallback=0", p.id, parsedModel); p.modelId = 0; }
 else { p.modelId = static_cast<uint16_t>(parsedModel); }
 a = (unsigned)std::strtoul(get(2), &e, 10); if (!e || *e) return;
 p.x = std::strtof(get(3), &e); if (!e || *e) return;
 p.y = std::strtof(get(4), &e); if (!e || *e) return;
 p.z = std::strtof(get(5), &e); if (!e || *e) return;
 p.heading = std::strtof(get(6), &e); if (!e || *e) return;
 p.health = (uint16_t)std::strtoul(get(7), &e, 10); if (!e || *e) return;
 p.armour = (uint16_t)std::strtoul(get(8), &e, 10); if (!e || *e) return;
 w = (unsigned)std::strtoul(get(9), &e, 10); if (!e || *e) return;
 vehicleId = (unsigned)std::strtoul(get(10), &e, 10); if (!e || *e) return;
 vehicleSeat = (unsigned)std::strtoul(get(11), &e, 10); if (!e || *e) return;
 if (fields.size() >= 13 && !fields[12].empty()) { std::snprintf(p.name, sizeof(p.name), "%s", fields[12].c_str()); hasName = true; }
 if (fields.size() >= 14 && !fields[13].empty()) {
 unsigned parsedColor = (unsigned)std::strtoul(get(13), &e, 10);
 if (e && *e == '\0' && parsedColor <= 7) p.colorIndex = (uint8_t)parsedColor;
 }
 if (fields.size() >= 15 && coop_special::IsSpecialNetworkIndex(p.modelId))
 coop_special::NormalizeName(get(14), p.specialModelName, sizeof(p.specialModelName));
 if (!p.specialModelName[0] && p.modelId >= coop_special::kFirstVirtualSpecialIndex && p.modelId <= coop_special::kLastVirtualSpecialIndex)
 coop_special::NormalizeName(coop_special::VirtualSpecialName(p.modelId), p.specialModelName, sizeof(p.specialModelName));
 if (fields.size() >= 16) {
     char* areaEnd = nullptr;
     const unsigned long parsedArea = std::strtoul(get(15), &areaEnd, 10);
     if (areaEnd && *areaEnd == '\0' && parsedArea <= 255) p.areaCode = static_cast<uint8_t>(parsedArea);
 }
 if (fields.size() >= 17) {
     char* wantedEnd = nullptr;
     const unsigned long parsedWanted = std::strtoul(get(16), &wantedEnd, 10);
     if (wantedEnd && *wantedEnd == '\0' && parsedWanted <= 6) p.wanted = static_cast<uint8_t>(parsedWanted);
 }
 if (fields.size() >= 20) {
     char* velocityEnd = nullptr;
     p.vx = std::strtof(get(17), &velocityEnd); if (!velocityEnd || *velocityEnd || !std::isfinite(p.vx)) return;
     p.vy = std::strtof(get(18), &velocityEnd); if (!velocityEnd || *velocityEnd || !std::isfinite(p.vy)) return;
     p.vz = std::strtof(get(19), &velocityEnd); if (!velocityEnd || *velocityEnd || !std::isfinite(p.vz)) return;
     p.vx=std::max(-150.0f,std::min(150.0f,p.vx));
     p.vy=std::max(-150.0f,std::min(150.0f,p.vy));
     p.vz=std::max(-150.0f,std::min(150.0f,p.vz));
 }
 } else if (fields.size() >= 8) {
 char* e = nullptr;
 p.id = (uint16_t)std::strtoul(get(0), &e, 10); if (!e || *e) return;
 p.x = std::strtof(get(1), &e); if (!e || *e) return;
 p.y = std::strtof(get(2), &e); if (!e || *e) return;
 p.z = std::strtof(get(3), &e); if (!e || *e) return;
 p.heading = std::strtof(get(4), &e); if (!e || *e) return;
 p.health = (uint16_t)std::strtoul(get(5), &e, 10); if (!e || *e) return;
 p.armour = (uint16_t)std::strtoul(get(6), &e, 10); if (!e || *e) return;
 w = (unsigned)std::strtoul(get(7), &e, 10); if (!e || *e) return;
 p.modelId = 7; a = kMoveStill;
 } else return;

 if (vehicleId <= 65535 && vehicleSeat <= 7) { p.vehicleId = (uint16_t)vehicleId; p.vehicleSeat = (uint8_t)vehicleSeat; }
 if (!hasName) {
 p.name[0] = '\0';
 std::scoped_lock l(m_mutex);
 auto old = std::find_if(m_remotePlayers.begin(), m_remotePlayers.end(), [&](const RemotePlayer& x){ return x.id == p.id; });
 if (old != m_remotePlayers.end()) std::snprintf(p.name, sizeof(p.name), "%s", old->name);
 }
 if (p.id == 0 || a > kMoveSprint || w > 255 || !isFiniteNumber(p.x) || !isFiniteNumber(p.y) || !isFiniteNumber(p.z) || !isFiniteNumber(p.heading)) return;
 p.heading = normalizeHeadingDegrees(p.heading); p.animationState = (uint8_t)a; p.weapon = (uint8_t)w;
 p.receivedAtMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
 auto weaponIt = m_lastLoggedRemoteWeapons.find(p.id);
 if (weaponIt == m_lastLoggedRemoteWeapons.end() || weaponIt->second != p.weapon) { WeaponFileLog("WEAPON_REMOTE_RECEIVE id=%hu weapon=%u", p.id, p.weapon); m_lastLoggedRemoteWeapons[p.id] = p.weapon; }
	std::scoped_lock l(m_mutex);
	auto it = std::find_if(m_remotePlayers.begin(), m_remotePlayers.end(), [&](const RemotePlayer& x){ return x.id == p.id; });
	if (it == m_remotePlayers.end() || it->modelId != p.modelId || std::strcmp(it->specialModelName, p.specialModelName) != 0)
	WeaponFileLog("REMOTE_MODEL_RECEIVE id=%hu model=%hu special=%s", p.id, p.modelId, p.specialModelName);
	if (it == m_remotePlayers.end()) {
     auto pendingClothes = m_pendingRemoteClothes.find(p.id);
     if (pendingClothes != m_pendingRemoteClothes.end()) {
         p.clothes = pendingClothes->second;
         p.clothesValid = true;
         m_pendingRemoteClothes.erase(pendingClothes);
     }
	 m_remotePlayers.push_back(p);
	} else {



	 if (fields.size() < 16) p.areaCode = it->areaCode;
	 p.animation = it->animation;
	 p.animationAssociations = it->animationAssociations;
	 p.animationAssociationCount = it->animationAssociationCount;
	 p.bones = it->bones;
	 p.boneCount = it->boneCount;
     p.clothes = it->clothes;
     p.clothesValid = it->clothesValid;
	 *it = p;
	}
}

void CoopClient::SetLocalAnimationProfile(const AnimationProfile& profile) {
 AnimationProfile sanitized = profile;
 if (sanitized.locomotion > kMoveSprint) sanitized.locomotion = kMoveStill;
 if (static_cast<unsigned>(sanitized.action) > static_cast<unsigned>(AnimationAction::Swim)) sanitized.action = AnimationAction::Idle;
 sanitized.phase = std::max(0.0f, std::min(1.0f, sanitized.phase));
 sanitized.blend = std::max(0.0f, std::min(1.0f, sanitized.blend));
 std::scoped_lock lock(m_mutex); m_animationProfile = sanitized; m_animationState.store(sanitized.locomotion);
}

void CoopClient::SetLocalAnimationAssociations(const AnimationAssociationState* states, uint8_t count) {
 if (count > 64) count = 64;
 std::scoped_lock lock(m_mutex);
 m_animationAssociationCount = count;
 if (states && count) std::copy(states, states + count, m_animationAssociations.begin());
}

void CoopClient::SetLocalGameTime(uint8_t hour, uint8_t minute, uint8_t day) {
 if (hour > 23 || minute > 59 || day < 1 || day > 31) return;
 m_localGameHour.store(hour); m_localGameMinute.store(minute); m_localGameDay.store(day); m_localGameTimeValid.store(true);
}

void CoopClient::PumpGameTime() {
 const uint16_t localId = m_localId.load();
 if (!m_connected || !localId || m_timeAuthorityId.load() != localId || !m_localGameTimeValid.load()) return;
 const uint8_t hour = m_localGameHour.load(), minute = m_localGameMinute.load(), day = m_localGameDay.load();
 const uint64_t now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
 const bool changed = !m_sentGameTime || hour != m_lastSentGameHour || minute != m_lastSentGameMinute || day != m_lastSentGameDay;
 if (m_forceGameTimeSend || changed || now - m_lastGameTimeSendMs >= 5000) {
 char packet[64]; std::snprintf(packet, sizeof(packet), "TIME|%u|%u|%u\n", hour, minute, day);
 SendLine(packet);
 m_lastSentGameHour = hour; m_lastSentGameMinute = minute; m_lastSentGameDay = day;
 m_lastGameTimeSendMs = now; m_sentGameTime = true; m_forceGameTimeSend = false;
 }
}

}
