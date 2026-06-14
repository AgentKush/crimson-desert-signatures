// cdsig_probe.cpp — a minimal diagnostic ASI plugin: the instrument for capturing signatures
// from the LIVE Crimson Desert process.
//
// What it does on injection:
//   1. Opens a console AND writes a log file to %TEMP%\cdsig_probe.log (readable after the run).
//   2. Prints the CrimsonDesert.exe load base and its IN-MEMORY section table
//      (confirm .xcode/.sbss are decrypted/executable at runtime).
//   3. Optionally tests a byte signature: prints how many times it matches (want exactly 1)
//      and the resolved address.
//
// It does NOT modify the game. It reads the game's own already-running memory, the same way
// every camera/QoL ASI mod does. Single-player use only.
//
// BUILD (Visual Studio Build Tools are installed on this machine):
//   Open "x64 Native Tools Command Prompt for VS", then from this folder:
//     cl /std:c++17 /EHsc /LD /O2 cdsig_probe.cpp /Fe:cdsig_probe.asi
//   (.asi is just a renamed .dll that an ASI loader auto-loads from bin64\)
//
#include "../include/cdsig.hpp"
#include <cstdio>
#include <cstdarg>

// Put a candidate signature here to test it live. Leave empty to just dump sections.
static const char* TEST_SIGNATURE = "";

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
    AllocConsole();
    FILE* c = nullptr; freopen_s(&c, "CONOUT$", "w", stdout);

    char tmp[MAX_PATH] = {0}; GetTempPathA(MAX_PATH, tmp);
    char logpath[MAX_PATH] = {0}; snprintf(logpath, sizeof(logpath), "%scdsig_probe.log", tmp);
    fopen_s(&g_log, logpath, "w");

    HMODULE game = GetModuleHandleW(L"CrimsonDesert.exe");
    if (!game) game = GetModuleHandleW(nullptr);
    auto base = reinterpret_cast<uintptr_t>(game);

    logln("[cdsig] log file: %s", logpath);
    logln("[cdsig] module base = %p", (void*)base);
    logln("[cdsig] in-memory sections (X=executable, W=writable):");
    logln("        %-10s %-18s %14s  %s", "name", "start", "size", "flags");
    for (auto& s : cdsig::sections(game)) {
        bool x = (s.characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
        bool w = (s.characteristics & IMAGE_SCN_MEM_WRITE) != 0;
        logln("   %s%s   %-10s %p %14zu  0x%08X",
              x ? "X" : " ", w ? "W" : " ", s.name.c_str(), (void*)s.start, s.size, s.characteristics);
    }

    if (TEST_SIGNATURE && TEST_SIGNATURE[0]) {
        size_t n = cdsig::scanCount(TEST_SIGNATURE, game);
        logln("[cdsig] signature matches: %zu  (want exactly 1)", n);
        if (auto a = cdsig::scan(TEST_SIGNATURE, game))
            logln("[cdsig] first match = %p  (rva 0x%llX)",
                  (void*)*a, (unsigned long long)(*a - base));
    } else {
        logln("[cdsig] set TEST_SIGNATURE to probe a pattern.");
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
