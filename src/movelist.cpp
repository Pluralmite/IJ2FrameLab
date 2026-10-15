#include "movelist.h"
#include "log.h"
#include <windows.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <cstring>
#include <cctype>
#include <algorithm>

namespace movelist {

static std::mutex g_mtx;
static std::map<std::string, Entry> g_table;
static std::atomic<bool> g_scanning{false};
static std::atomic<int> g_gen{0};

const char* TypeName(int t) {
    switch (t) { case 1: return "High"; case 2: return "Mid"; case 3: return "Low"; case 5: return "Throw"; case 6: return "Overhead"; case 7: return "Utility"; default: return "?"; }
}

// Record layout (0x68 bytes), preceded by a 14-dword header {char* name, 1, 1|2, 0 x10}:
//   +0 type  +4 damage(f)  +8 block damage(f)  +C startup  +10 active  +14 recovery  +18 block adv
//   +1C hit adv  +20 cancel  +24 0  +28 0  +2C 1
static bool ReadName(uintptr_t p, std::string& out) {
    char b[72];
    SIZE_T got = 0;
    if (p < 0x10000 || !ReadProcessMemory(GetCurrentProcess(), (void*)p, b, sizeof(b), &got) || got != sizeof(b)) return false;
    int n = 0;
    while (n < 70 && b[n] > 32 && b[n] < 127) ++n;
    if (n < 3 || b[n] != 0) return false;
    out.assign(b, n);
    return true;
}

static void ScanThread() {
    ULONGLONG t0 = GetTickCount64();
    std::map<std::string, Entry> found;
    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t a = 0x10000;
    std::vector<uint8_t> buf;
    std::vector<std::string> loose;
    std::map<std::string, std::pair<uintptr_t, uintptr_t>> nameRange;   // prefix -> name strings span
    size_t scanned = 0;
    while (a < 0x7FFFFFFF0000ull && VirtualQuery((void*)a, &mbi, sizeof(mbi))) {
        uintptr_t next = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
        bool ok = mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE && (mbi.Protect & PAGE_READWRITE) && !(mbi.Protect & PAGE_GUARD) &&
                  mbi.RegionSize < (256u << 20);
        if (ok) {
            buf.resize(mbi.RegionSize);
            SIZE_T got = 0;
            if (ReadProcessMemory(GetCurrentProcess(), mbi.BaseAddress, buf.data(), mbi.RegionSize, &got) && got >= 0x100) {
                scanned += got;
                const int32_t* I = (const int32_t*)buf.data();
                size_t n = got / 4;
                for (size_t i = 0; i + 40 < n; i += 2) {   // header starts with an 8-byte pointer: 8-aligned
                    bool strict = I[i + 2] == 1 && (I[i + 3] == 1 || I[i + 3] == 2);
                    // header flags: normals have all zeros, specials have a few 1s (e.g. Grodd Stampede: 0 0 0 1 0 1 0 0 0 0)
                    if (strict) for (int k = 4; k < 14; ++k) if (I[i + k] != 0 && I[i + k] != 1) { strict = false; break; }
                    if (!strict) {   // exploratory: same record shape behind a different header (specials?)
                        const int32_t* r = I + i + 14;
                        if (r[11] < 1 || r[11] > 4 || r[0] < 1 || r[0] > 10 || r[3] < 1 || r[3] > 200 || r[4] < 0 || r[4] > 200 || r[5] < 0 || r[5] > 300) continue;
                        uintptr_t np; memcpy(&np, I + i, 8);
                        std::string nm;
                        if (!ReadName(np, nm) || nm.size() < 4 || nm.find('_') == std::string::npos || nm.find('_') > 4) continue;
                        char line[400];
                        snprintf(line, sizeof(line), "%s hdr %d %d %d %d %d %d | %d %d %d %d %d %d %d %d %d %d %d %d", nm.c_str(), I[i + 2], I[i + 3], I[i + 4], I[i + 5],
                                 I[i + 6], I[i + 7], r[0], r[3], r[4], r[5], r[6], r[7], r[8], r[9], r[10], r[11], r[12], r[13]);
                        loose.push_back(line);
                        continue;
                    }
                    const int32_t* r = I + i + 14;
                    float dmg, blk;
                    memcpy(&dmg, r + 1, 4); memcpy(&blk, r + 2, 4);
                    if (r[0] < 1 || r[0] > 10 || !(dmg >= 0 && dmg < 0.6f) || !(blk >= 0 && blk < 0.6f)) continue;
                    if (r[3] < 1 || r[3] > 200 || r[4] < 0 || r[4] > 200 || r[5] < 0 || r[5] > 300) continue;
                    if (r[6] < -200 || r[6] > 300 || r[7] < -200 || r[7] > 400 || r[8] < 0 || r[8] > 200 || r[11] < 1 || r[11] > 4) continue;
                    uintptr_t np;
                    memcpy(&np, I + i, 8);
                    Entry e;
                    if (!ReadName(np, e.name)) continue;
                    {
                        auto& rg = nameRange[e.name.substr(0, e.name.find('_'))];
                        rg.first = rg.first ? std::min(rg.first, np) : np; rg.second = std::max(rg.second, np);
                    }
                    e.type = r[0]; e.damage = dmg; e.blockDamage = blk;
                    e.startup = r[3]; e.active = r[4]; e.recovery = r[5]; e.blockAdv = r[6]; e.hitAdv = r[7]; e.cancel = r[8];
                    e.category = r[11];
                    for (int k = 4; k < 14; ++k) e.flags = (e.flags << 1) | (I[i + k] & 1);
                    found[e.name] = e;
                }
            }
        }
        a = next;
    }
    {
        std::lock_guard<std::mutex> l(g_mtx);
        g_table.swap(found);
    }
    {
        // also write it out (used by the automatic sweep)
        char path[MAX_PATH];
        HMODULE self = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&ScanThread, &self);
        GetModuleFileNameA(self, path, MAX_PATH);
        std::string dir = path;
        dir = dir.substr(0, dir.find_last_of("\\/") + 1);
        FILE* f = fopen((dir + "IJ2FrameLab_movelist.txt").c_str(), "w");
        if (f) {
            std::lock_guard<std::mutex> l(g_mtx);
            for (auto& kv : g_table) {
                const Entry& e = kv.second;
                fprintf(f, "%s %d %.4f %.4f %d %d %d %d %d %d %d %03X\n", e.name.c_str(), e.type, e.damage, e.blockDamage, e.startup, e.active, e.recovery,
                        e.blockAdv, e.hitAdv, e.cancel, e.category, e.flags);
            }
            fclose(f);
        }
        f = fopen((dir + "IJ2FrameLab_movelist_loose.txt").c_str(), "w");
        if (f) { for (auto& l : loose) fprintf(f, "%s\n", l.c_str()); fclose(f); }
    }
    Log("movelist: %d entries found in %.1f s (%zu MB scanned)", (int)g_table.size(), (GetTickCount64() - t0) / 1000.0, scanned >> 20);
    g_gen++;
    g_scanning = false;
}

