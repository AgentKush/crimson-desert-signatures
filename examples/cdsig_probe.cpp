// cdsig_probe.cpp — a minimal diagnostic ASI plugin: the instrument for capturing signatures
// from the LIVE Crimson Desert process.
//
// On injection it writes a PER-PROCESS log to %TEMP%\cdsig_<exe>_<pid>.log containing the host
// exe, the module load base, and the IN-MEMORY section table. Per-process naming matters because
// an ASI loader injects into every bin64 process that imports the proxy DLL (the game AND its
// crashpad_handler/launcher) — separate logs stop them clobbering each other.
//
// It does NOT modify the game. It reads the game's own already-running memory, the same way
// every camera/QoL ASI mod does. Single-player use only.
//
// BUILD (Visual Studio Build Tools are installed on this machine):
//   x64 Native Tools Command Prompt, from this folder:
//     cl /std:c++17 /EHsc /LD /O2 cdsig_probe.cpp /Fe:cdsig_probe.asi
//
#include "../include/cdsig.hpp"
#include <cstdio>
#include <cstdarg>
#include <cstring>

// Put a candidate signature here to test it live. Leave empty to just dump sections.
static const char* TEST_SIGNATURE = "";
// Delay before dumping (ms). 0 = at process attach. Increase if you need code fully decrypted
// before a scan (anti-tamper decrypts .xcode/.sbss shortly after start).
static const DWORD DUMP_DELAY_MS = 0;

static FILE* g_log = nullptr;
static void logln(const char* fmt, ...) {
    char buf[1024];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    printf("%s\n", buf);
    if (g_log) { fprintf(g_log, "%s\n", buf); fflush(g_log); }
}

static DWORD WINAPI Run(LPVOID) {
    if (DUMP_DELAY_MS) Sleep(DUMP_DELAY_MS);

    char exePath[MAX_PATH] = {0};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    const char* exe = strrchr(exePath, '\\');
    exe = exe ? exe + 1 : exePath;
    DWORD pid = GetCurrentProcessId();

    char tmp[MAX_PATH] = {0}; GetTempPathA(MAX_PATH, tmp);
    char logpath[MAX_PATH] = {0};
    snprintf(logpath, sizeof(logpath), "%scdsig_%s_%lu.log", tmp, exe, pid);

    AllocConsole();
    FILE* c = nullptr; freopen_s(&c, "CONOUT$", "w", stdout);
    fopen_s(&g_log, logpath, "w");

    bool isGame = (GetModuleHandleW(L"CrimsonDesert.exe") != nullptr);
    HMODULE self = GetModuleHandleW(nullptr); // host process main module
    auto base = reinterpret_cast<uintptr_t>(self);

    logln("[cdsig] host exe   = %s (pid %lu)%s", exe, pid, isGame ? "  <-- GAME" : "");
    logln("[cdsig] module base = %p", (void*)base);
    logln("[cdsig] in-memory sections (X=executable, W=writable):");
    logln("        %-10s %-18s %14s  %s", "name", "start", "size", "flags");
    for (auto& s : cdsig::sections(self)) {
        bool x = (s.characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
        bool w = (s.characteristics & IMAGE_SCN_MEM_WRITE) != 0;
        logln("   %s%s   %-10s %p %14zu  0x%08X",
              x ? "X" : " ", w ? "W" : " ", s.name.c_str(), (void*)s.start, s.size, s.characteristics);
    }

    if (isGame && TEST_SIGNATURE && TEST_SIGNATURE[0]) {
        size_t n = cdsig::scanCount(TEST_SIGNATURE, self);
        logln("[cdsig] signature matches: %zu  (want exactly 1)", n);
        if (auto a = cdsig::scan(TEST_SIGNATURE, self))
            logln("[cdsig] first match = %p  (rva 0x%llX)",
                  (void*)*a, (unsigned long long)(*a - base));
    }

    logln("[cdsig] done.");
    if (g_log) { fclose(g_log); g_log = nullptr; }
    return 0;
}

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        CreateThread(nullptr, 0, Run, nullptr, 0, nullptr);
    }
    return TRUE;
}
