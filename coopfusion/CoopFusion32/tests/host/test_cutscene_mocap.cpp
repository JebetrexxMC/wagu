#include "cutscene_mocap.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {
using Bytes = std::vector<uint8_t>;

void U32(Bytes& out, uint32_t value) {
    const auto* p = reinterpret_cast<const uint8_t*>(&value);
    out.insert(out.end(), p, p + sizeof(value));
}
void Raw(Bytes& out, const void* data, size_t size) {
    const auto* p = static_cast<const uint8_t*>(data);
    out.insert(out.end(), p, p + size);
}
Bytes Chunk(const char* name, const Bytes& payload) {
    Bytes out;
    Raw(out, name, 4);
    U32(out, static_cast<uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    while (out.size() & 3u) out.push_back(0);
    return out;
}
void Append(Bytes& dst, const Bytes& src) {
    dst.insert(dst.end(), src.begin(), src.end());
}

struct Frame { float time, x, y, z; };
Bytes MakeBone(const char* name, int32_t tag, const std::vector<Frame>& frames) {
    Bytes metadata(28, 0);
    std::memcpy(metadata.data(), name, std::min<size_t>(27, std::strlen(name)));
    U32(metadata, static_cast<uint32_t>(frames.size()));
    U32(metadata, 0);
    U32(metadata, 0);
    U32(metadata, static_cast<uint32_t>(tag));
    Bytes keys;
    for (const Frame& frame : frames) {
        const float values[8] = {0.0f, 0.0f, 0.0f, 1.0f, frame.x, frame.y, frame.z, frame.time};
        Raw(keys, values, sizeof(values));
    }
    Bytes cpan;
    Append(cpan, Chunk("ANIM", metadata));
    Append(cpan, Chunk("KRT0", keys));
    return Chunk("CPAN", cpan);
}

Bytes MakeIfp(const char* actor, const std::vector<Frame>& rootFrames) {
    Bytes topInfo;
    U32(topInfo, 0);
    U32(topInfo, 0);
    Bytes innerInfo;
    U32(innerInfo, 1);
    U32(innerInfo, 0);
    Bytes dgan;
    Append(dgan, Chunk("INFO", innerInfo));
    Append(dgan, MakeBone("root", 0, rootFrames));
    Append(dgan, MakeBone("upperarm", 21, rootFrames));

    Bytes anpk;
    Append(anpk, Chunk("INFO", topInfo));
    Bytes actorName(actor, actor + std::strlen(actor) + 1u);
    Append(anpk, Chunk("NAME", actorName));
    Append(anpk, Chunk("DGAN", dgan));
    return Chunk("ANPK", anpk);
}

void Put16(Bytes& bytes, size_t at, uint16_t value) {
    std::memcpy(bytes.data() + at, &value, sizeof(value));
}
void Put32(Bytes& bytes, size_t at, uint32_t value) {
    std::memcpy(bytes.data() + at, &value, sizeof(value));
}

std::string MakeSyntheticCutsImg() {
    constexpr size_t sector = 2048;
    const auto standing = MakeIfp("csplay", {
        {110.0f, 0.0f, 0.0f, 0.0f}, {112.0f, 0.0f, 0.0f, 0.0f},
        {114.0f, 0.0f, 0.0f, 0.0f}, {116.0f, 0.0f, 0.0f, 0.0f}, {118.0f, 0.0f, 0.0f, 0.0f}
    });
    const auto walking = MakeIfp("CsPlay", {
        {54.5f, 0.0f, 0.0f, 0.0f}, {55.0f, 0.75f, 0.0f, 0.0f},
        {55.5f, 1.50f, 0.0f, 0.0f}, {56.0f, 2.25f, 0.0f, 0.0f}
    });
    assert(standing.size() <= sector && walking.size() <= sector);

    Bytes img(sector * 3u, 0);
    std::memcpy(img.data(), "VER2", 4);
    Put32(img, 4, 2);
    auto writeEntry = [&](size_t index, uint32_t sectorOffset, const char* filename) {
        const size_t at = 8u + index * 32u;
        Put32(img, at, sectorOffset);
        Put16(img, at + 4u, 1);
        Put16(img, at + 6u, 0);
        std::memcpy(img.data() + at + 8u, filename, std::strlen(filename));
    };
    writeEntry(0, 1, "BHILL1.IFP");
    writeEntry(1, 2, "D10_ALT.IFP");
    std::memcpy(img.data() + sector, standing.data(), standing.size());
    std::memcpy(img.data() + sector * 2u, walking.data(), walking.size());

    const std::string path = "/tmp/coopfusion_test_cuts.img";
    FILE* file = std::fopen(path.c_str(), "wb");
    assert(file);
    assert(std::fwrite(img.data(), 1, img.size(), file) == img.size());
    std::fclose(file);
    return path;
}
}

int main() {
    const std::string archive = MakeSyntheticCutsImg();
    const bool built = coop_cutscene_mocap::BuildFromImageForTesting(archive.c_str());
    if (!built) {
        std::cerr << "synthetic cuts.img build failed: " << coop_cutscene_mocap::GetLastError() << '\n';
        std::remove(archive.c_str());
        return 1;
    }
    size_t size = 0;
    const uint8_t* pack = coop_cutscene_mocap::GetAnimPackData(&size);
    const size_t standingCount = coop_cutscene_mocap::GetStandingClipCount();
    const size_t walkCount = coop_cutscene_mocap::GetWalkClipCount();
    const float walkSpeed = coop_cutscene_mocap::GetWalkSpeed(0);
    const bool packLooksValid = pack && size > 32 && std::memcmp(pack, "ANPK", 4) == 0 &&
        std::search(pack, pack + size, "COOPCS", "COOPCS" + 6) != pack + size &&
        std::search(pack, pack + size, "coopcs00", "coopcs00" + 8) != pack + size &&
        std::search(pack, pack + size, "coopwk00", "coopwk00" + 8) != pack + size;
    std::remove(archive.c_str());
    if (standingCount != 1 || walkCount != 1 || !packLooksValid ||
        !std::isfinite(walkSpeed) || walkSpeed < 1.5f || walkSpeed > 1.9f) {
        std::cerr << "bad generated bank: standing=" << standingCount << " walking=" << walkCount
                  << " speed=" << walkSpeed << " size=" << size << '\n';
        return 1;
    }
    std::cout << "cuts.img VER2 parser + ANPK/COOPCS generation: PASS ("
              << standingCount << " standing, " << walkCount << " gait, "
              << static_cast<int>(std::lround(walkSpeed * 100.0f)) << " cm/s)\n";
    return 0;
}
