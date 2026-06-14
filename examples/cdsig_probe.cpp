// cdsig_probe.cpp — diagnostic + live capture ASI for Crimson Desert.
//
// Per process: writes %TEMP%\cdsig_<exe>_<pid>.log (host exe + in-memory section table).
// In the GAME process: runs a file-driven command channel so you can probe live memory with NO
// recompiles/relaunches.  Write a line to %TEMP%\cdsig_cmd.txt , read %TEMP%\cdsig_result.txt.
//
// Commands (READ-ONLY — no writes to game memory, no debugger):
//   sig <AOB>          scan executable sections; report match count + first addr/RVA
//   findf <val> [tol]  fresh float scan of writable heap (CE "first scan")
//   nextf <val> [tol]  keep prior hits ~= val
//   decf / incf        keep prior hits whose value went DOWN / UP since last scan
//   changedf / samef   keep prior hits that CHANGED / stayed the SAME
//   peekf <hexaddr>    read float/int at an address
//   dump <hexaddr> <n> hex-dump n bytes (n<=256)
//
// Typical capture (stamina regenerates, so use change-scans, not exact values):
//   findf 140   -> sprint to drain -> decf -> stop to regen -> incf -> repeat -> 1 address.
//
// Single-player interoperability tool. Reads the game's own memory; never modifies it.
// BUILD: x64 Native Tools Command Prompt, here:
//   cl /std:c++17 /EHsc /O2 /LD cdsig_probe.cpp /Fe:cdsig_probe.asi
//
#include "../include/cdsig.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

static std::string g_tmp, g_cmd, g_res;
static std::vector<uintptr_t> g_hits;
static std::vector<float>     g_vals;   // last-seen value per hit (parallel to g_hits)

static std::string slurp(const std::string& p) {
    FILE* f = nullptr; if (fopen_s(&f, p.c_str(), "rb") || !f) return "";
    std::string s; char b[4096]; size_t n;
    while ((n = fread(b, 1, sizeof(b), f)) > 0) s.append(b, n);
    fclose(f); return s;
}
static void spit(const std::string& p, const std::string& s) {
    FILE* f = nullptr; if (fopen_s(&f, p.c_str(), "wb") || !f) return;
    fwrite(s.data(), 1, s.size(), f); fclose(f);
}
static std::string trim(std::string s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
}
static std::string hex(uintptr_t v) { char b[32]; snprintf(b, sizeof(b), "0x%llX", (unsigned long long)v); return b; }

