# Write your first Crimson Desert ASI plugin

An **ASI plugin** is just a Windows DLL (renamed `.asi`) that an *ASI loader* injects into the game at startup. It then runs your C++ inside the game process — where it can read live memory and hook engine functions with MinHook. This is the standard, community-accepted way to mod Crimson Desert (single-player).

This machine already has the toolchain: **Visual Studio Build Tools 2022/2026** (the C++ compiler) and Python/Git.

## 1. Get an ASI loader
The game has no loader by default (none was found in `bin64\`). The common choice is **Ultimate ASI Loader** (ThirstyScholar / ListLibrary builds), dropped into the game's `bin64\` as a proxy DLL the game already loads — e.g. `version.dll` or `dinput8.dll`. CDUMM's **ASI Plugins** tab can also install the loader and manage `.asi` files for you. Verify the proxy name your build loads before committing to one.

> Install location: `E:\SteamLibrary\steamapps\common\Crimson Desert\bin64\`

## 2. Build the probe
From **"x64 Native Tools Command Prompt for VS 2022"** (this sets up `cl.exe`):

```bat
cd path\to\crimson-desert-signatures\examples
cl /std:c++17 /EHsc /LD /O2 /I ..\include cdsig_probe.cpp /Fe:cdsig_probe.asi
```

`/LD` builds a DLL; `/Fe:` names it `.asi`. That's it — `cdsig_probe.asi` is ready.

## 3. Run it
1. Put the ASI loader + `cdsig_probe.asi` in `bin64\`.
2. Launch the game.
3. A console appears printing the module base and in-memory sections. That confirms your toolchain, loader, and `cdsig` all work end-to-end.

## 4. Add a hook (when you have a verified signature)
Pull MinHook (`vcpkg install minhook`, or add the amalgamated source), then:

```cpp
#include "../include/cdsig.hpp"
#include <MinHook.h>

using Fn = void(__fastcall*)(void* self);
static Fn oReal = nullptr;
static void __fastcall Detour(void* self) { /* your logic */ return oReal(self); }

void Install() {
    MH_Initialize();
    auto sig = "..."; // from signatures/signatures.json (verify scanCount==1 first)
    if (auto addr = cdsig::scan(sig)) {
        MH_CreateHook(reinterpret_cast<void*>(*addr), &Detour,
                      reinterpret_cast<void**>(&oReal));
        MH_EnableHook(MH_ALL_HOOKS);
    }
}
```

## Etiquette / safety
- Single-player only. There's no multiplayer or anti-cheat in Crimson Desert today — don't build things that would change that calculus or break the game's ToS.
- Resolve addresses with `cdsig::scan` (signatures), never hardcoded offsets, so your mod survives patches.
- Ship the **signature**, not extracted game code. Don't commit binaries/assets.
