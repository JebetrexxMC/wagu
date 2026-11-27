#pragma once

#include <array>
#include <cstdint>

struct NetworkBone {
    uint8_t index = 0;
    float qx = 0.0f;
    float qy = 0.0f;
    float qz = 0.0f;
    float qw = 1.0f;
};

struct PlayerClothesDesc {
    uint32_t modelKeys[10]{};
    uint32_t textureKeys[18]{};
    float fatStat = 0.0f;
    float muscleStat = 0.0f;
};
static_assert(sizeof(PlayerClothesDesc) == 0x78, "PC CoopAndreas clothes descriptor layout must stay 0x78 bytes");



enum class AnimationAction : uint8_t {
    None = 0,
    Idle = 1,
    Locomotion = 2,
    Jump = 3,
    Fall = 4,
    GetUp = 5,
    Attack = 6,
    Aim = 7,
    Death = 8,
    EnterVehicle = 9,
    ExitVehicle = 10,
    Driving = 11,
    Passenger = 12,
    Crouch = 13,
    HandsUp = 14,
    Phone = 15,
    Chat = 16,
    Swim = 17,
};

enum AnimationFlags : uint16_t {
    ANIM_AIMING = 1u << 0,
    ANIM_FIRING = 1u << 1,
    ANIM_DUCKING = 1u << 2,
    ANIM_IN_VEHICLE = 1u << 3,
    ANIM_ON_GROUND = 1u << 4,
    ANIM_LOOPING = 1u << 5,
    ANIM_UPPER_BODY = 1u << 6,
    ANIM_DEAD = 1u << 7,
    ANIM_AIM_FIRST_PERSON = 1u << 8,
};

struct AnimationAssociationState {
    uint16_t group = 0;
    int16_t animationId = 0;
    uint16_t flags = 0;
    float blend = 0.0f;
    float blendDelta = 0.0f;
    float currentTime = 0.0f;
    float speed = 1.0f;
    float timeStep = 0.0f;
    uint32_t hierarchyHash = 0;
};



constexpr int16_t kAnimationFireId = 224;
constexpr int16_t kAnimationCrouchFireId = 225;

struct AnimationProfile {
    uint32_t sequence = 0;
    uint8_t locomotion = 1;
    AnimationAction action = AnimationAction::Idle;
    uint16_t flags = ANIM_ON_GROUND | ANIM_LOOPING;
    uint8_t weaponState = 0;
    uint8_t reserved = 0;
    uint16_t animGroup = 0;
    float phase = 0.0f;
    float blend = 1.0f;
    float aimYaw = 0.0f;
    float aimPitch = 0.0f;
};

constexpr uint32_t kModelInfoCapacity = 20000;

struct VehicleAppearanceState {
    int8_t paintjob = -1;
    std::array<int16_t, 15> upgrades = [] {
        std::array<int16_t, 15> value{};
        value.fill(-1);
        return value;
    }();
    bool valid = false;
};

struct VehicleExtendedState {
    uint8_t tertiaryColor = 0;
    uint8_t quaternaryColor = 0;
    int8_t extraA = -1;
    int8_t extraB = -1;
    uint8_t handbrake = 0;
    bool landingGearValid = false;
    float landingGear = 0.0f;
    bool searchLightValid = false;
    float searchLightX = 0.0f;
    float searchLightY = 0.0f;
    float searchLightZ = 0.0f;
    float searchLightIntensity = 0.0f;
    uint8_t searchLightEnabled = 0;
    bool bikeLeanValid = false;
    float bikeLean = 0.0f;
    float bikeLeanForward = 0.0f;
    bool bmxValid = false;
    float bmxSprintLean = 0.0f;
    float bmxPedallingCharge = 0.0f;
    bool valid = false;
};

struct RemoteNpc {
    uint32_t id = 0;
    uint16_t ownerId = 0;
    uint32_t localRef = 0;
    uint16_t modelId = 7;
    char specialModelName[16]{};
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float heading = 0.0f;
    uint16_t health = 100;
    uint16_t armour = 0;
    uint8_t weapon = 0;
    uint8_t moveState = 1;
    uint16_t vehicleId = 0;
    uint8_t vehicleSeat = 0;
    float vx = 0.0f, vy = 0.0f, vz = 0.0f;
    float currentRotation = 0.0f;
    float aimingRotation = 0.0f;
    float lookingDirection = 0.0f;
    uint8_t fightingStyle = 4;
    bool aiming = false;
    bool firing = false;
    float aimX = 0.0f, aimY = 0.0f, aimZ = 0.0f;
    std::array<AnimationAssociationState, 64> animationAssociations{};
    uint8_t animationAssociationCount = 0;
    uint64_t receivedAtMs = 0;
};

struct RemoteNpcDamage {
    uint32_t id = 0;
    uint32_t localRef = 0;
    uint16_t health = 100;
    uint16_t armour = 0;
    uint8_t weapon = 0;
};

struct RemoteNpcVitals {
    uint32_t id = 0;
    uint16_t ownerId = 0;
    uint32_t localRef = 0;
    uint16_t health = 100;
    uint16_t armour = 0;
};

struct RemoteMissionMarker {
    uint32_t id = 0;
    uint16_t ownerId = 0;
    uint16_t sourceSlot = 0;
    uint8_t type = 0;
    uint8_t display = 0;
    uint8_t sprite = 0;
    uint16_t scale = 1;
    uint32_t color = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float radius = 1.0f;
};

struct RemoteGameTime {
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t day = 1;
};
