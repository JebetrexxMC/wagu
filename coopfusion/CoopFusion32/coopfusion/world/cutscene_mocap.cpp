#include "cutscene_mocap.h"
#include "../diagnostics/cutscene_anim_log.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <unistd.h>

namespace coop_cutscene_mocap {
namespace {

struct ClipDef {
    const char* cutscene;
    const char* actor;
    float from;
    float to;
};

static const ClipDef kClips[] = {
    {"BCRAS2", "csplay", 17.5f, 22.5f},   {"BCRAS2", "csplay", 25.5f, 30.5f},  {"BHILL1", "csplay", 111.5f, 116.5f},
    {"CAS6B_1", "csplay", 8.5f, 13.5f},   {"CAS_11A", "csplay", 28.5f, 33.5f}, {"CAS_4C", "csplay", 47.5f, 52.5f},
    {"CRASH3A", "csplay", 19.5f, 24.5f},  {"CRASH3A", "csplay", 52.5f, 57.5f}, {"CRASV2A", "CsPlay", 48.5f, 53.5f},
    {"DESERT4", "Csplay", 5.5f, 10.5f},   {"FARL_2A", "CsPlay", 34.5f, 39.5f}, {"FARL_3A", "CsPlay", 42.5f, 47.5f},
    {"GARAG1B", "csplay", 93.5f, 98.5f},  {"GROVE1A", "csplay", 38.5f, 43.5f}, {"GROVE1C", "csplay", 7.5f, 12.5f},
    {"GROVE1C", "csplay", 44.5f, 49.5f},  {"GROVE2", "csplay", 57.5f, 62.5f},  {"RIOT_2", "csplay", 35.5f, 40.5f},
    {"RIOT_2", "csplay", 56.5f, 61.5f},   {"RYDER3A", "csplay", 47.5f, 52.5f}, {"SCRASH2", "csplay", 65.5f, 70.5f},
    {"STRAP2A", "CSplay", 52.5f, 57.5f},  {"STRAP3A", "csplay", 68.5f, 73.5f}, {"STRAP3A", "csplay", 73.5f, 78.5f},
    {"SYND_7", "CsPlay", 26.5f, 31.5f},   {"TRUTH_1", "Csplay", 31.5f, 36.5f}, {"TRUTH_2", "csplay", 8.5f, 13.5f},
    {"ZERO_4", "CsPlay", 9.5f, 14.5f},
};

static const ClipDef kWalks[] = {
    {"D10_ALT", "CsPlay", 54.6f, 55.95f}, {"RIOT4E2", "csplay", 79.1f, 80.47f}, {"CAS_4B", "csplay", 84.0f, 85.31f},
    {"FARL_2A", "CsPlay", 78.7f, 80.09f}, {"DESERT4", "Csplay", 1.0f, 2.29f},   {"CAS_2", "csplay", 17.1f, 18.39f},
    {"DESERT6", "Csplay", 69.5f, 70.83f}, {"HEIST5A", "csplay", 34.3f, 35.49f},
};

constexpr size_t kClipCapacity = sizeof(kClips) / sizeof(kClips[0]);
constexpr size_t kWalkCapacity = sizeof(kWalks) / sizeof(kWalks[0]);
constexpr float kRootHeight = 0.023f;
constexpr float kWalkHeight = -0.01f;
constexpr uint32_t kSectorSize = 2048;
constexpr uint32_t kMaxDirectoryEntries = 100000;
constexpr size_t kMaxIfpBytes = 64u * 1024u * 1024u;

enum class BuildState : uint8_t { Idle, Building, Ready, Loaded, Failed };
std::atomic<BuildState> g_state{BuildState::Idle};
std::vector<uint8_t> g_anpk;
char g_clipNames[kClipCapacity][12]{};
char g_walkNames[kWalkCapacity][12]{};
float g_walkSpeeds[kWalkCapacity]{};
size_t g_clipCount = 0;
size_t g_walkCount = 0;
char g_lastError[256]{};
char g_sourcePath[512]{};
std::vector<std::string> g_gameSearchRoots;

bool BodyBone(int tag) {
    return (tag >= 0 && tag <= 5) || (tag >= 21 && tag <= 26) || (tag >= 31 && tag <= 36) ||
           (tag >= 41 && tag <= 44) || (tag >= 51 && tag <= 54) || tag == 201 || tag == 301 || tag == 302;
}

struct Key {
    float q[4]{};
    float t[3]{};
    float time = 0.0f;
};
struct Bone {
    char name[28]{};
    int32_t tag = -1;
    bool trans = false;
    std::vector<Key> keys;
};

uint32_t ReadU32(const uint8_t* p) {
    uint32_t value = 0;
    std::memcpy(&value, p, sizeof(value));
    return value;
}
uint32_t U32(const std::vector<uint8_t>& bytes, size_t at) {
    return ReadU32(bytes.data() + at);
}
bool AddFits(size_t a, size_t b, size_t limit) {
    return a <= limit && b <= limit - a;
}
bool PadSize(size_t value, size_t& result) {
    if (value > static_cast<size_t>(-1) - 3) return false;
    result = (value + 3u) & ~size_t(3u);
    return true;
}
bool ChunkNext(size_t chunkAt, uint32_t chunkSize, size_t limit, size_t& next) {
    size_t padded = 0;
    if (!PadSize(chunkSize, padded) || !AddFits(chunkAt, 8u, limit) || !AddFits(chunkAt + 8u, padded, limit))
        return false;
    next = chunkAt + 8u + padded;
    return true;
}
bool EqualsNoCase(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b))) return false;
        ++a; ++b;
    }
    return *a == *b;
}
std::string Lower(std::string value) {
    for (char& ch : value) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return value;
}

