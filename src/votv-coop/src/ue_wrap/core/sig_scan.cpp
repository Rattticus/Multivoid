#include "ue_wrap/core/sig_scan.h"

#include <windows.h>

#include <cstring>
#include <vector>

namespace ue_wrap {
namespace {

// A parsed pattern byte: either a concrete value or a wildcard.
struct PatByte {
    uint8_t value;
    bool wild;
};

std::vector<PatByte> ParsePattern(const char* pattern) {
    std::vector<PatByte> out;
    for (const char* p = pattern; *p;) {
        if (*p == ' ') {
            ++p;
            continue;
        }
        if (*p == '?') {
            out.push_back({0, true});
            ++p;
            if (*p == '?') ++p;  // accept "??" or "?"
            continue;
        }
        // Parse one hex byte (two nibbles).
        auto nibble = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        const int hi = nibble(p[0]);
        const int lo = (hi >= 0) ? nibble(p[1]) : -1;
        if (hi < 0 || lo < 0) break;  // malformed; stop
        out.push_back({static_cast<uint8_t>((hi << 4) | lo), false});
        p += 2;
    }
    return out;
}

}  // namespace

void MainModuleRange(uintptr_t& base, size_t& size) {
    HMODULE h = ::GetModuleHandleW(nullptr);
    base = reinterpret_cast<uintptr_t>(h);
    size = 0;
    if (!h) return;
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(h);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;
    size = nt->OptionalHeader.SizeOfImage;
}

bool MainTextRange(uintptr_t& begin, size_t& size) {
    begin = 0;
    size = 0;
    uintptr_t base = 0;
    size_t imageSize = 0;
    MainModuleRange(base, imageSize);
    if (!imageSize) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        if (std::memcmp(sec->Name, ".text", 6) == 0) {
            begin = base + sec->VirtualAddress;
            size = sec->Misc.VirtualSize;
            return true;
        }
    }
    return false;
}

uintptr_t FunctionStart(uintptr_t pc) {
    DWORD64 imageBase = 0;
    const RUNTIME_FUNCTION* rf = ::RtlLookupFunctionEntry(pc, &imageBase, nullptr);
    for (int depth = 0; rf && depth < 32; ++depth) {
        const auto* info = reinterpret_cast<const uint8_t*>(imageBase + rf->UnwindInfoAddress);
        if (!((info[0] >> 3) & UNW_FLAG_CHAININFO)) return static_cast<uintptr_t>(imageBase + rf->BeginAddress);
        // UNWIND_INFO: version and flags, prolog size, code count, frame register; then the codes,
        // padded to an even count; then the chained entry.
        const size_t codes = (static_cast<size_t>(info[2]) + 1) & ~static_cast<size_t>(1);
        rf = reinterpret_cast<const RUNTIME_FUNCTION*>(info + 4 + codes * 2);
    }
    return 0;
}

// How UE4SS pays for its scan: one pass for every signature, split across eight threads
// (SinglePassSigScanner.cpp). Ours scans for a handful, on the loader's call where the game's start
// waits, so it makes each pass cheap instead: the anchor is the pattern's longest run of concrete
// bytes, not its first byte, which is usually a REX prefix (0x48 alone is 7% of the exe's code).
uintptr_t FindPatternIn(uintptr_t base, size_t size, const char* pattern) {
    const std::vector<PatByte> pat = ParsePattern(pattern);
    const size_t n = pat.size();
    if (n == 0 || size < n) return 0;
    size_t at = 0, len = 0;
    for (size_t i = 0, j = 0; i < n; i = j + 1) {
        for (j = i; j < n && !pat[j].wild; ++j) {
        }
        if (j - i > len) {
            at = i;
            len = j - i;
        }
    }
    if (len == 0) return base;  // all wildcards: the first position matches
    // Horspool on the run: the byte under the run's last position says how far the run can move
    // without passing an occurrence of itself, so no match of the whole pattern is passed either.
    size_t shift[256];
    for (size_t& s : shift) s = len;
    for (size_t k = 0; k + 1 < len; ++k) shift[pat[at + k].value] = len - 1 - k;
    const auto* bytes = reinterpret_cast<const uint8_t*>(base);
    const size_t last = size - n;
    const size_t tail = at + len - 1;
    for (size_t i = 0; i <= last; i += shift[bytes[i + tail]]) {
        if (bytes[i + tail] != pat[tail].value) continue;
        size_t j = 0;
        while (j < n && (pat[j].wild || bytes[i + j] == pat[j].value)) ++j;
        if (j == n) return base + i;
    }
    return 0;
}

uintptr_t FindPattern(const char* pattern) {
    uintptr_t base = 0;
    size_t size = 0;
    MainModuleRange(base, size);
    if (!base || !size) return 0;
    return FindPatternIn(base, size, pattern);
}

}  // namespace ue_wrap