void RequestScan() {
    bool expected = false;
    if (!g_scanning.compare_exchange_strong(expected, true)) return;
    std::thread(ScanThread).detach();
}
bool Scanning() { return g_scanning; }
int Generation() { return g_gen; }
std::map<std::string, Entry> Table() { std::lock_guard<std::mutex> l(g_mtx); return g_table; }

} // namespace movelist

namespace movelist {
static const char* DirLetter(const std::string& w) {
    if (w == "AWAY") return "B";
    if (w == "TOWARDS") return "F";
    if (w == "DOWN") return "D";
    if (w == "UP") return "U";
    if (w == "DOWNTOWARDS" || w == "TOWARDSDOWN") return "DF";
    if (w == "DOWNAWAY" || w == "AWAYDOWN") return "DB";
    if (w == "UPTOWARDS" || w == "TOWARDSUP") return "UF";
    if (w == "UPAWAY" || w == "AWAYUP") return "UB";
    return nullptr;
}
// ForceOfMindMB -> Force Of Mind MB
static std::string Words(const std::string& t) {
    if (t == "SUPERMOVE") return "Super Move";
    bool lower = false;
    for (char c : t) if (c >= 'a' && c <= 'z') lower = true;
    if (!lower) {   // ALLCAPS word: title case, short ones (MB) as they are
        if (t.size() <= 3) return t;
        std::string w = t;
        for (size_t i = 1; i < w.size(); ++i) w[i] = (char)tolower((unsigned char)w[i]);
        return w;
    }
    std::string w;
    for (size_t i = 0; i < t.size(); ++i) {
        char c = t[i];
        bool up = c >= 'A' && c <= 'Z', dig = c >= '0' && c <= '9';
        if (i && (up || dig)) {
            char p = t[i - 1];
            bool pUp = p >= 'A' && p <= 'Z', pDig = p >= '0' && p <= '9';
            bool nLow = i + 1 < t.size() && t[i + 1] >= 'a' && t[i + 1] <= 'z';
            if ((up && (!pUp || nLow)) || (dig && !pDig)) w += ' ';
        }
        w += c;
    }
    return w;
}
std::string Notation(const std::string& name) {
    std::vector<std::string> tok;
    size_t a = 0;
    for (size_t i = 0; i <= name.size(); ++i)
        if (i == name.size() || name[i] == '_') { if (i > a) tok.push_back(name.substr(a, i - a)); a = i + 1; }
    // drop a character prefix (WW_, MN_ ...) if one is still attached
    if (tok.size() > 1 && tok[0].size() <= 4 && tok[0] != "AIR" && !DirLetter(tok[0]) && tok[0].find("ATTACK") != 0 &&
        tok[0].find("THROW") != 0 && !(tok[0][0] >= '1' && tok[0][0] <= '4'))
        tok.erase(tok.begin());
    std::string out;
    bool lastHit = false, air = false;
    auto add = [&](const std::string& piece, bool hit) {
        if (!out.empty()) out += (hit && lastHit) ? "," : " ";
        out += piece; lastHit = hit;
    };
    for (const std::string& t : tok) {
        std::string body = t;
        if (body.compare(0, 6, "ATTACK") == 0) body = body.substr(6);
        if (!body.empty() && body[0] >= '1' && body[0] <= '4') {           // a button press, maybe with a direction
            std::string rest = body.substr(1), btn(1, body[0]);
            while (rest.size() >= 2 && rest[0] == 'N' && rest[1] >= '1' && rest[1] <= '4') { btn += '+'; btn += rest[1]; rest = rest.substr(2); }   // 1N3 = 1+3
            const char* d = DirLetter(rest);
            add(std::string(air ? "J" : "") + (d ? d : "") + btn, true);
            air = false;
            continue;
        }
        if (t.compare(0, 5, "THROW") == 0) {
            const char* d = DirLetter(t.substr(5));
            add(std::string(d ? d : "") + (d ? " Throw" : "Throw"), false);
            continue;
        }
        if (t == "AIR") { air = true; continue; }
        if (const char* d = DirLetter(t)) { add(d, false); continue; }
        add(Words(t), false);
    }
    return out.empty() ? name : out;
}
}
