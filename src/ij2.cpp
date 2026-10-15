#include <map>
#include <cstring>
#include <cmath>
#include "ij2.h"
#include "memory.h"
#include "log.h"
#include "MinHook.h"
#include "trace.h"
#include "bot.h"
#include "buffer.h"
namespace dib { void Init(); }
#include <windows.h>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <deque>
#include <thread>
#include <algorithm>

namespace ij2 {

// ----------------------------------------------------------------------------- signatures
// The first 11 signatures (MKProcDispatch .. SlowGameTimeForXTicks) are from I2Hook by ermaccer
// (https://github.com/ermaccer/I2Hook, plugin/PatternSolver.cpp). The collision / hitbox ones are ours.
struct Sig { const char* name; const char* pat; int off; bool rip; uintptr_t addr; };
enum {
    S_MKProcDispatch, S_Dispatch, S_FindGameInfo, S_GetInfo, S_GetObj, S_GetPos, S_CamSetPos,
    S_GetBonePos, S_GetBoneName, S_GetNames, S_SlowGameTime,
    S_CollCtor0, S_CollCtor1, S_MeleeCtor, S_ProjCtor, S_PCICtor, S_ResCtor, S_StrikeTest, S_AttachMat,
    S_COUNT
};
static Sig g_sigs[S_COUNT] = {
    {"MKProcDispatch", "48 8B C4 41 56 48 83 EC 60 48 C7 40 ? ? ? ? ? 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 48 8D 0D ? ? ? ? E8", 0, false, 0},
    {"Dispatch", "8B 51 18 39 51 14 74 0D 48 8B 01 89 51 14 48 FF A0 F8 00 00 00", 0, false, 0},
    {"FindGameInfo", "48 8B 15 ? ? ? ? 45 33 C9 48 81 C2 ? ? ? ? B9 07 00 FF 00 45 8D 41 04 E8 ? ? ? ? 33 C0", 3, true, 0},
    {"FGGameInfo_GetInfo", "83 FA 03 77 0C 48 63 C2 48 8B 84 C1 ? ? ? ? C3", 0, false, 0},
    {"GetObj", "40 53 48 83 EC 20 8B 41 28 3D ? ? ? ? 73 40 8B D0 4C 8D 05 ? ? ? ? 8B 41 2C 41 39 04 90 75 2E 48 8B 59 20 48 85 DB 74 25 48 8B 03 48 8B CB", 0, false, 0},
    {"MKObject_GetPos", "40 53 48 83 EC 70 48 8B DA 48 8B 51 20 48 85 D2 74 3F 48 85 DB 74 3A 44 8B 41 30 45 85 C0 74 37 41 83 E8 01 74 0B 41 83 E8 01", 0, false, 0},
    {"MKCamera_SetPosition", "40 53 48 83 EC 20 F2 0F 10 02 48 8B D9 F2 0F 11 81 F8 00 00 00 8B 42 08 BA ? ? ? ? 89 81 ? ? ? ? E8 ? ? ? ? 48 8D 8B ? ? ? ? E8", 0, false, 0},
    {"MKCharacter_GetBonePos", "4D 85 C0 74 62 53 48 83 EC 70 33 C0 49 8B D8 48 89 84 24 ? ? ? ? 48 3B D0 74 46 48 3B 15 ? ? ? ? 74 3D 48 3B 15", 0, false, 0},
    {"USkeletalMeshComponent_GetBoneName", "40 53 48 83 EC 20 48 8B 89 ? ? ? ? 48 8B DA 48 8B 89 ? ? ? ? E8 ? ? ? ? 48 8B C3 48 83 C4 20 5B C3", 0, false, 0},
    {"GetNames", "48 8B 05 ? ? ? ? 48 8B 0C C8 48 83 C1 10 E8 ? ? ? ? 0F 28 F0 48 8D 4C 24 ? E8 ? ? ? ? 90", 3, true, 0},
    {"SlowGameTimeForXTicks", "40 57 48 83 EC 50 48 C7 44 24 ? ? ? ? ? 48 89 5C 24 ? 0F 29 74 24 ? 8B FA F3 0F 10 35", 0, false, 0},
    {"CollisionObj_Ctor0", "48 8B C4 48 89 48 08 57 41 56 41 57 48 83 EC 50 48 C7 40 C8 FE FF FF FF 48 89 58 10 48 89 68 18 48 89 70 20 49 8B F9", 0, false, 0},
    {"CollisionObj_Ctor1", "48 8B C4 48 89 48 08 57 41 56 41 57 48 83 EC 50 48 C7 40 C8 FE FF FF FF 48 89 58 10 48 89 68 18 48 89 70 20 41 8B E9", 0, false, 0},
    {"MeleeCollisionObj_Ctor", "48 89 4C 24 08 57 48 83 EC 40 48 C7 44 24 30 FE FF FF FF 48 89 5C 24 58 48 89 74 24 60 49 8B D8 48 8B F2", 0, false, 0},
    {"ProjectileCollisionObj_Ctor", "48 89 4C 24 08 57 48 83 EC 40 48 C7 44 24 30 FE FF FF FF 48 89 5C 24 58 49 8B F8 48 8B D9 48 8B 44 24 70 48 89 44 24 20 E8", 0, false, 0},
    {"PlayerCollisionInfo_Ctor", "48 89 4C 24 08 53 48 83 EC 30 48 C7 44 24 20 FE FF FF FF 48 8B D9 45 33 C0 41 8D 50 01 E8 ? ? ? ? 90 48 8D 05 ? ? ? ? 48 89 03 48 8D 4B 18 E8 ? ? ? ? 45 33 D2", 0, false, 0},
    {"CollisionResolutionObj_Ctor", "48 89 4C 24 08 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 30 DB FF FF", 0, false, 0},
    {"StrikeVsHurtTest", "4C 89 4C 24 20 48 89 54 24 10 55 41 54 41 55 41 56", 0, false, 0},
    {"Attach_GetMatrix", "48 89 5C 24 18 48 89 7C 24 20 55 48 8D 6C 24 A9 48 81 EC B0 00 00 00 48 8B DA", 0, false, 0},
};

// vtable RVAs for this build, verified at runtime through RTTI names
struct VtDef { int kind; uintptr_t rva; const char* rtti; uintptr_t va; };
static VtDef g_vts[] = {
    {OK_CollisionObj, 0x3199da8, ".?AVCollisionObj@@", 0},
    {OK_Melee, 0x319a2a0, ".?AVMeleeCollisionObj@@", 0},
    {OK_Projectile, 0x3199f50, ".?AVProjectileCollisionObj@@", 0},
    {OK_PlayerCollisionInfo, 0x3199ab8, ".?AUPlayerCollisionInfo@@", 0},
    {OK_Resolution, 0x319a748, ".?AVCollisionResolutionObj@@", 0},
};

const char* KindName(int k) {
    switch (k) {
    case OK_CollisionObj: return "CollisionObj";
    case OK_Melee: return "MeleeCollisionObj";
    case OK_Projectile: return "ProjectileCollisionObj";
    case OK_PlayerCollisionInfo: return "PlayerCollisionInfo";
    case OK_Resolution: return "CollisionResolutionObj";
    default: return "?";
    }
}

static uintptr_t g_base = 0;
static bool g_ready = false;
static std::string g_status;
uintptr_t Base() { return g_base; }

// ----------------------------------------------------------------------------- game function types
using VoidFn = void (*)();
using DispatchFn = void (*)(uintptr_t, int);
using GetObjFn = uintptr_t (*)(uintptr_t);
using GetPosFn = void (*)(uintptr_t, Vec3*);
using CamSetPosFn = void (*)(uintptr_t, Vec3*);
using GetBonePosFn = void (*)(uintptr_t, uint64_t, Vec3*);
using GetBoneNameFn = void (*)(uintptr_t, uint64_t*, int);
using SlowFn = void (*)(float, int, int);
using Ctor7 = uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);

