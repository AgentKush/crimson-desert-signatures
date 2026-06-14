// cdsig_probe.cpp — diagnostic + live capture ASI for Crimson Desert.
//
// On injection (per process) it writes %TEMP%\cdsig_<exe>_<pid>.log with the host exe and the
// in-memory section table. In the GAME process it ALSO runs a command channel so you can probe
// live memory WITHOUT recompiling/relaunching:
//
//   Write a command line to   %TEMP%\cdsig_cmd.txt
//   Read the answer from       %TEMP%\cdsig_result.txt
//   (the probe consumes/deletes cmd.txt after each command)
//
// Commands (all READ-ONLY — no writes to game memory, no debugger, no anti-tamper poking):
//   sig <AOB>            scan executable sections; report match count + first address/RVA
//   findf <val> [tol]    fresh scan of writable heap for a float ~= val (CE "first scan")
//   nextf <val> [tol]    filter previous findf hits to those now ~= val (CE "next scan")
//   peekf <hexaddr>      read the float/int at an address
//   dump <hexaddr> <n>   hex-dump n bytes at an address (n<=256)
//
// Single-player interoperability tool. Reads the game's own memory; does not modify it.
//
// BUILD (VS Build Tools): x64 Native Tools Command Prompt, from this folder:
//   cl /std:c++17 /EHsc /LD /O2 cdsig_probe.cpp /Fe:cdsig_probe.asi
//
#include "../include/cdsig.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

static std::string g_tmp, g_cmd, g_res;
static std::vector<uintptr_t> g_hits;   // last findf/nextf result set

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
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
}
static bool readable(uintptr_t a, size_t len) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery((void*)a, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT) return false;
    DWORD p = mbi.Protect & 0xFF;
    if (mbi.Protect & PAGE_GUARD) return false;
    bool ok = (p==PAGE_READONLY||p==PAGE_READWRITE||p==PAGE_WRITECOPY||
               p==PAGE_EXECUTE_READ||p==PAGE_EXECUTE_READWRITE||p==PAGE_EXECUTE_WRITECOPY);
    if (!ok) return false;
    return (a + len) <= ((uintptr_t)mbi.BaseAddress + mbi.RegionSize);
}

static std::string hex(uintptr_t v) { char b[32]; snprintf(b, sizeof(b), "0x%llX", (unsigned long long)v); return b; }

static std::string cmdSig(const std::string& pat) {
    HMODULE g = GetModuleHandleW(L"CrimsonDesert.exe");
    uintptr_t base = (uintptr_t)g;
    size_t n = cdsig::scanCount(pat, g);
    std::string out = "sig matches=" + std::to_string(n);
    if (auto a = cdsig::scan(pat, g)) out += " first=" + hex(*a) + " rva=" + hex(*a - base);
    out += (n == 1) ? "  [UNIQUE]" : (n == 0 ? "  [none]" : "  [ambiguous - lengthen]");
    return out;
}
static std::string cmdFindf(float val, float tol, bool fresh) {
    std::vector<uintptr_t> hits;
    if (fresh) {
        MEMORY_BASIC_INFORMATION mbi{}; uintptr_t a = 0;
        while (VirtualQuery((void*)a, &mbi, sizeof(mbi))) {
            uintptr_t next = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
            DWORD p = mbi.Protect & 0xFF;
            if (mbi.State==MEM_COMMIT && mbi.Type==MEM_PRIVATE && !(mbi.Protect&PAGE_GUARD) &&
                (p==PAGE_READWRITE||p==PAGE_WRITECOPY)) {
                float* f = (float*)mbi.BaseAddress; size_t cnt = mbi.RegionSize / 4;
                for (size_t i = 0; i < cnt; i++) {
                    float v = f[i];
                    if (v==v && fabsf(v - val) <= tol) { hits.push_back((uintptr_t)(f + i)); if (hits.size() >= 200000) break; }
                }
            }
            if (next <= a) break; a = next;
            if (hits.size() >= 200000) break;
        }
    } else {
        for (uintptr_t h : g_hits) if (readable(h, 4) && fabsf(*(float*)h - val) <= tol) hits.push_back(h);
    }
    g_hits = hits;
    std::string out = (fresh ? "findf" : "nextf");
    out += " val=" + std::to_string(val) + " tol=" + std::to_string(tol) + " hits=" + std::to_string(hits.size());
    size_t show = hits.size() < 40 ? hits.size() : 40;
    for (size_t i = 0; i < show; i++) out += "\n  " + hex(hits[i]);
    if (hits.size() > show) out += "\n  ...";
    return out;
}
static std::string cmdPeekf(uintptr_t a) {
    if (!readable(a, 4)) return "peekf " + hex(a) + "  [unreadable]";
    float f = *(float*)a; int i = *(int*)a;
    char b[96]; snprintf(b, sizeof(b), "peekf %s  float=%.4f  int=%d", hex(a).c_str(), f, i);
    return b;
}
static std::string cmdDump(uintptr_t a, size_t n) {
    if (n > 256) n = 256;
    if (!readable(a, n)) return "dump " + hex(a) + "  [unreadable]";
    std::string out = "dump " + hex(a) + " (" + std::to_string(n) + "):\n";
    unsigned char* p = (unsigned char*)a;
    char b[8];
    for (size_t i = 0; i < n; i++) { snprintf(b, sizeof(b), "%02X ", p[i]); out += b; if ((i & 15) == 15) out += "\n"; }
    return out;
}