bool ParseBones(const std::vector<uint8_t>& bytes, size_t pos, size_t end, std::vector<Bone>& out) {
    if (!AddFits(pos, 8, end) || std::memcmp(bytes.data() + pos, "INFO", 4) != 0) return false;
    size_t infoNext = 0;
    if (!ChunkNext(pos, U32(bytes, pos + 4), end, infoNext)) return false;
    pos = infoNext;

    while (AddFits(pos, 8, end)) {
        const uint32_t chunkSize = U32(bytes, pos + 4);
        size_t next = 0;
        if (!ChunkNext(pos, chunkSize, end, next)) return false;
        const size_t data = pos + 8;
        if (std::memcmp(bytes.data() + pos, "CPAN", 4) != 0 || !AddFits(data, 8u + 44u, next) ||
            std::memcmp(bytes.data() + data, "ANIM", 4) != 0) {
            pos = next;
            continue;
        }

        const uint32_t animSize = U32(bytes, data + 4);
        if (!AddFits(data + 8, animSize, next) || animSize < 36) return false;
        const size_t metadata = data + 8;
        const uint32_t frames = U32(bytes, metadata + 28);
        Bone bone;
        std::memcpy(bone.name, bytes.data() + metadata, 28);
        bone.name[27] = '\0';
        bone.tag = animSize == 44 ? static_cast<int32_t>(U32(bytes, metadata + 40)) : -1;

        size_t animPadded = 0;
        if (!PadSize(animSize, animPadded) || !AddFits(metadata, animPadded, next)) return false;
        const size_t keyChunk = metadata + animPadded;
        if (frames && AddFits(keyChunk, 8, next)) {
            const bool hasTranslation = bytes[keyChunk + 2] == 'T';
            const bool hasScale = bytes[keyChunk + 3] == 'S';
            const size_t stride = sizeof(float) * (5u + (hasTranslation ? 3u : 0u) + (hasScale ? 3u : 0u));
            const uint32_t keyChunkSize = U32(bytes, keyChunk + 4);
            if (frames > keyChunkSize / stride ||
                !AddFits(keyChunk + 8, static_cast<size_t>(frames) * stride, next)) return false;
            bone.trans = hasTranslation;
            bone.keys.resize(frames);
            for (uint32_t i = 0; i < frames; ++i) {
                const uint8_t* frame = bytes.data() + keyChunk + 8 + static_cast<size_t>(i) * stride;
                Key& key = bone.keys[i];
                std::memcpy(key.q, frame, sizeof(key.q));
                if (hasTranslation) std::memcpy(key.t, frame + sizeof(float) * 4u, sizeof(key.t));
                else key.t[0] = key.t[1] = key.t[2] = 0.0f;
                const size_t timeIndex = 4u + (hasTranslation ? 3u : 0u) + (hasScale ? 3u : 0u);
                std::memcpy(&key.time, frame + sizeof(float) * timeIndex, sizeof(float));
                if (!std::isfinite(key.time)) return false;
                for (float component : key.q) if (!std::isfinite(component)) return false;
                for (float component : key.t) if (!std::isfinite(component)) return false;
            }
        }
        if (BodyBone(bone.tag) && !bone.keys.empty()) out.push_back(std::move(bone));
        pos = next;
    }
    return !out.empty();
}