using StrikeTestFn = int (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
static StrikeTestFn o_StrikeTest = nullptr;
static std::atomic<uintptr_t> g_pciOf[2], g_objOf[2];
struct StrikeCap { uint64_t seq = 0; std::vector<HurtPoint> pts; uintptr_t victim = 0; };
static std::mutex g_capMtx;
static StrikeCap g_cap[2];
static uint64_t g_capSeq = 0, g_capSeen[2] = {0, 0};

static void ReadStrikeList(uintptr_t pci, std::vector<HurtPoint>& out) {
    uintptr_t S = mem::ReadOr<uintptr_t>(pci + 0x38, 0);
    uint32_t sn = S ? mem::ReadOr<uint32_t>(S + 4, 0) : 0;
    if (sn == 0 || sn > 36) return;
    static uint8_t buf[0x20 * 36 + 0x40];
    if (!mem::ReadRaw(S, buf, 0x2C + 0x20 * sn)) return;
    for (uint32_t i = 0; i < sn; ++i) {
        const uint8_t* e = buf + 0x0C + 0x20 * i;
        HurtPoint h{};
        memcpy(&h.id, e + 0x00, 4);
        memcpy(&h.bone, e + 0x08, 4);
        memcpy(&h.pos, e + 0x10, 12);
        memcpy(&h.radius, e + 0x1C, 4);
        h.part = (int)i;
        out.push_back(h);
    }
}

static int h_StrikeTest(uintptr_t spec, uintptr_t attacker, uintptr_t victim, uintptr_t result) {
    for (int p = 0; p < 2; ++p) {
        if (attacker && attacker == g_objOf[p].load() && g_pciOf[p].load()) {
            std::vector<HurtPoint> pts;
            ReadStrikeList(g_pciOf[p].load(), pts);
            std::lock_guard<std::mutex> l(g_capMtx);
            g_cap[p].pts.swap(pts);
            g_cap[p].victim = victim;
            g_cap[p].seq = ++g_capSeq;
        }
    }
    return o_StrikeTest(spec, attacker, victim, result);
}

using AttachMatFn = int (*)(uintptr_t, float*);
using GetMatVFn = float* (*)(uintptr_t, float*);

// The PlayerCollisionInfo is created when the match loads. After a hot reload we missed its constructor,
// so look for it through pointers held by the fighter object / player info (1 and 2 levels deep).
static uintptr_t FindPciByScan(uintptr_t obj, uintptr_t info) {
    static uintptr_t cached[2] = {0, 0};
    static uintptr_t cachedObj[2] = {0, 0};
    static ULONGLONG lastTry = 0;
    uintptr_t vtPci = 0;
    for (auto& v : g_vts) if (v.kind == OK_PlayerCollisionInfo) vtPci = v.va;
    if (!vtPci || !obj) return 0;
    for (int i = 0; i < 2; ++i)
        if (cachedObj[i] == obj && cached[i] && mem::ReadOr<uintptr_t>(cached[i], 0) == vtPci &&
            mem::ReadOr<uintptr_t>(cached[i] + 0x28, 0) == obj) return cached[i];
    ULONGLONG now = GetTickCount64();
    if (now - lastTry < 200) return 0;
    lastTry = now;
    auto isPci = [&](uintptr_t p) { return p && mem::ReadOr<uintptr_t>(p, 0) == vtPci && mem::ReadOr<uintptr_t>(p + 0x28, 0) == obj; };
    uintptr_t roots[2] = {obj, info};
    static uint8_t buf[0x4000], buf2[0x1000];
    for (uintptr_t root : roots) {
        if (!root || !mem::ReadRaw(root, buf, sizeof(buf))) continue;
        for (int o = 0; o < (int)sizeof(buf); o += 8) {
            uintptr_t p; memcpy(&p, buf + o, 8);
            if (p < 0x10000 || (p & 7)) continue;
            if (isPci(p)) goto found1;
            continue;
        found1:
            for (int i = 0; i < 2; ++i) if (!cached[i] || cachedObj[i] == obj) { cached[i] = p; cachedObj[i] = obj; break; }
            Log("pci: found by scan at root+0x%X (%s)", o, root == obj ? "obj" : "info");
            return p;
        }
    }
    for (uintptr_t root : roots) {
        if (!root || !mem::ReadRaw(root, buf, sizeof(buf))) continue;
        for (int o = 0; o < 0x1000; o += 8) {
            uintptr_t p; memcpy(&p, buf + o, 8);
            if (p < 0x10000 || (p & 7) || !mem::ReadRaw(p, buf2, sizeof(buf2))) continue;
            for (int o2 = 0; o2 < (int)sizeof(buf2); o2 += 8) {
                uintptr_t q; memcpy(&q, buf2 + o2, 8);
                if (q < 0x10000 || (q & 7) || !isPci(q)) continue;
                for (int i = 0; i < 2; ++i) if (!cached[i] || cachedObj[i] == obj) { cached[i] = q; cachedObj[i] = obj; break; }
                Log("pci: found by scan at [root+0x%X]+0x%X (%s)", o, o2, root == obj ? "obj" : "info");
                return q;
            }
        }
    }
    return 0;
}


// ---- exact geometry through the game's own functions (RVAs for this build, byte-checked) ----
struct TArr { void* data; int count; int max; };
using PolyFn = void (*)(const float* center, const float* axis, float radius, float length, TArr* pts, TArr* normals, int flag);
using FreeFn = void (*)(void*);
using ShapeGetFn = float (*)(uintptr_t);
static PolyFn g_polyFn = nullptr;
static FreeFn g_freeFn = nullptr;
static ShapeGetFn g_getLen = nullptr, g_getRad = nullptr;
static uintptr_t g_planeO = 0, g_planeA = 0, g_hurtWild = 0;
static bool CheckBytes(uintptr_t a, const char* hex) {
    uint8_t b[16];
    if (!mem::ReadRaw(a, b, 16)) return false;
    for (int i = 0; i < 16 && hex[i * 3]; ++i) {
        unsigned v; sscanf(hex + i * 3, "%2x", &v);
        if (b[i] != v) return false;
    }
    return true;
}
static void InitExact() {
    if (CheckBytes(g_base + 0x99a560, "48 8b c4 48 89 58 10 48 89 70 18 55 57 41 54 41")) g_polyFn = (PolyFn)(g_base + 0x99a560);
    if (CheckBytes(g_base + 0x176a540, "48 ff 25 51 82 d0 02")) g_freeFn = (FreeFn)(g_base + 0x176a540);
    if (CheckBytes(g_base + 0x99b3b0, "40 53 48 83 ec 20 83 b9 f8 03 00 00 00 48 8b d9")) g_getLen = (ShapeGetFn)(g_base + 0x99b3b0);
    if (CheckBytes(g_base + 0x99c7b0, "40 53 48 83 ec 20 83 b9 f8 03 00 00 00 48 8b d9")) g_getRad = (ShapeGetFn)(g_base + 0x99c7b0);
    if (CheckBytes(g_base + 0x954bd0, "48 89 5c 24 08 4c 8b 91 58 10 00 00 48 8b d9 4d")) g_hurtWild = g_base + 0x43d6be8;
    g_planeO = g_base + 0x418b7e8;
    g_planeA = g_base + 0x418b810;
    Log("exact geometry: poly %s free %s len %s rad %s hurtscale %s", g_polyFn ? "ok" : "MISSING", g_freeFn ? "ok" : "MISSING",
        g_getLen ? "ok" : "MISSING", g_getRad ? "ok" : "MISSING", g_hurtWild ? "ok" : "MISSING");
}
// world point from a 2D fight-plane point (x along the plane axis, y = height)
static Vec3 PlaneToWorld(float x, float y) {
    float O[3] = {0, 0, 0}, A[3] = {0, 1, 0};
    mem::ReadRaw(g_planeO, O, 12);
    mem::ReadRaw(g_planeA, A, 12);
    float n2 = A[0] * A[0] + A[1] * A[1] + A[2] * A[2];
    if (n2 < 1e-6f) n2 = 1;
    return Vec3{O[0] + A[0] * x / n2, O[1] + A[1] * x / n2, y};
}
// the hurt-sphere size the game actually uses (moves can scale or replace hurt radii per sphere)
// mirrors game fn 0x954bd0: table at defender+0x1058 (count +0x1050), entries {u64 key, float v}
static float HurtRadius(uintptr_t defender, const uint8_t* entry, float r) {
    if (!g_hurtWild || !defender) return r;
    uint32_t n = mem::ReadOr<uint32_t>(defender + 0x1050, 0);
    uintptr_t d = mem::ReadOr<uintptr_t>(defender + 0x1058, 0);
    if (!n || !d || n > 512) return r;
    int mode = mem::ReadOr<int>(defender + 0x1054, 0);
    uint64_t key; memcpy(&key, entry, 8);
    uint64_t wild = mem::ReadOr<uint64_t>(g_hurtWild, 0);
    for (int pass = 0; pass < 2; ++pass)
        for (uint32_t i = 0; i < n; ++i) {
            uint64_t k = mem::ReadOr<uint64_t>(d + i * 12, 0);
            if (k == (pass ? wild : key)) {
                float v = mem::ReadOr<float>(d + i * 12 + 8, 1.f);
                return mode == 0 ? v : r * v;   // 0x1054 == 0: replace the radius, otherwise scale it
            }
        }
    return r;
}
// exact hit polygon for a capsule: the game's own 2D builder (same call its hit test makes)
static void ExactPoly(const float* c, const float* ax, float R, float L, std::vector<Vec3>& out) {
    if (!g_polyFn) return;
    alignas(16) float center[4] = {c[0], c[1], c[2], 0};
    alignas(16) float axis[4] = {ax[0], ax[1], ax[2], 0};
    TArr pts{}, nrm{};
    g_polyFn(center, axis, R, L, &pts, &nrm, 0);
    if (pts.data && pts.count > 0 && pts.count < 64) {
        const float* p = (const float*)pts.data;
        for (int i = 0; i < pts.count; ++i) out.push_back(PlaneToWorld(p[i * 2], p[i * 2 + 1]));
    }
    if (g_freeFn) { if (pts.data) g_freeFn(pts.data); if (nrm.data) g_freeFn(nrm.data); }
}
// Projectiles / traps can exist (state 1) before and after they can hit: +0x384 is their "armed" flag
// (Swamp Thing's log: spawned on frame 14, armed frames 25-55). Unarmed = startup (0) before, finished (2) after.
struct ProjArm { bool everArmed = false; int age = 0, first = -1, last = -1; uint64_t seenTick = 0; };
static std::map<std::pair<uintptr_t, int64_t>, ProjArm> g_projArm;
static int ProjState(const Tracked& t, int st, uint64_t tick, bool advance) {
    if (t.kind != OK_Projectile) return st;
    ProjArm& a = g_projArm[{t.ptr, (int64_t)t.createdTick}];
    int armed = mem::ReadOr<int>(t.ptr + 0x384, 1);
    if (advance && a.seenTick != tick) {
        a.seenTick = tick;
        if (armed && st == 1) { if (a.first < 0) a.first = a.age; a.last = a.age; }
        a.age++;
    }
    if (st != 1) return st;
    if (armed) { a.everArmed = true; return 1; }
    return a.everArmed ? 2 : 0;
}
static void ProjArmPrune(const Snapshot& s) {
    for (auto it = g_projArm.begin(); it != g_projArm.end();) {
        bool alive = false;
        for (auto& t : s.live) if (t.ptr == it->first.first && (int64_t)t.createdTick == it->first.second) { alive = true; break; }
        if (!alive) {
            if (g_logVerbose) Log("projarm: %llX lived %d frames, armed %d..%d", (unsigned long long)it->first.first, it->second.age, it->second.first, it->second.last);
            it = g_projArm.erase(it);
        } else ++it;
    }
}
static std::atomic<bool> g_armShapeTrace{false};
// diagnostics: per-frame memory of the next projectile P1 fires (armed with the 'projdump' command)
static std::atomic<bool> g_projDumpArm{false};
void ArmProjDump() { g_projDumpArm = true; }
static void ProjDump(const Snapshot& s) {
    static uintptr_t cur = 0;
    static FILE* f = nullptr;
    static int age = 0;
    if (!cur && g_projDumpArm) {
        for (auto& t : s.live) {
            if (t.kind != OK_Projectile) continue;
            uintptr_t owner = mem::ReadOr<uintptr_t>(t.ptr + 0x330, 0);
            if (!owner || owner != s.p[0].obj) continue;
            cur = t.ptr; age = 0; g_projDumpArm = false;
            char path[MAX_PATH];
            HMODULE self = nullptr;
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&ProjDump, &self);
            GetModuleFileNameA(self, path, MAX_PATH);
            std::string dir = path;
            dir = dir.substr(0, dir.find_last_of("\\/") + 1);
            f = fopen((dir + "IJ2FrameLab_projdump.txt").c_str(), "w");
            Log("projdump: following %llX", (unsigned long long)cur);
            break;
        }
    }
    if (!cur) return;
    bool alive = false;
    for (auto& t : s.live) if (t.ptr == cur) alive = true;
    if (!alive || !f) {
        if (f) fclose(f);
        f = nullptr; cur = 0;
        Log("projdump: done (%d frames)", age);
        return;
    }
    if (!s.advanced) return;
    static uint32_t buf[0x600 / 4];
    mem::ReadRaw(cur, buf, sizeof(buf));
    fprintf(f, "age %d frame %llu P1 %X P2 %X :", age++, (unsigned long long)s.frame, s.p[0].stateFlags, s.p[1].stateFlags);
    for (uint32_t v : buf) fprintf(f, " %X", v);
    fprintf(f, "\n");
}
static void SampleShapes(Snapshot& s) {
    ProjDump(s);
    for (auto& t : s.live) {
        if (t.kind != OK_CollisionObj && t.kind != OK_Melee && t.kind != OK_Projectile) continue;
        int type = mem::ReadOr<int>(t.ptr + 0x38C, 0);
        if (type < 1 || type > 4) continue;
        ShapeSnap sh;
        sh.ptr = t.ptr; sh.kind = t.kind; sh.type = type;
        sh.state = ProjState(t, mem::ReadOr<int>(t.ptr + 0x380, -1), s.tick, false);
        uintptr_t owner = mem::ReadOr<uintptr_t>(t.ptr + 0x330, 0);
        for (int p = 0; p < 2; ++p) if (owner && owner == s.p[p].obj) sh.owner = p;
        sh.radius = mem::ReadOr<float>(t.ptr + 0x3EC, 0);
        sh.width = mem::ReadOr<float>(t.ptr + 0x3F0, 0);
        sh.length = mem::ReadOr<float>(t.ptr + 0x3F4, 0);
        // world position of the collision object (same accessor the game uses)
        if (mem::ReadOr<uintptr_t>(t.ptr + 0x20, 0)) ((GetPosFn)g_sigs[S_GetPos].addr)(t.ptr, &sh.pos);
        alignas(16) float m[16];
        if (type == 4 && g_sigs[S_AttachMat].addr) {
            if (((AttachMatFn)g_sigs[S_AttachMat].addr)(t.ptr + 0x390, m)) { memcpy(sh.mat, m, sizeof(m)); sh.hasMat = true; }
        } else if (type == 2 || type == 3) {
            uintptr_t mo = mem::ReadOr<uintptr_t>(t.ptr + 0x48, 0);
            if (mo && mem::ReadOr<int>(t.ptr + 0x30, 1) == 0) {
                uintptr_t vt = mem::ReadOr<uintptr_t>(mo, 0);
                uintptr_t fn = vt ? mem::ReadOr<uintptr_t>(vt + 0x18, 0) : 0;
                if (fn >= g_base && fn < g_base + mem::ModuleSize()) {
                    alignas(16) float tmp[16];
                    float* r = ((GetMatVFn)fn)(mo, tmp);
                    if (r && mem::ReadRaw((uintptr_t)r, m, sizeof(m))) { memcpy(sh.mat, m, sizeof(m)); sh.hasMat = true; }
                }
            }
            if (!sh.hasMat) { // fall back: axis-aligned at position
                memset(sh.mat, 0, sizeof(sh.mat));
                sh.mat[0] = sh.mat[5] = sh.mat[10] = sh.mat[15] = 1.f;
                sh.mat[12] = sh.pos.x; sh.mat[13] = sh.pos.y; sh.mat[14] = sh.pos.z;
            }
        }
        // sizes through the game's getters (they apply per-move overrides)
        if (g_getRad) sh.radius = g_getRad(t.ptr);
        if (g_getLen) sh.length = g_getLen(t.ptr);
        if (type == 2 && sh.hasMat) {
            ExactPoly(&sh.mat[12], &sh.mat[0], sh.radius, sh.length, sh.poly);
        } else if (type == 4 && sh.hasMat) {
            // swept capsule: from the attach point to the current position
            float a[3] = {sh.mat[12], sh.mat[13], sh.mat[14]}, b[3] = {sh.pos.x, sh.pos.y, sh.pos.z};
            float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
            float L = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (L > 0.01f) {
                float c[3] = {(a[0] + b[0]) * 0.5f, (a[1] + b[1]) * 0.5f, (a[2] + b[2]) * 0.5f};
                float u[3] = {d[0] / L, d[1] / L, d[2] / L};
                ExactPoly(c, u, sh.radius, L, sh.poly);
            }
        }
        if (g_armShapeTrace && sh.owner == 0 && (sh.state == 0 || sh.state == 1)) {
            g_armShapeTrace = false;
            uintptr_t a[4] = {t.ptr + 0x3EC, t.ptr + 0x3F4, t.ptr + 0x390, t.ptr + 0x38C};
            const char* n[4] = {"shape+3EC (r/height)", "shape+3F4 (length)", "shape+390 (attach)", "shape+38C (type)"};
            trace::Request(a, n, 2, std::string("shape_") + KindName(t.kind));
            Log("shape trace armed on %s %llX type %d r %.1f l %.1f", KindName(t.kind), (unsigned long long)t.ptr, type, sh.radius, sh.length);
        }
        s.shapes.push_back(sh);
    }
}