static std::string handle(const std::string& line) {
    std::string s = trim(line); if (s.empty()) return "";
    size_t sp = s.find(' ');
    std::string cmd = s.substr(0, sp), rest = (sp==std::string::npos) ? "" : trim(s.substr(sp+1));
    try {
        if (cmd == "sig")   return cmdSig(rest);
        if (cmd == "peekf") return cmdPeekf(std::stoull(rest, 0, 16));
        if (cmd == "dump")  { size_t sp2 = rest.find(' '); return cmdDump(std::stoull(rest.substr(0,sp2),0,16), sp2==std::string::npos?32:std::stoul(rest.substr(sp2+1))); }
        if (cmd == "findf" || cmd == "nextf") {
            size_t sp2 = rest.find(' ');
            float val = std::stof(sp2==std::string::npos?rest:rest.substr(0,sp2));
            float tol = sp2==std::string::npos ? 0.5f : std::stof(rest.substr(sp2+1));
            return cmdFindf(val, tol, cmd=="findf");
        }
    } catch (...) { return "error parsing: " + s; }
    return "unknown cmd: " + cmd;
}

static FILE* g_log = nullptr;
static void logln(const char* fmt, ...) {
    char buf[1024]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    printf("%s\n", buf); if (g_log) { fprintf(g_log, "%s\n", buf); fflush(g_log); }
}

static DWORD WINAPI Run(LPVOID) {
    char exePath[MAX_PATH] = {0}; GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    const char* exe = strrchr(exePath, '\\'); exe = exe ? exe + 1 : exePath;
    DWORD pid = GetCurrentProcessId();
    char tmp[MAX_PATH] = {0}; GetTempPathA(MAX_PATH, tmp); g_tmp = tmp;
    g_cmd = g_tmp + "cdsig_cmd.txt"; g_res = g_tmp + "cdsig_result.txt";
    char lp[MAX_PATH]; snprintf(lp, sizeof(lp), "%scdsig_%s_%lu.log", tmp, exe, pid);

    AllocConsole(); FILE* c = nullptr; freopen_s(&c, "CONOUT$", "w", stdout);
    fopen_s(&g_log, lp, "w");

    bool isGame = (GetModuleHandleW(L"CrimsonDesert.exe") != nullptr);
    HMODULE self = GetModuleHandleW(nullptr); uintptr_t base = (uintptr_t)self;
    logln("[cdsig] host exe = %s (pid %lu)%s", exe, pid, isGame ? "  <-- GAME" : "");
    logln("[cdsig] module base = %p", (void*)base);
    for (auto& s : cdsig::sections(self)) {
        bool x=(s.characteristics&IMAGE_SCN_MEM_EXECUTE)!=0, w=(s.characteristics&IMAGE_SCN_MEM_WRITE)!=0;
        logln("   %s%s %-10s %p %14zu 0x%08X", x?"X":" ", w?"W":" ", s.name.c_str(), (void*)s.start, s.size, s.characteristics);
    }
    if (!isGame) { logln("[cdsig] non-game process; no command channel."); if (g_log){fclose(g_log);g_log=nullptr;} return 0; }

    logln("[cdsig] command channel ready: write %s , read %s", g_cmd.c_str(), g_res.c_str());
    spit(g_res, "ready");
    for (;;) {
        Sleep(300);
        std::string line = slurp(g_cmd);
        if (trim(line).empty()) continue;
        DeleteFileA(g_cmd.c_str());
        std::string out = handle(line);
        logln("[cmd] %s\n%s", trim(line).c_str(), out.c_str());
        spit(g_res, out);
    }
}

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); CreateThread(nullptr,0,Run,nullptr,0,nullptr); }
    return TRUE;
}
