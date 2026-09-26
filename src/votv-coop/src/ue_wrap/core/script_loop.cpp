// ue_wrap/core/script_loop.cpp -- see ue_wrap/core/script_loop.h.

#include "ue_wrap/core/script_loop.h"

#include "ue_wrap/core/sig_scan.h"

#include <windows.h>

#include <cstring>

namespace ue_wrap::script_loop {
namespace {

constexpr int kOpcodeLocalVirtual = 0x45;
constexpr int kOpcodeLocalFinal   = 0x46;
constexpr std::uint8_t kExReturn  = 0x04;
constexpr std::uint8_t kExNothing = 0x0B;
constexpr size_t kWindow = 0xA0;   // the loop and both handlers are under 0xA0 bytes
constexpr int kMaxCandidates = 256;

bool InModule(std::uintptr_t p, std::uintptr_t base, size_t size) { return p >= base && p < base + size; }

// A `lea r64,[rip+disp32]` at p (REX.W with or without REX.R; ModRM mod=00 rm=101) -> its target.
bool DecodeLeaRip(const std::uint8_t* p, std::uintptr_t& out) {
    if ((p[0] != 0x48 && p[0] != 0x4C) || p[1] != 0x8D || (p[2] & 0xC7) != 0x05) return false;
    std::int32_t rel;
    std::memcpy(&rel, p + 3, sizeof(rel));
    out = reinterpret_cast<std::uintptr_t>(p) + 7 + rel;
    return true;
}

// The loop's shape: within its body, the return-opcode compare, the nothing-opcode compare (the last
// thing it does, past the loop itself) and a rip-relative reference to the exec-handler table. The
// window covers the whole function; a short one once refused the real loop.
bool LooksLikeLoop(std::uintptr_t t, std::uintptr_t gnatives, std::uintptr_t base, size_t size) {
    if (!InModule(t, base, size) || !InModule(t + kWindow, base, size)) return false;
    const std::uint8_t* p = reinterpret_cast<const std::uint8_t*>(t);
    bool cmpReturn = false, cmpNothing = false, namesTable = false;
    for (size_t i = 0; i + 7 <= kWindow; ++i) {
        if (p[i] == 0x80 && p[i + 1] == 0x38 && p[i + 2] == kExReturn) cmpReturn = true;
        if (p[i] == 0x80 && p[i + 1] == 0x38 && p[i + 2] == kExNothing) cmpNothing = true;
        std::uintptr_t target;
        if (DecodeLeaRip(p + i, target) && target == gnatives) namesTable = true;
    }
    return cmpReturn && cmpNothing && namesTable;
}

// The body executor a handler hands ProcessScriptFunction: the one rip-relative lea in the handler
// whose target has the loop's shape. 0 when there is none or more than one.
std::uintptr_t ExecutorOfHandler(std::uintptr_t handler, std::uintptr_t gnatives, std::uintptr_t base,
                                 size_t size) {
    if (!InModule(handler, base, size) || !InModule(handler + kWindow, base, size)) return 0;
    const std::uint8_t* p = reinterpret_cast<const std::uint8_t*>(handler);
    std::uintptr_t found = 0;
    for (size_t i = 0; i + 7 <= kWindow; ++i) {
        std::uintptr_t target;
        if (!DecodeLeaRip(p + i, target)) continue;
        if (!LooksLikeLoop(target, gnatives, base, size)) continue;
        if (found && found != target) return 0;
        found = target;
    }
    return found;
}

}  // namespace

std::uintptr_t* ResolveGNatives() {
    // The dispatch site `lea r9,[GNatives]; ... movzx; ... call [r9+rax*8]`: the rel32 sits at hit+3,
    // so the table is hit + 7 + rel32.
    const std::uintptr_t hit = ue_wrap::FindPattern(
        "4C 8D 0D ?? ?? ?? ?? 49 8B D7 0F B6 08 48 FF C0 49 89 47 20 8B C1 49 8B 4F 18 41 FF 14 C1");
    if (!hit) return nullptr;
    const std::int32_t rel = *reinterpret_cast<std::int32_t*>(hit + 3);
    return reinterpret_cast<std::uintptr_t*>(hit + 7 + rel);
}

bool TableFilled(const std::uintptr_t* gnatives) {
    if (!gnatives) return false;
    std::uintptr_t base = 0;
    size_t size = 0;
    ue_wrap::MainModuleRange(base, size);
    int inRange = 0;
    for (int i = 0; i < 256; ++i)
        if (InModule(gnatives[i], base, size)) ++inRange;
    return inRange >= 200;
}

std::uintptr_t ByCode(std::uintptr_t gnatives, int& candidates) {
    candidates = 0;
    std::uintptr_t base = 0, text = 0;
    size_t size = 0, textSize = 0;
    ue_wrap::MainModuleRange(base, size);
    if (!gnatives || !ue_wrap::MainTextRange(text, textSize) || textSize < 7) return 0;
    // Every rip-relative lea of the table, by its opcode byte; the distinct functions that hold one.
    std::uintptr_t seen[kMaxCandidates];
    int n = 0;
    const auto* p = reinterpret_cast<const std::uint8_t*>(text);
    for (size_t i = 1; i + 6 < textSize; ++i) {
        const void* op = std::memchr(p + i, 0x8D, textSize - 6 - i);
        if (!op) break;
        i = static_cast<size_t>(static_cast<const std::uint8_t*>(op) - p);
        std::uintptr_t target;
        if (!DecodeLeaRip(p + i - 1, target) || target != gnatives) continue;
        const std::uintptr_t fn = ue_wrap::FunctionStart(text + i - 1);
        if (!fn) continue;
        bool dup = false;
        for (int k = 0; k < n && !dup; ++k) dup = seen[k] == fn;
        if (!dup && n < kMaxCandidates) seen[n++] = fn;
    }
    candidates = n;
    std::uintptr_t found = 0;
    for (int k = 0; k < n; ++k) {
        if (!LooksLikeLoop(seen[k], gnatives, base, size)) continue;
        if (found) return 0;   // two with the loop's shape: no answer
        found = seen[k];
    }
    return found;
}

std::uintptr_t ByHandlers(const std::uintptr_t* gnatives, std::uintptr_t& virt, std::uintptr_t& fin) {
    virt = fin = 0;
    if (!TableFilled(gnatives)) return 0;
    std::uintptr_t base = 0;
    size_t size = 0;
    ue_wrap::MainModuleRange(base, size);
    const auto table = reinterpret_cast<std::uintptr_t>(gnatives);
    virt = ExecutorOfHandler(gnatives[kOpcodeLocalVirtual], table, base, size);
    fin = ExecutorOfHandler(gnatives[kOpcodeLocalFinal], table, base, size);
    return (virt && virt == fin) ? virt : 0;
}

}  // namespace ue_wrap::script_loop