static VoidFn o_ProcDispatch = nullptr;
static DispatchFn o_Dispatch = nullptr;
static CamSetPosFn o_CamSetPos = nullptr;
static Ctor7 o_Ctor[6] = {};

static uintptr_t g_pGameInfo = 0;   // address of the global pointer
static int32_t g_infoTableOff = 0;  // GameInfo + off + plr*8
static uintptr_t g_pNames = 0;      // TArray<FNameEntry*>

static std::atomic<uintptr_t> g_camera{0};
static std::atomic<bool> g_frozen{false};
static std::atomic<int> g_stepPending{0};

// ----------------------------------------------------------------------------- tracking
static std::mutex g_trackMtx;
static std::vector<Tracked> g_tracked;
static std::deque<std::string> g_events;
static std::atomic<uint64_t> g_tick{0};
static std::atomic<bool> g_armMelee{false};
static int g_meleeTraceN = 0;
void ArmShapeTrace() { g_armShapeTrace = true; }
void ArmMeleeTrace() { g_armMelee = true; }
bool MeleeTraceArmed() { return g_armMelee; }

static void AddEvent(const std::string& s) {
    g_events.push_back(s);
    while (g_events.size() > 400) g_events.pop_front();
}

// Subclasses (weapon / special attack collision objects) have their own vtables: classify them
// through their RTTI names, cached per vtable.
static std::map<uintptr_t, int> g_vtKindCache;
static int KindOfVtable(uintptr_t vt) {
    for (auto& v : g_vts) if (v.va && v.va == vt) return v.kind;
    if (vt < g_base || vt >= g_base + mem::ModuleSize()) return OK_Unknown;
    auto it = g_vtKindCache.find(vt);
    if (it != g_vtKindCache.end()) return it->second;
    int k = OK_Unknown;
    uintptr_t col = mem::ReadOr<uintptr_t>(vt - 8, 0);
    char name[96] = {};
    if (col && mem::ReadOr<uint32_t>(col, 0) == 1) {
        uint32_t tdRva = mem::ReadOr<uint32_t>(col + 12, 0);
        if (tdRva && tdRva < mem::ModuleSize() && mem::ReadRaw(g_base + tdRva + 0x10, name, sizeof(name) - 1)) {
            name[sizeof(name) - 1] = 0;
            if (strstr(name, "PlayerCollisionInfo")) k = OK_PlayerCollisionInfo;
            else if (strstr(name, "Melee")) k = OK_Melee;
            else if (strstr(name, "Projectile")) k = OK_Projectile;
            else if (strstr(name, "CollisionResolution")) k = OK_Resolution;
            else if (strstr(name, "Collision")) k = OK_CollisionObj;
        }
    }
    g_vtKindCache[vt] = k;
    Log("vtable %llX rtti '%s' -> %s", (unsigned long long)(vt - g_base), name, k == OK_Unknown ? "ignored" : KindName(k));
    return k;
}

