// 5.00.01 b: g++ -std=c++17 -Wall -O1 -o /tmp/vc_save_test test/vc_save_test.cpp && /tmp/vc_save_test
#include "../src/vc/vc_save.h"
#include <cstdio>
#include <vector>
static int fails = 0;
static void ok(bool c, const char* what) { printf("%s %s\n", c ? "ok  " : "FAIL", what); if(!c) ++fails; }
static void put32(std::vector<uint8_t>& v, uint32_t x) { for(int i = 0; i < 4; ++i) v.push_back((uint8_t)(x >> (8 * i))); }
static std::vector<uint8_t> save(const std::vector<uint32_t>& sizes, bool fixSum = true)
{
    std::vector<uint8_t> v;
    for(uint32_t s : sizes) { put32(v, s); for(uint32_t k = 0; k < s; ++k) v.push_back((uint8_t)(k * 7 + s)); }
    uint32_t sum = 0; for(uint8_t b : v) sum += b;
    put32(v, fixSum ? sum : sum ^ 0x5A5A);
    return v;
}
int main()
{
    auto a = save({ 1000, 52000, 4000, 300 });
    ok(VcSaveScan(a.data(), a.size()) == 1, "valid save");
    auto b = save({ 1000, 52000 }, false);
    ok(VcSaveScan(b.data(), b.size()) == 1, "valid blocks, other checksum = the game decides");
    auto c = a; c[4 + 1000] = 0xFF; c[4 + 1000 + 1] = 0xFF; c[4 + 1000 + 2] = 0xFF; c[4 + 1000 + 3] = 0x7F;   // 2nd size = huge
    ok(VcSaveScan(c.data(), c.size()) == 0, "block size past the end = skipped (the crash)");
    auto d = a; d.resize(a.size() / 2);   // cut file
    ok(VcSaveScan(d.data(), d.size()) == 0, "cut save = skipped");
    ok(VcSaveScan(a.data(), 3) == 0, "3 bytes = skipped");
    std::vector<uint8_t> z(64, 0);
    ok(VcSaveScan(z.data(), z.size()) == 1, "all zero (size 0) = the game decides");
    printf(fails ? "vc_save_test: %d failed\n" : "vc_save_test: all passed\n", fails);
    return fails ? 1 : 0;
}