bool ParseActor(const std::vector<uint8_t>& bytes, const char* actor, std::vector<Bone>& out) {
    if (bytes.size() < 16 || std::memcmp(bytes.data(), "ANPK", 4) != 0) return false;
    const uint64_t declaredEnd64 = static_cast<uint64_t>(U32(bytes, 4)) + 8u;
    if (declaredEnd64 < 16u) return false;
    const size_t end = static_cast<size_t>(std::min<uint64_t>(bytes.size(), declaredEnd64));
    size_t pos = 8;
    if (!AddFits(pos, 8, end) || std::memcmp(bytes.data() + pos, "INFO", 4) != 0) return false;
    if (!ChunkNext(pos, U32(bytes, pos + 4), end, pos)) return false;

    bool actorMatches = false;
    while (AddFits(pos, 8, end)) {
        const uint32_t chunkSize = U32(bytes, pos + 4);
        const size_t data = pos + 8;
        size_t next = 0;
        if (!ChunkNext(pos, chunkSize, end, next)) return false;
        if (std::memcmp(bytes.data() + pos, "NAME", 4) == 0) {
            const size_t nameLength = std::min<size_t>(chunkSize, 255u);
            std::string name(reinterpret_cast<const char*>(bytes.data() + data),
                             strnlen(reinterpret_cast<const char*>(bytes.data() + data), nameLength));
            actorMatches = EqualsNoCase(name.c_str(), actor);
        } else if (std::memcmp(bytes.data() + pos, "DGAN", 4) == 0 && actorMatches) {
            return ParseBones(bytes, data, data + chunkSize, out);
        }
        pos = next;
    }
    return false;
}

float Dot(const float a[4], const float b[4]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
}
Key Sample(const std::vector<Key>& keys, float time) {
    Key result = time <= keys.front().time ? keys.front() : keys.back();
    for (size_t i = 1; i < keys.size() && time > keys.front().time; ++i) {
        if (keys[i].time >= time) {
            const Key& a = keys[i - 1];
            const Key& b = keys[i];
            const float u = b.time > a.time ? (time - a.time) / (b.time - a.time) : 0.0f;
            const float sign = Dot(a.q, b.q) < 0.0f ? -1.0f : 1.0f;
            float norm = 0.0f;
            for (int j = 0; j < 4; ++j) {
                result.q[j] = a.q[j] + (b.q[j] * sign - a.q[j]) * u;
                norm += result.q[j] * result.q[j];
            }
            if (norm > 1.0e-12f) {
                const float invNorm = 1.0f / std::sqrt(norm);
                for (float& component : result.q) component *= invNorm;
            } else {
                std::memcpy(result.q, a.q, sizeof(result.q));
            }
            for (int j = 0; j < 3; ++j) result.t[j] = a.t[j] + (b.t[j] - a.t[j]) * u;
            break;
        }
    }
    result.time = time;
    return result;
}

std::vector<Key> Cut(const std::vector<Key>& keys, float from, float to) {
    std::vector<Key> result;
    result.push_back(Sample(keys, from));
    for (const Key& key : keys)
        if (key.time > from && key.time < to && key.time > result.back().time) result.push_back(key);
    result.push_back(Sample(keys, to));
    for (Key& key : result) key.time -= from;
    for (size_t i = 1; i < result.size(); ++i) {
        if (Dot(result[i - 1].q, result[i].q) < 0.0f)
            for (float& component : result[i].q) component = -component;
    }
    return result;
}