static void Track(int ctorIdx, uintptr_t self, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t ret) {
    std::lock_guard<std::mutex> l(g_trackMtx);
    uintptr_t rva = (ret >= g_base && ret < g_base + mem::ModuleSize()) ? ret - g_base : ret;
    for (auto& t : g_tracked) {
        if (t.ptr == self) {
            // outer (derived) constructors return last - keep their caller + args
            t.callerRva = rva; t.arg1 = a1; t.arg2 = a2; t.arg3 = a3;
            t.createdTick = g_tick.load();
            return;
        }
    }
    Tracked t;
    t.ptr = self; t.callerRva = rva; t.arg1 = a1; t.arg2 = a2; t.arg3 = a3;
    t.createdTick = g_tick.load();
    g_tracked.push_back(t);
    if (g_tracked.size() > 4096) g_tracked.erase(g_tracked.begin());
    (void)ctorIdx;
}

#define CTOR_HOOK(I)                                                                                  \
    static uintptr_t h_Ctor##I(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e, uintptr_t f, uintptr_t g) { \
        uintptr_t r = o_Ctor[I](a, b, c, d, e, f, g);                                                 \
        Track(I, a, b, c, d, (uintptr_t)__builtin_return_address(0));                                 \
        return r;                                                                                     \
    }
CTOR_HOOK(0) CTOR_HOOK(1) CTOR_HOOK(2) CTOR_HOOK(3) CTOR_HOOK(4) CTOR_HOOK(5)
static Ctor7 g_ctorHooks[6] = {h_Ctor0, h_Ctor1, h_Ctor2, h_Ctor3, h_Ctor4, h_Ctor5};

// ----------------------------------------------------------------------------- game accessors
// GameInfo+0x10: 1 while the game's pause menu is open (found by diffing memory paused vs running)
bool GamePaused() {
    // GI+0x10 = paused, GI+0x14 = why: 0x200 pause menu ... 7 = halted by our own freeze / frame step
    // (F5 / F6), which must keep the overlay visible
    uintptr_t gi = GameInfo();
    if (!gi || mem::ReadOr<int>(gi + 0x10, 0) == 0) return false;
    return mem::ReadOr<int>(gi + 0x14, 0) != 7 && !g_frozen.load() && g_stepPending.load() == 0;
}

uintptr_t GameInfo() {
    if (!g_pGameInfo) return 0;
    return mem::ReadOr<uintptr_t>(g_pGameInfo, 0);
}

uintptr_t GetInfo(int plr) {
    uintptr_t gi = GameInfo();
    if (!gi || plr < 0 || plr > 3) return 0;
    return mem::ReadOr<uintptr_t>(gi + g_infoTableOff + plr * 8, 0);
}

uintptr_t GetObj(int plr) {
    uintptr_t info = GetInfo(plr);
    if (!info || !g_sigs[S_GetObj].addr) return 0;
    return ((GetObjFn)g_sigs[S_GetObj].addr)(info);
}

