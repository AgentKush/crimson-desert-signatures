// cdsig.hpp — header-only runtime signature scanner for Crimson Desert (and any Win64 module).
//
// Why runtime scanning: CrimsonDesert.exe ships packed/anti-tampered (real code lives in
// oversized, renamed, runtime-decrypted sections like .xcode / .sbss). You CANNOT pattern-scan
// the on-disk file. By the time an injected ASI plugin runs, the code is decrypted in memory,
// so we scan the LIVE module's executable sections instead. That's what this header does.
//
// Dependencies: Windows only. No Psapi, no extra libs. C++17.
// Usage:
//     #include "cdsig.hpp"
//     auto addr = cdsig::scan("48 8B ?? ?? ?? ?? ?? E8");   // ?? = wildcard
//     size_t n = cdsig::scanCount(sig);                      // verify uniqueness (want 1)
//
#pragma once
#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <optional>

namespace cdsig {

struct Pattern { std::vector<uint8_t> bytes; std::vector<bool> mask; };

// Parse an IDA-style signature: "48 8B ?? E8 ?? ?? ?? ??". '?' or '??' = wildcard byte.
inline Pattern parse(const std::string& sig) {
    Pattern p;
    for (size_t i = 0; i < sig.size();) {
        char c = sig[i];
        if (c == ' ' || c == '\t') { ++i; continue; }
        if (c == '?') {
            p.bytes.push_back(0); p.mask.push_back(false);
            ++i; if (i < sig.size() && sig[i] == '?') ++i;
        } else {
            p.bytes.push_back(static_cast<uint8_t>(std::stoul(sig.substr(i, 2), nullptr, 16)));
            p.mask.push_back(true);
            i += 2;
        }
    }
    return p;
}

struct Section { std::string name; uintptr_t start; size_t size; uint32_t characteristics; };

// Enumerate the LIVE in-memory sections of a module by parsing its PE headers at the load base.
inline std::vector<Section> sections(HMODULE mod = nullptr) {
    std::vector<Section> out;
    if (!mod) mod = GetModuleHandleW(nullptr);
    if (!mod) return out;
    auto base = reinterpret_cast<uint8_t*>(mod);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return out;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return out;
    auto sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        char nm[9] = {0}; memcpy(nm, sec[i].Name, 8);
        out.push_back({ std::string(nm),
                        reinterpret_cast<uintptr_t>(base + sec[i].VirtualAddress),
                        sec[i].Misc.VirtualSize,
                        sec[i].Characteristics });
    }
    return out;
}

inline std::optional<uintptr_t> scanIn(const Pattern& p, const uint8_t* start, size_t size) {
    if (p.bytes.empty() || size < p.bytes.size()) return std::nullopt;
    const size_t n = p.bytes.size();
    for (size_t i = 0; i + n <= size; ++i) {
        bool ok = true;
        for (size_t j = 0; j < n; ++j)
            if (p.mask[j] && start[i + j] != p.bytes[j]) { ok = false; break; }
        if (ok) return reinterpret_cast<uintptr_t>(start + i);
    }
    return std::nullopt;
}

// First match across all EXECUTABLE sections of the module (covers decrypted .xcode/.sbss/etc.).
inline std::optional<uintptr_t> scan(const std::string& sig, HMODULE mod = nullptr) {
    Pattern p = parse(sig);
    for (auto& s : sections(mod)) {
        if (!(s.characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        if (auto a = scanIn(p, reinterpret_cast<const uint8_t*>(s.start), s.size)) return a;
    }
    return std::nullopt;
}

// Count matches — use this to confirm a signature is UNIQUE (you want exactly 1) before trusting it.
inline size_t scanCount(const std::string& sig, HMODULE mod = nullptr) {
    Pattern p = parse(sig);
    size_t count = 0;
    for (auto& s : sections(mod)) {
        if (!(s.characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        auto st = reinterpret_cast<const uint8_t*>(s.start);
        size_t off = 0, sz = s.size;
        while (true) {
            auto hit = scanIn(p, st + off, sz - off);
            if (!hit) break;
            ++count;
            off = (*hit - reinterpret_cast<uintptr_t>(st)) + 1;
            if (off + p.bytes.size() > sz) break;
        }
    }
    return count;
}

// Follow a 32-bit RIP-relative displacement (e.g. operand of a call/lea) to an absolute address.
// dispField = address of the int32 displacement; nextInstr = address of the following instruction.
inline uintptr_t ripRel(uintptr_t dispField, uintptr_t nextInstr) {
    return nextInstr + *reinterpret_cast<int32_t*>(dispField);
}

} // namespace cdsig