void Mul(const float a[4], const float b[4], float out[4]) {
    out[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    out[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    out[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    out[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}
float Yaw(const float q[4]) {
    return std::atan2(2.0f * (q[0] * q[1] + q[3] * q[2]),
                      1.0f - 2.0f * (q[1] * q[1] + q[2] * q[2]));
}
void Normalize(std::vector<Key>& root, float height) {
    if (root.empty()) return;
    float sine = 0.0f, cosine = 0.0f, z = 0.0f;
    for (const Key& key : root) {
        const float q[4] = {-key.q[0], -key.q[1], -key.q[2], key.q[3]};
        const float yaw = Yaw(q);
        sine += std::sin(yaw);
        cosine += std::cos(yaw);
        z += key.t[2];
    }
    const float delta = 1.5707963f - std::atan2(sine, cosine);
    const float lift = height - z / static_cast<float>(root.size());
    const float turn[4] = {0.0f, 0.0f, std::sin(delta / 2.0f), std::cos(delta / 2.0f)};
    for (Key& key : root) {
        const float q[4] = {-key.q[0], -key.q[1], -key.q[2], key.q[3]};
        float rotated[4];
        Mul(turn, q, rotated);
        key.q[0] = -rotated[0];
        key.q[1] = -rotated[1];
        key.q[2] = -rotated[2];
        key.q[3] = rotated[3];
        key.t[0] = key.t[1] = 0.0f;
        key.t[2] += lift;
    }
}

struct Out {
    std::vector<uint8_t> bytes;
    void Raw(const void* data, size_t size) {
        const uint8_t* begin = static_cast<const uint8_t*>(data);
        bytes.insert(bytes.end(), begin, begin + size);
    }
    void U32(uint32_t value) { Raw(&value, sizeof(value)); }
    size_t Open(const char* tag) {
        const size_t at = bytes.size();
        Raw(tag, 4);
        U32(0);
        return at;
    }
    void Close(size_t at) {
        const uint32_t size = static_cast<uint32_t>(bytes.size() - at - 8u);
        std::memcpy(bytes.data() + at + 4u, &size, sizeof(size));
        while (bytes.size() & 3u) bytes.push_back(0);
    }
};

void WriteClip(Out& out, const char* name, const std::vector<Bone>& bones) {
    size_t nameChunk = out.Open("NAME");
    out.Raw(name, std::strlen(name) + 1u);
    out.Close(nameChunk);

    size_t dgan = out.Open("DGAN");
    size_t info = out.Open("INFO");
    out.U32(static_cast<uint32_t>(bones.size()));
    out.U32(0);
    out.Close(info);

    for (const Bone& bone : bones) {
        size_t cpan = out.Open("CPAN");
        size_t anim = out.Open("ANIM");
        out.Raw(bone.name, sizeof(bone.name));
        out.U32(static_cast<uint32_t>(bone.keys.size()));
        out.U32(0);
        out.U32(0);
        out.U32(static_cast<uint32_t>(bone.tag));
        out.Close(anim);
        size_t keyframes = out.Open(bone.trans ? "KRT0" : "KR00");
        for (const Key& key : bone.keys) {
            out.Raw(key.q, sizeof(key.q));
            if (bone.trans) out.Raw(key.t, sizeof(key.t));
            out.Raw(&key.time, sizeof(key.time));
        }
        out.Close(keyframes);
        out.Close(cpan);
    }
    out.Close(dgan);
}

void SetError(const char* message) {
    std::snprintf(g_lastError, sizeof(g_lastError), "%s", message ? message : "unknown error");
    CutsceneAnimLog("MOCAP_ERROR %s", g_lastError);
}

bool Exists(const std::string& path) {
    return access(path.c_str(), R_OK) == 0;
}
void AddCandidate(std::vector<std::string>& paths, const std::string& path) {
    if (path.empty()) return;
    if (std::find(paths.begin(), paths.end(), path) == paths.end()) paths.push_back(path);
}
std::string ReadProcessPackage() {
    FILE* file = std::fopen("/proc/self/cmdline", "rb");
    if (!file) return {};
    char buffer[256]{};
    const size_t count = std::fread(buffer, 1, sizeof(buffer) - 1u, file);
    std::fclose(file);
    size_t length = 0;
    while (length < count && buffer[length] && buffer[length] != ':') ++length;
    buffer[length] = '\0';
    return buffer;
}

bool FindCutsImg(std::string& selected) {
    std::vector<std::string> paths;
    char cwd[512]{};
    if (getcwd(cwd, sizeof(cwd))) {
        const std::string root(cwd);
        AddCandidate(paths, root + "/anim/cuts.img");
        AddCandidate(paths, root + "/data/anim/cuts.img");
        AddCandidate(paths, root + "/files/anim/cuts.img");
        AddCandidate(paths, root + "/files/data/anim/cuts.img");
        AddCandidate(paths, root + "/../files/anim/cuts.img");
        AddCandidate(paths, root + "/../files/data/anim/cuts.img");
    }

    for (const std::string& rootValue : g_gameSearchRoots) {
        if (rootValue.empty()) continue;
        std::string root = rootValue;
        while (root.size() > 1 && root.back() == '/') root.pop_back();
        AddCandidate(paths, root + "/anim/cuts.img");
        AddCandidate(paths, root + "/data/anim/cuts.img");
        AddCandidate(paths, root + "/files/anim/cuts.img");
        AddCandidate(paths, root + "/files/data/anim/cuts.img");
    }

    const std::string package = ReadProcessPackage();
    const char* external = std::getenv("EXTERNAL_STORAGE");
    const std::string externalRoot = external && external[0] ? external : "/storage/emulated/0";
    if (!package.empty()) {
        const std::string publicAppRoot = externalRoot + "/Android/data/" + package;
        const std::string sdcardAppRoot = "/sdcard/Android/data/" + package;
        const std::string privateAppRoot = "/data/user/0/" + package;
        const std::string legacyPrivateAppRoot = "/data/data/" + package;
        AddCandidate(paths, publicAppRoot + "/files/data/anim/cuts.img");
        AddCandidate(paths, publicAppRoot + "/files/anim/cuts.img");
        AddCandidate(paths, sdcardAppRoot + "/files/data/anim/cuts.img");
        AddCandidate(paths, sdcardAppRoot + "/files/anim/cuts.img");
        AddCandidate(paths, privateAppRoot + "/files/data/anim/cuts.img");
        AddCandidate(paths, privateAppRoot + "/files/anim/cuts.img");
        AddCandidate(paths, legacyPrivateAppRoot + "/files/data/anim/cuts.img");
        AddCandidate(paths, legacyPrivateAppRoot + "/files/anim/cuts.img");
    }
    AddCandidate(paths, "/storage/emulated/0/Android/data/com.rockstargames.gtasa/files/data/anim/cuts.img");
    AddCandidate(paths, "/storage/emulated/0/Android/data/com.rockstargames.gtasa/files/anim/cuts.img");
    AddCandidate(paths, "/sdcard/Android/data/com.rockstargames.gtasa/files/data/anim/cuts.img");
    AddCandidate(paths, "/sdcard/Android/data/com.rockstargames.gtasa/files/anim/cuts.img");

    std::string checked;
    for (const std::string& path : paths) {
        if (Exists(path)) {
            selected = path;
            std::snprintf(g_sourcePath, sizeof(g_sourcePath), "%s", path.c_str());
            CutsceneAnimLog("CUTS_IMG_FOUND path=%s", path.c_str());
            return true;
        }
        if (checked.size() < sizeof(g_lastError) - 64u) {
            if (!checked.empty()) checked += "; ";
            checked += path;
        }
    }
    std::snprintf(g_lastError, sizeof(g_lastError), "anim/cuts.img not found; checked: %.180s", checked.c_str());
    CutsceneAnimLog("CUTS_IMG_NOT_FOUND checked=%s", checked.c_str());
    return false;
}

bool ReadImgDirectory(FILE* file, size_t fileSize,
                      std::map<std::string, std::pair<uint32_t, uint32_t>>& directory) {
    uint8_t header[8]{};
    if (fseeko(file, 0, SEEK_SET) != 0 || std::fread(header, 1, sizeof(header), file) != sizeof(header)) {
        SetError("cuts.img header could not be read");
        return false;
    }
    if (std::memcmp(header, "VER2", 4) != 0) {
        SetError("cuts.img is not a VER2 IMG archive");
        return false;
    }
    const uint32_t count = ReadU32(header + 4);
    if (count == 0 || count > kMaxDirectoryEntries) {
        SetError("cuts.img has an invalid directory entry count");
        return false;
    }
    const size_t entriesSize = static_cast<size_t>(count) * 32u;
    if (entriesSize > fileSize || !AddFits(8u, entriesSize, fileSize)) {
        SetError("cuts.img directory is truncated");
        return false;
    }
    std::vector<uint8_t> entries(entriesSize);
    if (std::fread(entries.data(), 1, entries.size(), file) != entries.size()) {
        SetError("cuts.img directory read was incomplete");
        return false;
    }
    for (size_t at = 0; at + 32u <= entries.size(); at += 32u) {
        const uint32_t offsetSector = ReadU32(entries.data() + at);
        uint16_t sizeA = 0, sizeB = 0;
        std::memcpy(&sizeA, entries.data() + at + 4u, sizeof(sizeA));
        std::memcpy(&sizeB, entries.data() + at + 6u, sizeof(sizeB));
        const uint32_t sizeSectors = sizeA ? sizeA : sizeB;
        char rawName[25]{};
        std::memcpy(rawName, entries.data() + at + 8u, 24u);
        rawName[24] = '\0';
        const size_t nameLength = strnlen(rawName, sizeof(rawName));
        if (!nameLength || !sizeSectors) continue;
        const uint64_t offsetBytes = static_cast<uint64_t>(offsetSector) * kSectorSize;
        const uint64_t sizeBytes = static_cast<uint64_t>(sizeSectors) * kSectorSize;
        if (offsetBytes > fileSize || sizeBytes > fileSize - offsetBytes) continue;
        directory[Lower(std::string(rawName, nameLength))] = {offsetSector, sizeSectors};
    }
    CutsceneAnimLog("CUTS_IMG_DIRECTORY entries=%u usable=%u", count,
                    static_cast<unsigned>(directory.size()));
    if (directory.empty()) SetError("cuts.img IMG directory contains no usable entries");
    return !directory.empty();
}

bool LoadIfp(FILE* file, size_t fileSize, const std::pair<uint32_t, uint32_t>& entry,
             std::vector<uint8_t>& output) {
    const uint64_t offsetBytes = static_cast<uint64_t>(entry.first) * kSectorSize;
    const uint64_t sizeBytes = static_cast<uint64_t>(entry.second) * kSectorSize;
    if (!entry.second || sizeBytes > kMaxIfpBytes || offsetBytes > fileSize || sizeBytes > fileSize - offsetBytes)
        return false;
    output.resize(static_cast<size_t>(sizeBytes));
    if (fseeko(file, static_cast<off_t>(offsetBytes), SEEK_SET) != 0) return false;
    return std::fread(output.data(), 1, output.size(), file) == output.size();
}

bool CutActorFromImg(FILE* file, size_t fileSize,
                     const std::map<std::string, std::pair<uint32_t, uint32_t>>& directory,
                     const ClipDef& clip, std::string& cachedName, std::vector<uint8_t>& ifp,
                     std::vector<Bone>& bones) {
    const std::string filename = Lower(std::string(clip.cutscene) + ".ifp");
    const auto found = directory.find(filename);
    if (found == directory.end()) return false;
    if (cachedName != filename) {
        cachedName.clear();
        if (!LoadIfp(file, fileSize, found->second, ifp)) return false;
        cachedName = filename;
    }
    if (!ParseActor(ifp, clip.actor, bones)) return false;
    for (Bone& bone : bones) {
        if (bone.keys.empty() || clip.to <= clip.from || clip.from < 0.0f) return false;
        bone.keys = Cut(bone.keys, clip.from, clip.to);
        for (size_t i = 0; i < bone.keys.size(); ++i) {
            if (!std::isfinite(bone.keys[i].time) ||
                bone.keys[i].time < (i ? bone.keys[i - 1].time : 0.0f)) return false;
        }
    }
    return true;
}

bool BuildFromImg(const std::string& path) {
    std::snprintf(g_sourcePath, sizeof(g_sourcePath), "%s", path.c_str());
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) {
        std::snprintf(g_lastError, sizeof(g_lastError), "cannot open %s (errno=%d)", path.c_str(), errno);
        return false;
    }
    if (fseeko(file, 0, SEEK_END) != 0) {
        std::fclose(file); SetError("cuts.img size could not be queried"); return false;
    }
    const off_t end = ftello(file);
    if (end <= 8) { std::fclose(file); SetError("cuts.img is empty or truncated"); return false; }
    const size_t fileSize = static_cast<size_t>(end);
    CutsceneAnimLog("MOCAP_IMG_OPEN path=%s bytes=%u", path.c_str(),
                    static_cast<unsigned>(std::min<size_t>(fileSize, UINT32_MAX)));

    std::map<std::string, std::pair<uint32_t, uint32_t>> directory;
    if (!ReadImgDirectory(file, fileSize, directory)) { std::fclose(file); return false; }

    Out out;
    const size_t anpk = out.Open("ANPK");
    const size_t info = out.Open("INFO");
    out.U32(0);
    out.Raw("COOPCS\0\0", 8);
    out.Close(info);

    std::string cachedName;
    std::vector<uint8_t> ifp;
    for (const ClipDef& clip : kClips) {
        std::vector<Bone> bones;
        if (!CutActorFromImg(file, fileSize, directory, clip, cachedName, ifp, bones)) continue;
        for (Bone& bone : bones)
            if (bone.tag == 0) { Normalize(bone.keys, kRootHeight); bone.trans = true; }
        if (g_clipCount >= kClipCapacity) continue;
        std::snprintf(g_clipNames[g_clipCount], sizeof(g_clipNames[g_clipCount]), "coopcs%02u",
                      static_cast<unsigned>(g_clipCount));
        WriteClip(out, g_clipNames[g_clipCount], bones);
        CutsceneAnimLog("MOCAP_CLIP_EXTRACTED source=%s actor=%s clip=%s bones=%u",
                        clip.cutscene, clip.actor, g_clipNames[g_clipCount],
                        static_cast<unsigned>(bones.size()));
        ++g_clipCount;
    }

    for (const ClipDef& clip : kWalks) {
        std::vector<Bone> bones;
        if (!CutActorFromImg(file, fileSize, directory, clip, cachedName, ifp, bones)) continue;
        float speed = 0.0f;
        bool haveRoot = false;
        for (Bone& bone : bones) {
            if (bone.tag == 0) {
                const Key& first = bone.keys.front();
                const Key& last = bone.keys.back();
                speed = std::hypot(last.t[0] - first.t[0], last.t[1] - first.t[1]) / (clip.to - clip.from);
                Normalize(bone.keys, kWalkHeight);
                bone.trans = true;
                haveRoot = true;
            }
            Key& last = bone.keys.back();
            const float lastTime = last.time;
            last = bone.keys.front();
            last.time = lastTime;
            if (bone.keys.size() > 1 && Dot(bone.keys[bone.keys.size() - 2].q, last.q) < 0.0f)
                for (float& component : last.q) component = -component;
        }
        if (!haveRoot || speed < 0.5f || g_walkCount >= kWalkCapacity) continue;
        g_walkSpeeds[g_walkCount] = speed;
        std::snprintf(g_walkNames[g_walkCount], sizeof(g_walkNames[g_walkCount]), "coopwk%02u",
                      static_cast<unsigned>(g_walkCount));
        WriteClip(out, g_walkNames[g_walkCount], bones);
        CutsceneAnimLog("MOCAP_WALK_EXTRACTED source=%s actor=%s clip=%s speed=%.3f bones=%u",
                        clip.cutscene, clip.actor, g_walkNames[g_walkCount], speed,
                        static_cast<unsigned>(bones.size()));
        ++g_walkCount;
    }
    std::fclose(file);

    if (g_clipCount == 0) {
        SetError("no usable csplay standing clips could be extracted from cuts.img");
        return false;
    }
    if (out.bytes.size() > UINT32_MAX) {
        SetError("generated COOPCS ANPK exceeds the RenderWare stream size limit");
        return false;
    }
    const uint32_t animationCount = static_cast<uint32_t>(g_clipCount + g_walkCount);
    std::memcpy(out.bytes.data() + info + 8u, &animationCount, sizeof(animationCount));
    out.Close(anpk);
    g_anpk.swap(out.bytes);
    std::snprintf(g_sourcePath, sizeof(g_sourcePath), "%s", path.c_str());
    CutsceneAnimLog("MOCAP_PACK_READY path=%s standing=%u walking=%u bytes=%u",
                    path.c_str(), static_cast<unsigned>(g_clipCount),
                    static_cast<unsigned>(g_walkCount), static_cast<unsigned>(g_anpk.size()));
    return true;
}

void BuildWorker() {
    CutsceneAnimLog("MOCAP_BUILD_WORKER_STARTED");
    std::string path;
    if (!FindCutsImg(path)) {
        g_state.store(BuildState::Failed, std::memory_order_release);
        CutsceneAnimLog("MOCAP_BUILD_FAILED stage=find-image error=%s", g_lastError);
        return;
    }
    if (!BuildFromImg(path)) {
        g_state.store(BuildState::Failed, std::memory_order_release);
        CutsceneAnimLog("MOCAP_BUILD_FAILED stage=parse-build error=%s", g_lastError);
        return;
    }
    g_state.store(BuildState::Ready, std::memory_order_release);
    CutsceneAnimLog("MOCAP_BUILD_FINISHED state=ready");
}

}

void SetGameSearchRoots(const char* dataPath, const char* androidDataPath,
                        const char* internalPath, const char* androidDataRootPath) {
    g_gameSearchRoots.clear();
    const char* roots[] = {dataPath, androidDataPath, internalPath, androidDataRootPath};
    for (const char* root : roots) {
        if (!root || !root[0]) continue;
        const std::string value(root);
        if (std::find(g_gameSearchRoots.begin(), g_gameSearchRoots.end(), value) == g_gameSearchRoots.end())
            g_gameSearchRoots.push_back(value);
    }
}

void StartBuildAsync() {
    BuildState expected = BuildState::Idle;
    if (!g_state.compare_exchange_strong(expected, BuildState::Building, std::memory_order_acq_rel)) return;
    CutsceneAnimLog("MOCAP_BUILD_QUEUED");
    try {
        std::thread(BuildWorker).detach();
    } catch (...) {
        SetError("could not start cuts.img worker thread");
        g_state.store(BuildState::Failed, std::memory_order_release);
    }
}

State GetState() {
    switch (g_state.load(std::memory_order_acquire)) {
        case BuildState::Building: return State::Building;
        case BuildState::Ready: return State::Ready;
        case BuildState::Loaded: return State::Loaded;
        case BuildState::Failed: return State::Failed;
        default: return State::Idle;
    }
}
const char* GetLastError() { return g_lastError; }
const char* GetSourcePath() { return g_sourcePath; }
const uint8_t* GetAnimPackData(size_t* sizeOut) {
    if (sizeOut) *sizeOut = g_anpk.size();
    const BuildState state = g_state.load(std::memory_order_acquire);
    return (state == BuildState::Ready || state == BuildState::Loaded) && !g_anpk.empty() ? g_anpk.data() : nullptr;
}
size_t GetStandingClipCount() {
    const BuildState state = g_state.load(std::memory_order_acquire);
    return (state == BuildState::Ready || state == BuildState::Loaded) ? g_clipCount : 0;
}
const char* GetStandingClip(size_t index) {
    const size_t count = GetStandingClipCount();
    return count ? g_clipNames[index % count] : nullptr;
}
size_t GetWalkClipCount() {
    const BuildState state = g_state.load(std::memory_order_acquire);
    return (state == BuildState::Ready || state == BuildState::Loaded) ? g_walkCount : 0;
}
const char* GetWalkClip(size_t index) {
    const size_t count = GetWalkClipCount();
    return count ? g_walkNames[index % count] : nullptr;
}
float GetWalkSpeed(size_t index) {
    const size_t count = GetWalkClipCount();
    return count ? g_walkSpeeds[index % count] : 0.0f;
}
void MarkLoaded() {
    g_state.store(BuildState::Loaded, std::memory_order_release);
    CutsceneAnimLog("MOCAP_ANIM_BANK_REGISTERED block=COOPCS");
}
void MarkLoadFailed(const char* reason) {
    SetError(reason ? reason : "CAnimManager rejected the generated COOPCS block");
    g_state.store(BuildState::Failed, std::memory_order_release);
    CutsceneAnimLog("MOCAP_ANIM_BANK_LOAD_FAILED reason=%s", reason ? reason : "unknown");
}
void MarkBlockMissing() {
    BuildState expected = BuildState::Loaded;
    g_state.compare_exchange_strong(expected, BuildState::Ready, std::memory_order_acq_rel);
}

#ifdef COOP_CUTSCENE_MOCAP_TESTING
bool BuildFromImageForTesting(const char* path) {
    if (!path || !path[0]) return false;
    g_clipCount = 0;
    g_walkCount = 0;
    g_anpk.clear();
    g_lastError[0] = '\0';
    g_sourcePath[0] = '\0';
    const bool ok = BuildFromImg(path);
    g_state.store(ok ? BuildState::Ready : BuildState::Failed, std::memory_order_release);
    return ok;
}
#endif

}