static std::mutex g_nameMtx;
std::string BoneName(int idx) {
    if (!g_pNames || idx < 0) return "?";
    uintptr_t data = mem::ReadOr<uintptr_t>(g_pNames, 0);
    int count = mem::ReadOr<int>(g_pNames + 8, 0);
    if (!data || idx >= count) return "?";
    uintptr_t e = mem::ReadOr<uintptr_t>(data + (uintptr_t)idx * 8, 0);
    if (!e) return "?";
    char buf[129] = {};
    if (!mem::ReadRaw(e + 0x10, buf, 128)) return "?";
    buf[128] = 0;
    return buf;
}

// bone FName cache per character
struct BoneCache { uintptr_t obj = 0, comp = 0; int count = 0; std::vector<uint64_t> names; uint32_t charKey = 0; };
static BoneCache g_boneCache[2];
static std::atomic<bool> g_sampleBones{true};

static void RefreshBones(int p, uintptr_t obj) {
    BoneCache& c = g_boneCache[p];
    uintptr_t comp = mem::ReadOr<uintptr_t>(obj + 0x328, 0);
    int count = comp ? mem::ReadOr<int>(comp + 0x730 + 8, 0) : 0;
    if (c.obj == obj && c.comp == comp && c.count == count) return;
    c.obj = obj; c.comp = comp; c.count = count; c.names.clear();
    if (!comp || count <= 0 || count > 400 || !g_sigs[S_GetBoneName].addr) return;
    for (int i = 0; i < count; ++i) {
        uint64_t fn = 0;
        ((GetBoneNameFn)g_sigs[S_GetBoneName].addr)(comp, &fn, i);
        c.names.push_back(fn);
    }
    // character identity that survives restarts: hash of the skeleton's bone names
    uint32_t h = 2166136261u;
    for (uint64_t fn : c.names) {
        std::string n = BoneName((int)(fn & 0xFFFFFFFF));
        for (char ch : n) { h ^= (uint8_t)ch; h *= 16777619u; }
        h ^= '|'; h *= 16777619u;
    }
    c.charKey = h;
    Log("P%d: %d bones cached (comp %llX), character key %08X", p + 1, count, (unsigned long long)comp, h);
}

// ----------------------------------------------------------------------------- snapshot
static std::mutex g_snapMtx;
static Snapshot g_snap;

Snapshot GetSnapshot() {
    std::lock_guard<std::mutex> l(g_snapMtx);
    return g_snap;
}
static std::deque<Snapshot> g_frameQ;   // every simulated frame, consumed by the frame meter (render thread)
std::vector<Snapshot> TakeFrames() {
    std::lock_guard<std::mutex> l(g_snapMtx);
    std::vector<Snapshot> v(std::make_move_iterator(g_frameQ.begin()), std::make_move_iterator(g_frameQ.end()));
    g_frameQ.clear();
    return v;
}

// ----------------------------------------------------------------------------- recorder
static const size_t REC_OBJ = 0x20000, REC_INFO = 0x2000, REC_GI = 0x2000, REC_CAM = 0x800, REC_COBJ = 0x1000;
static std::mutex g_recMtx;
static std::atomic<bool> g_recording{false};
static std::atomic<int> g_recLeft{0}, g_recTotal{0};
static std::vector<uint8_t> g_recBuf;
static std::string g_recLabel, g_recLast;
static std::vector<std::string> g_recEvents;
static std::string g_dir;

static void Put(std::vector<uint8_t>& b, const void* p, size_t n) {
    const uint8_t* s = (const uint8_t*)p;
    b.insert(b.end(), s, s + n);
}
template <class T> static void PutV(std::vector<uint8_t>& b, const T& v) { Put(b, &v, sizeof(T)); }
static void PutMem(std::vector<uint8_t>& b, uintptr_t addr, size_t n) {
    size_t at = b.size();
    b.resize(at + n, 0);
    if (!addr) return;
    // page-wise so a hole doesn't zero the whole block
    for (size_t o = 0; o < n;) {
        size_t chunk = 0x1000 - ((addr + o) & 0xFFF);
        if (chunk > n - o) chunk = n - o;
        mem::ReadRaw(addr + o, &b[at + o], chunk);
        o += chunk;
    }
}

void StartRecording(int ticks, const std::string& label) {
    std::lock_guard<std::mutex> l(g_recMtx);
    if (g_recording) return;
    g_recBuf.clear();
    g_recBuf.reserve((size_t)ticks * (2 * (REC_OBJ + REC_INFO) + REC_GI + REC_CAM + 8 * REC_COBJ));
    g_recEvents.clear();
    g_recLabel = label.empty() ? "rec" : label;
    g_recTotal = ticks;
    g_recLeft = ticks;
    const char magic[8] = {'I', 'J', '2', 'R', 'E', 'C', '0', '2'};
    Put(g_recBuf, magic, 8);
    PutV(g_recBuf, (uint64_t)g_base);
    uint32_t sizes[5] = {(uint32_t)REC_OBJ, (uint32_t)REC_INFO, (uint32_t)REC_GI, (uint32_t)REC_CAM, (uint32_t)REC_COBJ};
    Put(g_recBuf, sizes, sizeof(sizes));
    g_recording = true;
    Log("Recording %d ticks (%s)", ticks, g_recLabel.c_str());
}
bool Recording() { return g_recording; }
int RecordProgress() { return g_recTotal ? (int)(100 * (g_recTotal - g_recLeft) / g_recTotal) : 0; }
std::string LastRecordingPath() { std::lock_guard<std::mutex> l(g_recMtx); return g_recLast; }

static void FinishRecording() {
    std::vector<uint8_t> buf;
    std::vector<std::string> events;
    std::string label;
    {
        std::lock_guard<std::mutex> l(g_recMtx);
        buf.swap(g_recBuf);
        events.swap(g_recEvents);
        label = g_recLabel;
        g_recording = false;
    }
    // bone names for both players
    std::string names;
    for (int p = 0; p < 2; ++p) {
        names += "P" + std::to_string(p + 1) + " bones:\n";
        for (size_t i = 0; i < g_boneCache[p].names.size(); ++i)
            names += "  " + std::to_string(i) + " " + std::to_string((int)(g_boneCache[p].names[i] & 0xFFFFFFFF)) + " " +
                     BoneName((int)(g_boneCache[p].names[i] & 0xFFFFFFFF)) + "\n";
    }
    std::thread([buf = std::move(buf), events = std::move(events), names, label]() {
        SYSTEMTIME t;
        GetLocalTime(&t);
        char stamp[64];
        snprintf(stamp, sizeof(stamp), "%02d%02d%02d", t.wHour, t.wMinute, t.wSecond);
        std::string bin = g_dir + "IJ2FrameLab_" + label + "_" + stamp + ".bin";
        std::string txt = g_dir + "IJ2FrameLab_" + label + "_" + stamp + ".txt";
        FILE* f = fopen(bin.c_str(), "wb");
        if (f) { fwrite(buf.data(), 1, buf.size(), f); fclose(f); }
        FILE* g = fopen(txt.c_str(), "w");
        if (g) {
            fprintf(g, "base %llX\n", (unsigned long long)g_base);
            fputs(names.c_str(), g);
            fputs("events:\n", g);
            for (auto& e : events) fprintf(g, "%s\n", e.c_str());
            fclose(g);
        }
        Log("Recording written: %s (%zu bytes)", bin.c_str(), buf.size());
        std::lock_guard<std::mutex> l(g_recMtx);
        g_recLast = bin;
    }).detach();
}