static bool readable(uintptr_t a, size_t len) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery((void*)a, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD)) return false;
    DWORD p = mbi.Protect & 0xFF;
    bool ok = (p==PAGE_READONLY||p==PAGE_READWRITE||p==PAGE_WRITECOPY||
               p==PAGE_EXECUTE_READ||p==PAGE_EXECUTE_READWRITE||p==PAGE_EXECUTE_WRITECOPY);
    return ok && (a + len) <= ((uintptr_t)mbi.BaseAddress + mbi.RegionSize);
}
// SEH-guarded primitives (POD-only locals so __try is legal).
static bool readF(uintptr_t a, float& out) {
    __try { out = *(const float*)a; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static bool scanRegionFloat(const float* p, size_t count, float val, float tol,
                            std::vector<uintptr_t>& out, size_t cap) {
    __try {
        for (size_t i = 0; i < count; i++) {
            float v = p[i];
            if (v == v && fabsf(v - val) <= tol) { out.push_back((uintptr_t)(p + i)); if (out.size() >= cap) return true; }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

static void refreshVals() {
    g_vals.assign(g_hits.size(), 0.f);
    for (size_t i = 0; i < g_hits.size(); i++) { float v; if (readF(g_hits[i], v)) g_vals[i] = v; }
}
static std::string report(const std::string& head) {
    std::string out = head;
    size_t show = g_hits.size() < 40 ? g_hits.size() : 40;
    char b[80];
    for (size_t i = 0; i < show; i++) { snprintf(b, sizeof(b), "  %s = %.3f", hex(g_hits[i]).c_str(), (i<g_vals.size()?g_vals[i]:0.f)); out += "\n"; out += b; }
    if (g_hits.size() > show) out += "\n  ...(" + std::to_string(g_hits.size()) + " total)";
    return out;
}

static std::string cmdSig(const std::string& pat) {
    HMODULE g = GetModuleHandleW(L"CrimsonDesert.exe"); uintptr_t base = (uintptr_t)g;
    size_t n = cdsig::scanCount(pat, g);
    std::string out = "sig matches=" + std::to_string(n);
    if (auto a = cdsig::scan(pat, g)) out += " first=" + hex(*a) + " rva=" + hex(*a - base);
    out += (n == 1) ? "  [UNIQUE]" : (n == 0 ? "  [none]" : "  [ambiguous - lengthen]");
    return out;
}
static std::string cmdFindf(float val, float tol, bool fresh) {
    std::vector<uintptr_t> hits; const size_t CAP = 60000;
    unsigned long long scanned = 0; const unsigned long long MAXSCAN = 3ULL*1024*1024*1024;
    if (fresh) {
        MEMORY_BASIC_INFORMATION mbi{}; uintptr_t a = 0;
        while (VirtualQuery((void*)a, &mbi, sizeof(mbi))) {
            uintptr_t next = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
            DWORD p = mbi.Protect & 0xFF;
            if (mbi.State==MEM_COMMIT && mbi.Type==MEM_PRIVATE && !(mbi.Protect&PAGE_GUARD) &&
                (p==PAGE_READWRITE||p==PAGE_WRITECOPY)) {
                scanRegionFloat((const float*)mbi.BaseAddress, mbi.RegionSize/4, val, tol, hits, CAP);
                scanned += mbi.RegionSize;
            }
            if (hits.size() >= CAP || scanned >= MAXSCAN) break;
            if (next <= a) break; a = next;
        }
    } else {
        for (uintptr_t h : g_hits) { float v; if (readable(h,4) && readF(h,v) && fabsf(v-val)<=tol) hits.push_back(h); }
    }
    g_hits = hits; refreshVals();
    char hd[128]; snprintf(hd, sizeof(hd), "%s val=%.3f tol=%.3f scanned=%lluMB hits=%zu",
                           fresh?"findf":"nextf", val, tol, scanned/(1024*1024), g_hits.size());
    return report(hd);
}
static std::string cmdFilter(const std::string& mode) {
    std::vector<uintptr_t> nh; std::vector<float> nv;
    for (size_t i = 0; i < g_hits.size(); i++) {
        uintptr_t h = g_hits[i]; if (!readable(h,4)) continue;
        float cur; if (!readF(h, cur)) continue;
        float old = (i < g_vals.size()) ? g_vals[i] : cur; bool keep = false;
        if (mode=="decf") keep = cur < old-0.001f;
        else if (mode=="incf") keep = cur > old+0.001f;
        else if (mode=="changedf") keep = fabsf(cur-old) > 0.001f;
        else if (mode=="samef") keep = fabsf(cur-old) <= 0.001f;
        if (keep) { nh.push_back(h); nv.push_back(cur); }
    }
    g_hits = nh; g_vals = nv;
    return report(mode + " hits=" + std::to_string(g_hits.size()));
}
static std::string cmdPeekf(uintptr_t a) {
    float f; if (!readable(a,4) || !readF(a,f)) return "peekf " + hex(a) + "  [unreadable]";
    int i = *(int*)&f; char b[96]; snprintf(b, sizeof(b), "peekf %s  float=%.4f  int=%d", hex(a).c_str(), f, i); return b;
}
static std::string cmdDump(uintptr_t a, size_t n) {
    if (n > 256) n = 256;
    if (!readable(a, n)) return "dump " + hex(a) + "  [unreadable]";
    std::string out = "dump " + hex(a) + " (" + std::to_string(n) + "):\n";
    unsigned char* p = (unsigned char*)a; char b[8];
    for (size_t i = 0; i < n; i++) { snprintf(b, sizeof(b), "%02X ", p[i]); out += b; if ((i&15)==15) out += "\n"; }
    return out;
}

static std::string handle(const std::string& line) {
    std::string s = trim(line); if (s.empty()) return "";
    size_t sp = s.find(' ');
    std::string cmd = s.substr(0, sp), rest = (sp==std::string::npos) ? "" : trim(s.substr(sp+1));
    try {
        if (cmd=="sig")   return cmdSig(rest);
        if (cmd=="peekf") return cmdPeekf(std::stoull(rest,0,16));
        if (cmd=="dump")  { size_t s2 = rest.find(' '); return cmdDump(std::stoull(rest.substr(0,s2),0,16), s2==std::string::npos?32:std::stoul(rest.substr(s2+1))); }
        if (cmd=="findf"||cmd=="nextf") { size_t s2 = rest.find(' '); float val = std::stof(s2==std::string::npos?rest:rest.substr(0,s2)); float tol = s2==std::string::npos?0.5f:std::stof(rest.substr(s2+1)); return cmdFindf(val,tol,cmd=="findf"); }
        if (cmd=="decf"||cmd=="incf"||cmd=="changedf"||cmd=="samef") return cmdFilter(cmd);
    } catch (...) { return "error parsing: " + s; }
    return "unknown cmd: " + cmd;
}

static FILE* g_log = nullptr;
static void logln(const char* fmt, ...) {
    char buf[1024]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    printf("%s\n", buf); if (g_log) { fprintf(g_log, "%s\n", buf); fflush(g_log); }
}

static DWORD WINAPI Run(LPVOID) {
    char exePath[MAX_PATH]={0}; GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    const char* exe = strrchr(exePath, '\\'); exe = exe ? exe+1 : exePath;
    DWORD pid = GetCurrentProcessId();
    char tmp[MAX_PATH]={0}; GetTempPathA(MAX_PATH, tmp); g_tmp = tmp;
    g_cmd = g_tmp + "cdsig_cmd.txt"; g_res = g_tmp + "cdsig_result.txt";
    char lp[MAX_PATH]; snprintf(lp, sizeof(lp), "%scdsig_%s_%lu.log", tmp, exe, pid);
    AllocConsole(); FILE* c=nullptr; freopen_s(&c, "CONOUT$", "w", stdout);
    fopen_s(&g_log, lp, "w");

    bool isGame = (GetModuleHandleW(L"CrimsonDesert.exe") != nullptr);
    HMODULE self = GetModuleHandleW(nullptr);
    logln("[cdsig] host exe = %s (pid %lu)%s", exe, pid, isGame ? "  <-- GAME" : "");
    for (auto& s : cdsig::sections(self)) {
        bool x=(s.characteristics&IMAGE_SCN_MEM_EXECUTE)!=0, w=(s.characteristics&IMAGE_SCN_MEM_WRITE)!=0;
        logln("   %s%s %-10s %p %14zu", x?"X":" ", w?"W":" ", s.name.c_str(), (void*)s.start, s.size);
    }
    if (!isGame) { if (g_log){fclose(g_log);g_log=nullptr;} return 0; }
    logln("[cdsig] command channel ready (cmd=%s)", g_cmd.c_str());
    spit(g_res, "ready");
    for (;;) {
        Sleep(300);
        std::string line = slurp(g_cmd);
        if (trim(line).empty()) continue;
        DeleteFileA(g_cmd.c_str());
        std::string out = handle(line);
        logln("[cmd] %s -> %.200s", trim(line).c_str(), out.c_str());
        spit(g_res, out);
    }
}

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); CreateThread(nullptr,0,Run,nullptr,0,nullptr); }
    return TRUE;
}