static void RecordTick(const Snapshot& s) {
    std::lock_guard<std::mutex> l(g_recMtx);
    auto& b = g_recBuf;
    PutV(b, (uint32_t)0x4B434954); // 'TICK'
    PutV(b, (uint64_t)s.tick);
    LARGE_INTEGER q; QueryPerformanceCounter(&q);
    PutV(b, (uint64_t)q.QuadPart);
    for (int p = 0; p < 2; ++p) {
        const PlayerSnap& ps = s.p[p];
        PutV(b, (uint64_t)ps.info);
        PutV(b, (uint64_t)ps.obj);
        PutV(b, ps.pos);
        PutV(b, (uint32_t)ps.bones.size());
        for (auto& bs : ps.bones) { PutV(b, (int32_t)bs.nameIndex); PutV(b, bs.pos); }
        PutMem(b, ps.obj, REC_OBJ);
        PutMem(b, ps.info, REC_INFO);
        PutV(b, (uint64_t)ps.strikeList);
        PutMem(b, ps.strikeList, 0x400);
    }
    uintptr_t gi = GameInfo();
    PutV(b, (uint64_t)gi);
    PutMem(b, gi, REC_GI);
    PutV(b, (uint64_t)s.camPtr);
    PutV(b, s.camPos);
    PutV(b, s.camRot);
    PutV(b, s.camFov);
    PutMem(b, s.camPtr, REC_CAM);
    PutV(b, (uint32_t)s.live.size());
    for (auto& t : s.live) {
        PutV(b, (uint64_t)t.ptr);
        PutV(b, (uint64_t)t.vtable);
        PutV(b, (int32_t)t.kind);
        PutV(b, (uint64_t)t.createdTick);
        PutV(b, (uint64_t)t.callerRva);
        PutV(b, (uint64_t)t.arg1);
        PutV(b, (uint64_t)t.arg2);
        PutV(b, (uint64_t)t.arg3);
        PutMem(b, t.ptr, REC_COBJ);
    }
}

// ----------------------------------------------------------------------------- tick
void PollStep();
static void OnTick() {
    uint64_t tick = ++g_tick;
    Snapshot s;
    s.tick = tick;
    static uint32_t lastGf = 0;
    uint32_t gf = 0;
    if (uintptr_t gi = GameInfo()) gf = mem::ReadOr<uint32_t>(gi + 0xA0, 0);
    s.frame = gf;
    s.advanced = gf != lastGf;
    lastGf = gf;
    {   // game mode name, read through P1's info (only practice_mode unlocks the overlay)
        uintptr_t info0 = GetInfo(0);
        uintptr_t mo = info0 ? mem::ReadOr<uintptr_t>(info0 + 0x88, 0) : 0;   // mode object, name pointer at +0
        uintptr_t mp = mo > 0x10000 ? mem::ReadOr<uintptr_t>(mo, 0) : 0;
        char b[40] = {};
        if (mp > 0x10000 && mem::ReadRaw(mp, b, sizeof(b) - 1)) {
            int k = 0;
            while (k < 39 && b[k] > 32 && b[k] < 127) ++k;
            if (k >= 3 && b[k] == 0) s.gameMode.assign(b, k);
        }
    }
    for (int p = 0; p < 2; ++p) {
        PlayerSnap& ps = s.p[p];
        ps.info = GetInfo(p);
        ps.obj = ps.info ? GetObj(p) : 0;
        if (!ps.obj) continue;
        ps.valid = true;
        ((GetPosFn)g_sigs[S_GetPos].addr)(ps.obj, &ps.pos);
        if (g_sampleBones && g_sigs[S_GetBonePos].addr) {
            RefreshBones(p, ps.obj);
            ps.charKey = g_boneCache[p].charKey;
            {   // character identity: the character's asset name (skeletons are shared between characters)
                static uintptr_t lastInfo[2], lastStr[2];
                static std::string name[2];
                uintptr_t sp = mem::ReadOr<uintptr_t>(ps.info + 0xC0, 0);
                if (ps.info != lastInfo[p] || sp != lastStr[p]) {
                    lastInfo[p] = ps.info; lastStr[p] = sp;
                    char b[64] = {};
                    SIZE_T got = 0;
                    std::string n;
                    if (sp > 0x10000 && ReadProcessMemory(GetCurrentProcess(), (void*)sp, b, sizeof(b) - 1, &got)) {
                        int k = 0;
                        while (k < 63 && b[k] > 32 && b[k] < 127) ++k;
                        if (k >= 5 && b[k] == 0 && !strncmp(b, "char_", 5)) n.assign(b, k);
                    }
                    if (n != name[p]) { name[p] = n; Log("P%d: character %s", p + 1, n.empty() ? "?" : n.c_str()); }
                }
                ps.charName = name[p];
                {   // name of the special move in progress (the game keeps a pointer to the move's script entry)
                    uintptr_t e = mem::ReadOr<uintptr_t>(ps.obj + 0xDD0, 0);
                    uintptr_t np = e > 0x10000 ? mem::ReadOr<uintptr_t>(e, 0) : 0;
                    char b[64] = {};
                    if (np > 0x10000 && mem::ReadRaw(np, b, sizeof(b) - 1)) {
                        int k = 0;
                        while (k < 63 && b[k] > 32 && b[k] < 127) ++k;
                        if (k >= 4 && b[k] == 0 && strchr(b, '_')) ps.moveName.assign(b, k);
                    }
                }
                if (!name[p].empty()) {
                    uint32_t h = 2166136261u;
                    for (char ch : name[p]) { h ^= (uint8_t)ch; h *= 16777619u; }
                    ps.charKey = h;
                }
            }
            const BoneCache& c = g_boneCache[p];
            ps.bones.reserve(c.names.size());
            for (uint64_t fn : c.names) {
                Vec3 v{};
                ((GetBonePosFn)g_sigs[S_GetBonePos].addr)(ps.obj, fn, &v);
                ps.bones.push_back({(int)(fn & 0xFFFFFFFF), v});
            }
        }
    }
    uintptr_t cam = g_camera.load();
    if (cam) {
        s.camPtr = cam;
        s.camValid = mem::Read(cam + 0x584, s.camPos) && mem::Read(cam + 0x590, s.camRot) && mem::Read(cam + 0x59C, s.camFov);
    }
    {
        std::lock_guard<std::mutex> l(g_trackMtx);
        // drop dead objects, classify live ones
        std::vector<Tracked> keep;
        keep.reserve(g_tracked.size());
        for (auto& t : g_tracked) {
            uintptr_t vt = 0;
            if (!mem::Read(t.ptr, vt)) continue;
            int k = KindOfVtable(vt);
            if (k == OK_Unknown) {
                if (tick - t.createdTick > 2) continue; // vtable not one of ours (destroyed / reused)
            } else if (t.kind != k) {
                t.kind = k;
                char e[256];
                snprintf(e, sizeof(e), "tick %llu  new %-22s %llX  caller rva %llX  args %llX %llX %llX",
                         (unsigned long long)tick, KindName(k), (unsigned long long)t.ptr, (unsigned long long)t.callerRva,
                         (unsigned long long)t.arg1, (unsigned long long)t.arg2, (unsigned long long)t.arg3);
                AddEvent(e);
                if (k == OK_Melee) {
                    // attack spec: [[melee+0x370]+0x30]+0x18 ; +0x200 active start, +0x204 active end (seconds?)
                    uintptr_t o1 = mem::ReadOr<uintptr_t>(t.ptr + 0x370, 0);
                    uintptr_t o2 = o1 ? mem::ReadOr<uintptr_t>(o1 + 0x30, 0) : 0;
                    uintptr_t spec = o2 ? o2 + 0x18 : 0;
                    float f[8] = {};
                    if (spec) mem::ReadRaw(spec + 0x200, f, sizeof(f));
                    uintptr_t owner = mem::ReadOr<uintptr_t>(t.ptr + 0x330, 0);
                    if (g_logVerbose) Log("spec: melee %llX owner %llX spec %llX  200:%.4f 204:%.4f 208:%.4f 20C:%.4f 210:%.4f 214:%.4f 218:%.4f 21C:%.4f",
                        (unsigned long long)t.ptr, (unsigned long long)owner, (unsigned long long)spec, f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7]);
                }
                if (k == OK_Melee && g_armMelee) {
                    g_armMelee = false;
                    uintptr_t a[4] = {t.ptr + 0x310, t.ptr + 0x370, t.ptr + 0x380, t.ptr + 0x2F8};
                    const char* n[4] = {"melee+310 (ptr)", "melee+370 (arg1 ptr)", "melee+380 (state)", "melee+2F8"};
                    trace::Request(a, n, 2, "melee" + std::to_string(++g_meleeTraceN));
                }
                if (g_recording) { std::lock_guard<std::mutex> rl(g_recMtx); g_recEvents.push_back(e); }
            }
            t.vtable = vt;
            keep.push_back(t);
        }
        g_tracked.swap(keep);
        for (auto& t : g_tracked) if (t.kind != OK_Unknown) s.live.push_back(t);
    }
    ProjArmPrune(s);
    // per-fighter collision data
    for (int p = 0; p < 2; ++p) {
        PlayerSnap& ps = s.p[p];
        if (!ps.obj) continue;
        ps.stateFlags = mem::ReadOr<uint32_t>(ps.obj + 0xA34, 0);
        ps.moveId = mem::ReadOr<int32_t>(ps.info + 0x2B8, -1);
        for (auto& t : s.live) {
            if (t.kind == OK_PlayerCollisionInfo && mem::ReadOr<uintptr_t>(t.ptr + 0x28, 0) == ps.obj) ps.pci = t.ptr;
            if (t.kind == OK_Melee && mem::ReadOr<uintptr_t>(t.ptr + 0x330, 0) == ps.obj) {
                if (!ps.melee || t.createdTick >= 0) { ps.melee = t.ptr; ps.meleeState = mem::ReadOr<int>(t.ptr + 0x380, -1); }
            }
            if ((t.kind == OK_Melee || t.kind == OK_CollisionObj || t.kind == OK_Projectile) && mem::ReadOr<uintptr_t>(t.ptr + 0x330, 0) == ps.obj)
                ps.hitObjs.push_back({t.ptr, ProjState(t, mem::ReadOr<int>(t.ptr + 0x380, -1), s.tick, s.advanced)});
        }
        if (!ps.pci) ps.pci = FindPciByScan(ps.obj, ps.info);   // e.g. after a hot reload: its constructor ran before we loaded
        if (!ps.pci) continue;
        struct Raw { int id, b, bone, part; float x, y, z, r; };
        // hurt spheres used by the hit test
        int n = mem::ReadOr<int>(ps.pci + 0x40, 0);
        uintptr_t arr = mem::ReadOr<uintptr_t>(ps.pci + 0x48, 0);
        if (n > 0 && n <= 64 && arr) {
            Raw raw[64];
            if (mem::ReadRaw(arr, raw, sizeof(Raw) * n))
                for (int i = 0; i < n; ++i)
                    ps.hurt.push_back({raw[i].id, raw[i].bone, raw[i].part, {raw[i].x, raw[i].y, raw[i].z},
                                       HurtRadius(ps.obj, (const uint8_t*)&raw[i], raw[i].r)});
        }
        g_pciOf[p] = ps.pci;
        g_objOf[p] = ps.obj;
        ps.strikeList = mem::ReadOr<uintptr_t>(ps.pci + 0x38, 0);
        {
            // Hitboxes tested during the last simulated frame. Kept while the game is frozen
            // or slowed (no new simulation frame), replaced/cleared when the next frame runs.
            static std::vector<HurtPoint> shown[2];
            std::lock_guard<std::mutex> l(g_capMtx);
            if (s.advanced) {
                if (g_cap[p].seq > g_capSeen[p]) { shown[p] = g_cap[p].pts; g_capSeen[p] = g_cap[p].seq; }
                else shown[p].clear();
            }
            ps.strike = shown[p];
            ps.striking = !shown[p].empty();
        }
    }

    // ---- non-melee attack objects (projectiles, lasso, thrown weapons): owner + lifecycle ----
    {
        static int logBudget = 4000;
        auto LOGOK = [&]() { return g_logVerbose && logBudget-- > 0; };
        struct ObjInfo { int owner = -1; int ownerOff = -1; int lastState = -99; uint64_t born = 0; int kind = 0; uintptr_t vt = 0; };
        static std::map<uintptr_t, ObjInfo> seen;
        static std::map<uintptr_t, int> ownerOffByVt;   // vtable -> offset of owner pointer (-2 = none found)
        std::map<uintptr_t, ObjInfo> now;
        for (auto& t : s.live) {
            if (t.kind != OK_Projectile && t.kind != OK_CollisionObj && t.kind != OK_Resolution) continue;
            ObjInfo oi;
            auto it = seen.find(t.ptr);
            if (it != seen.end() && it->second.vt == t.vtable) oi = it->second;
            else {
                oi.born = s.frame; oi.kind = t.kind; oi.vt = t.vtable;
                // find which player this object belongs to: scan for a pointer to a fighter object / info
                std::string offs;
                uint8_t buf[0x800];
                if (mem::ReadRaw(t.ptr, buf, sizeof(buf))) {
                    for (int o = 8; o < (int)sizeof(buf); o += 8) {
                        uintptr_t v; memcpy(&v, buf + o, 8);
                        for (int p = 0; p < 2; ++p) {
                            if (!v) continue;
                            bool isObj = v == s.p[p].obj, isInfo = v == s.p[p].info;
                            if (isObj || isInfo) {
                                char tmp[32]; snprintf(tmp, sizeof(tmp), " P%d%s@%X", p + 1, isObj ? "obj" : "info", o);
                                offs += tmp;
                                if (oi.owner < 0) { oi.owner = p; oi.ownerOff = o; }
                            }
                        }
                    }
                }
                if (LOGOK()) Log("obj+ %s %llX vt %llX f%llu P1flags %X state %d type %d r %.2f owners:%s", KindName(t.kind), (unsigned long long)t.ptr,
                    (unsigned long long)(t.vtable - g_base), (unsigned long long)s.frame, s.p[0].stateFlags,
                    mem::ReadOr<int>(t.ptr + 0x380, -1), mem::ReadOr<int>(t.ptr + 0x38C, 0), mem::ReadOr<float>(t.ptr + 0x3EC, 0), offs.c_str());
            }
            if (t.kind == OK_Resolution) {
                // a collision resolution object appears on the frame an attack connects
                if (it == seen.end() || it->second.vt != t.vtable) {
                    for (int p = 0; p < 2; ++p) s.p[p].contactNew = true;
                    if (LOGOK()) Log("contact f%llu P1flags %X P2flags %X", (unsigned long long)s.frame, s.p[0].stateFlags, s.p[1].stateFlags);
                }
                now[t.ptr] = oi;
                continue;
            }
            int st = mem::ReadOr<int>(t.ptr + 0x380, -1);
            if (st != oi.lastState && it != seen.end() && LOGOK())
                if (g_logVerbose) Log("obj~ %llX f%llu state %d -> %d (age %llu)", (unsigned long long)t.ptr, (unsigned long long)s.frame, oi.lastState, st,
                    (unsigned long long)(s.frame - oi.born));
            oi.lastState = st;
            now[t.ptr] = oi;
            if (oi.owner >= 0 && oi.owner < 2) {
                PlayerSnap& ps = s.p[oi.owner];
                ps.projAlive = true;
                if (st == 1) ps.projActive = true;   // only while its hitbox is live (1); 4 = already connected
            }
        }
        for (auto& kv : seen)
            if (!now.count(kv.first) && LOGOK())
                if (g_logVerbose) Log("obj- %llX f%llu age %llu", (unsigned long long)kv.first, (unsigned long long)s.frame, (unsigned long long)(s.frame - kv.second.born));
        seen.swap(now);
        (void)ownerOffByVt;
    }
    SampleShapes(s);
    if (s.advanced) ibuf::OnTick(gf, s.p[0].valid, s.p[0].stateFlags, s.p[0].obj, false);

    if (g_recording) {
        RecordTick(s);
        if (--g_recLeft <= 0) FinishRecording();
    }
    if (s.advanced) {
        bot::OnSimFrame(s);   // scripted input runs on the game thread, exactly once per simulated frame
        std::lock_guard<std::mutex> l(g_snapMtx);
        g_frameQ.push_back(s);
        while (g_frameQ.size() > 60) g_frameQ.pop_front();
    }
    {
        std::lock_guard<std::mutex> l(g_snapMtx);
        g_snap = std::move(s);
    }
    // frame step: let exactly one tick through, then freeze again
    PollStep();
}

static void h_ProcDispatch() {
    o_ProcDispatch();
    OnTick();
}

static void h_Dispatch(uintptr_t ptr, int a2) {
    // Ported from I2Hook's Dispatch_Hook "Freeze World" (ermaccer, plugin/Hooks.cpp)
    int v = g_frozen ? a2 : mem::ReadOr<int>(ptr + 0x18, a2);
    int cur = mem::ReadOr<int>(ptr + 0x14, v);
    if (cur == v) return;
    *(int*)(ptr + 0x14) = v;
    uintptr_t vt = *(uintptr_t*)ptr;
    ((DispatchFn)(*(uintptr_t*)(vt + 0xF8)))(ptr, v);
}

// This function is a generic "set location" that other actors call too (e.g. Swamp Thing's train).
// Only follow objects whose C++ class name contains "Camera".
static std::mutex g_camVtMtx;
static std::map<uintptr_t, bool> g_camVtOk;
static bool IsCameraClass(uintptr_t obj) {
    uintptr_t vt = mem::ReadOr<uintptr_t>(obj, 0);
    if (!vt) return false;
    std::lock_guard<std::mutex> l(g_camVtMtx);
    auto it = g_camVtOk.find(vt);
    if (it != g_camVtOk.end()) return it->second;
    char name[96] = {};
    uintptr_t col = mem::ReadOr<uintptr_t>(vt - 8, 0);
    if (col && mem::ReadOr<uint32_t>(col, 0) == 1) {
        uint32_t td = mem::ReadOr<uint32_t>(col + 12, 0);
        if (td && td < mem::ModuleSize()) mem::ReadRaw(g_base + td + 0x10, name, sizeof(name) - 1);
    }
    bool ok = strcmp(name, ".?AVAMKCamera@@") == 0;   // the fight camera (its view fields are at +0x584..)
    g_camVtOk[vt] = ok;
    Log("SetPosition caller class '%s' -> %s", name, ok ? "camera" : "ignored");
    return ok;
}

static void h_CamSetPos(uintptr_t cam, Vec3* pos) {
    if (IsCameraClass(cam)) g_camera = cam;
    o_CamSetPos(cam, pos);
}

static std::atomic<uint32_t> g_stepFrom{0};
static uint32_t SimFrame() {
    uintptr_t gi = GameInfo();
    return gi ? mem::ReadOr<uint32_t>(gi + 0xA0, 0) : 0;
}
void SetFrozen(bool f) { g_stepPending = 0; g_frozen = f; }
bool Frozen() { return g_frozen; }
// Unfreeze until exactly one new simulation frame has run, then freeze again.
void StepOnce() { g_stepFrom = SimFrame(); g_stepPending = 1; g_frozen = false; }
void PollStep() {
    if (g_stepPending.load() > 0 && SimFrame() != g_stepFrom.load()) { g_stepPending = 0; g_frozen = true; }
}

std::vector<std::string> RecentEvents() {
    std::lock_guard<std::mutex> l(g_trackMtx);
    return std::vector<std::string>(g_events.begin(), g_events.end());
}

bool Ready() { return g_ready; }
std::string Status() { return g_status; }

// ----------------------------------------------------------------------------- init
static bool VerifyVtable(VtDef& v) {
    uintptr_t va = g_base + v.rva;
    uintptr_t col = mem::ReadOr<uintptr_t>(va - 8, 0);
    if (!col) return false;
    uint32_t tdRva = mem::ReadOr<uint32_t>(col + 12, 0);
    char name[64] = {};
    if (!mem::ReadRaw(g_base + tdRva + 0x10, name, sizeof(name) - 1)) return false;
    if (strcmp(name, v.rtti) != 0) return false;
    v.va = va;
    return true;
}

static bool HookAt(uintptr_t target, void* detour, void** orig, const char* name) {
    if (!target) { Log("hook %s: target missing", name); return false; }
    MH_STATUS st = MH_CreateHook((void*)target, detour, orig);
    if (st != MH_OK) { Log("hook %s: MH_CreateHook failed %d", name, (int)st); return false; }
    st = MH_EnableHook((void*)target);
    if (st != MH_OK) { Log("hook %s: MH_EnableHook failed %d", name, (int)st); return false; }
    Log("hook %s at rva %llX", name, (unsigned long long)(target - g_base));
    return true;
}

bool Init() {
    g_base = mem::ModuleBase();
    char path[MAX_PATH];
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&Init, &self);
    GetModuleFileNameA(self, path, MAX_PATH);
    g_dir = path;
    g_dir = g_dir.substr(0, g_dir.find_last_of("\\/") + 1);

    int found = 0;
    std::string missing;
    for (auto& s : g_sigs) {
        uintptr_t m = mem::FindPattern(s.pat);
        if (m && s.rip) {
            int32_t d = mem::ReadOr<int32_t>(m + s.off, 0);
            m = m + s.off + 4 + d;
        } else if (m) {
            m += s.off;
        }
        s.addr = m;
        if (m) found++; else missing += std::string(" ") + s.name;
        Log("sig %-36s %s rva %llX", s.name, m ? "OK " : "MISSING", (unsigned long long)(m ? m - g_base : 0));
    }
    g_pGameInfo = g_sigs[S_FindGameInfo].addr;
    if (g_sigs[S_GetInfo].addr) g_infoTableOff = mem::ReadOr<int32_t>(g_sigs[S_GetInfo].addr + 12, 0);
    g_pNames = g_sigs[S_GetNames].addr;

    int vtOk = 0;
    for (auto& v : g_vts) {
        bool ok = VerifyVtable(v);
        vtOk += ok;
        Log("vtable %-28s %s", v.rtti, ok ? "OK" : "MISMATCH");
    }

    InitExact();
    if (MH_Initialize() != MH_OK) { Log("MH_Initialize failed (already initialised?)"); }
    bool core = g_sigs[S_MKProcDispatch].addr && g_pGameInfo && g_sigs[S_GetInfo].addr && g_sigs[S_GetObj].addr && g_sigs[S_GetPos].addr;
    if (core) {
        HookAt(g_sigs[S_MKProcDispatch].addr, (void*)h_ProcDispatch, (void**)&o_ProcDispatch, "MKProcDispatch");
        HookAt(g_sigs[S_Dispatch].addr, (void*)h_Dispatch, (void**)&o_Dispatch, "Dispatch");
        HookAt(g_sigs[S_CamSetPos].addr, (void*)h_CamSetPos, (void**)&o_CamSetPos, "MKCamera::SetPosition");
        HookAt(g_sigs[S_StrikeTest].addr, (void*)h_StrikeTest, (void**)&o_StrikeTest, "StrikeVsHurtTest");
        const int ctorSig[6] = {S_CollCtor0, S_CollCtor1, S_MeleeCtor, S_ProjCtor, S_PCICtor, S_ResCtor};
        if (vtOk == (int)(sizeof(g_vts) / sizeof(g_vts[0]))) {
            for (int i = 0; i < 6; ++i)
                HookAt(g_sigs[ctorSig[i]].addr, (void*)g_ctorHooks[i], (void**)&o_Ctor[i], g_sigs[ctorSig[i]].name);
        } else {
            Log("vtable verification failed - collision object tracking disabled");
        }
    }
    if (core) { ibuf::Init(); dib::Init(); }
    char st[256];
    snprintf(st, sizeof(st), "IJ2: %d/%d signatures, %d/5 vtables%s%s", found, (int)S_COUNT, vtOk,
             missing.empty() ? "" : ", missing:", missing.c_str());
    g_status = st;
    g_ready = core;
    Log("%s", st);
    return core;
}

} // namespace ij2
