#include "ue_wrap/core/hook.h"

#include "ue_wrap/core/log.h"

#include <MinHook.h>
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>

namespace ue_wrap::hook {
namespace {

// The facade's armed state, not whether MinHook is initialised: Shutdown clears this and
// deliberately leaves MinHook initialised, because uninitialising frees trampolines the
// process is still calling through (see hook.h). One flag, not two: a second could disagree
// with this one, and then neither is authority. The ordering is load-bearing: Shutdown
// clears it before lifting patches, so Enable's post-enable re-read cannot miss a teardown
// that started mid-call.
std::atomic<bool> g_live{false};

// The retirement latch, one-way, never cleared. The live flag answers two questions that
// diverge after a teardown: whether MinHook initialised and whether a new patch may arm.
// Shutdown never uninitialises MinHook, so Init would answer the first with yes, and an
// install after the teardown would resurrect the facade and arm a fresh patch nothing would
// ever lift, since the shutdown latch never runs a second time; the live path is the capture
// hook thread, un-joined, which calls Install from outside the shutdown ordering. Two flags
// that cannot disagree harmfully: this is a monotonic latch, not a mirror of the live flag.
std::atomic<bool> g_retired{false};

const char* StatusName(MH_STATUS s) { return MH_StatusToString(s); }

// The shared entries as they must stay, for VerifyEntries: our jump, then the bytes the entry held
// before our patch read it, up to where our trampoline returns into the function and a little past.
// Written by Install before the count that publishes the slot.
constexpr int kEntryBytes = 16;
constexpr int kMaxEntries = 8;
constexpr int kJumpBytes = 5;
struct EntryShot {
    void*   target = nullptr;
    uint8_t expect[kEntryBytes] = {};
    int     checked = kJumpBytes;   // how many leading bytes must match `expect`
};
EntryShot g_entries[kMaxEntries];
std::atomic<int> g_entryCount{0};

// Where the trampoline jumps back into `target`: MinHook ends the copied instructions with an absolute
// jump (FF 25 00000000, then the address). 0 when there is none, the stolen bytes being a jump of
// another engine's that never returns.
int ReturnOffset(const void* trampoline, const void* target) {
    const auto* tr = static_cast<const uint8_t*>(trampoline);
    const auto t = reinterpret_cast<uintptr_t>(target);
    for (int off = 0; off + 14 <= 64; ++off) {
        if (tr[off] != 0xFF || tr[off + 1] != 0x25 || tr[off + 2] || tr[off + 3] || tr[off + 4] || tr[off + 5])
            continue;
        uint64_t to = 0;
        std::memcpy(&to, tr + off + 6, sizeof(to));
        if (to > t && to < t + kEntryBytes) return static_cast<int>(to - t);
    }
    return 0;
}

// The first changed byte of an entry against its record, or -1 when it holds.
int FirstChange(const EntryShot& e) {
    const auto* now = static_cast<const uint8_t*>(e.target);
    for (int i = 0; i < e.checked; ++i)
        if (now[i] != e.expect[i]) return i;
    return -1;
}

void LogChange(const EntryShot& e, int at, const char* when) {
    const auto* now = static_cast<const uint8_t*>(e.target);
    UE_LOGE("hook: the entry at %p changed at +%d (%s) -- %s (expected %02x %02x %02x %02x %02x | %02x %02x %02x %02x "
            "%02x, holds %02x %02x %02x %02x %02x | %02x %02x %02x %02x %02x)", e.target, at, when,
            at < kJumpBytes ? "another jump covers ours, and our detour is bypassed"
                            : "foreign bytes lie under our jump, and our trampoline returns into them",
            e.expect[0], e.expect[1], e.expect[2], e.expect[3], e.expect[4], e.expect[5], e.expect[6], e.expect[7],
            e.expect[8], e.expect[9], now[0], now[1], now[2], now[3], now[4], now[5], now[6], now[7], now[8], now[9]);
}

// The process loader lock, held around every MinHook enable and disable. One that changes a hook
// freezes the other threads, first enumerating them through Toolhelp, which maps a section per
// step; holding the lock keeps every freeze out of every DLL's DllMain. The embedded browser's
// chrome_elf.dll patches ntdll!NtMapViewOfSection inside its DllMain before it stores the pointer
// the patch forwards through, so a mapping in between calls a null pointer (docs/architecture.md).
// UE4SS walks threads through Toolhelp inside its own DllMain, under this lock, on every boot of
// this process, so the walk is safe under it; a walk without Toolhelp would avoid the mapping too,
// but means patching the vendored MinHook, and the lock also covers the freeze's other calls.
using LdrLockFn   = LONG(NTAPI*)(ULONG flags, ULONG* disposition, ULONG_PTR* cookie);
using LdrUnlockFn = LONG(NTAPI*)(ULONG flags, ULONG_PTR cookie);
struct LoaderLockApi {
    LdrLockFn   lock   = nullptr;
    LdrUnlockFn unlock = nullptr;
};
const LoaderLockApi& Api() {
    static const LoaderLockApi api = [] {
        LoaderLockApi a{};
        if (HMODULE nt = ::GetModuleHandleW(L"ntdll.dll")) {
            a.lock   = reinterpret_cast<LdrLockFn>(::GetProcAddress(nt, "LdrLockLoaderLock"));
            a.unlock = reinterpret_cast<LdrUnlockFn>(::GetProcAddress(nt, "LdrUnlockLoaderLock"));
        }
        if (!a.lock || !a.unlock) {
            a = LoaderLockApi{};   // both or neither: a lock without its unlock would never release
            UE_LOGE("hook: ntdll exports no loader-lock pair -- enables and disables run unlocked");
        }
        return a;
    }();
    return api;
}
class LoaderLockScope {
public:
    LoaderLockScope() {
        const LoaderLockApi& a = Api();
        held_ = a.lock && a.lock(0, nullptr, &cookie_) >= 0;
    }
    ~LoaderLockScope() {
        if (held_) Api().unlock(0, cookie_);
    }
    LoaderLockScope(const LoaderLockScope&) = delete;
    LoaderLockScope& operator=(const LoaderLockScope&) = delete;

private:
    ULONG_PTR cookie_ = 0;
    bool      held_   = false;
};
MH_STATUS EnableLocked(void* target) {
    const LoaderLockScope lock;
    return MH_EnableHook(target);
}
MH_STATUS DisableLocked(void* target) {
    const LoaderLockScope lock;
    return MH_DisableHook(target);
}

// The follow-jmp-immune relay rewrite. On x64 MinHook always routes a patched target through
// a relay (an indirect jump through an absolute pointer) inside the 64-byte trampoline slot.
// A co-resident inline-hook engine that follows jmp chains (UE4SS ships one) hooking the
// same function after us takes our target's jump into this relay, sees the indirect branch,
// resolves its destination to the operand address (the relay's pointer slot) and writes its
// own patch there, clobbering the detour pointer; our relay then jumps through a garbage
// pointer to a non-canonical address, a general-protection fault surfaced as an access
// violation at -1 (proven from a full crash dump). The fix rewrites the relay's leading
// instruction to a non-branching form (mov rax, imm64; jmp rax): the follower stops on the
// mov, hooks the relay itself in place, and both detours chain. Absolute-jump semantics are
// identical; only the byte encoding the follower keys on changes. Safe because it runs
// before the enable: the target is still unpatched, so nothing executes the relay yet.
// Fail-closed: if the expected relay signature is not in the slot, leave it untouched
// (MinHook's layout changed; surface it rather than guess).
bool MakeRelayFollowJmpImmune(void* trampoline, void* detour) {
    if (!trampoline || !detour) return false;
    auto* base = static_cast<uint8_t*>(trampoline);
    const uint64_t want = reinterpret_cast<uint64_t>(detour);
    uint8_t* relay = nullptr;
    // The relay lives inside the 64-byte trampoline slot; scan for the classic MinHook relay
    // signature whose absolute target is our detour.
    for (int off = 0; off + 14 <= 64; ++off) {
        if (base[off] == 0xFF && base[off + 1] == 0x25 && base[off + 2] == 0x00 &&
            base[off + 3] == 0x00 && base[off + 4] == 0x00 && base[off + 5] == 0x00) {
            uint64_t p = 0;
            std::memcpy(&p, base + off + 6, sizeof(p));
            if (p == want) { relay = base + off; break; }
        }
    }
    if (!relay) {
        UE_LOGE("hook: immune-relay: FF25 relay not found in trampoline slot -- "
                "MinHook layout changed? leaving relay as-is (fail-closed)");
        return false;
    }
    DWORD oldProt = 0;
    if (!VirtualProtect(relay, 14, PAGE_EXECUTE_READWRITE, &oldProt)) {
        UE_LOGE("hook: immune-relay: VirtualProtect(RWX) failed on relay %p", relay);
        return false;
    }
    uint8_t buf[14];
    buf[0] = 0x48; buf[1] = 0xB8;                 // mov rax, imm64
    std::memcpy(buf + 2, &want, sizeof(want));    //   = &detour
    buf[10] = 0xFF; buf[11] = 0xE0;               // jmp rax
    buf[12] = 0x90; buf[13] = 0x90;               // pad to the 14-byte relay footprint
    std::memcpy(relay, buf, sizeof(buf));
    DWORD tmp = 0;
    VirtualProtect(relay, 14, oldProt, &tmp);
    FlushInstructionCache(GetCurrentProcess(), relay, 14);
    UE_LOGI("hook: immune-relay: relay @%p rewritten to MOV RAX,&detour/JMP RAX "
            "(followJmp-immune)", relay);
    return true;
}

}  // namespace

bool Init() {
    // Retirement outranks MinHook's own opinion: the initialise call reports already-initialised
    // forever, and that answer once undid a completed Shutdown.
    if (g_retired) return false;
    if (g_live) return true;
    const MH_STATUS s = MH_Initialize();
    // Already-initialised is success here, not an error: Shutdown clears the live flag without
    // uninitialising MinHook, so the two states legitimately disagree after a teardown, and
    // MinHook is the one telling the truth about its own heap.
    if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) {
        UE_LOGE("hook: MH_Initialize failed (%s)", StatusName(s));
        return false;
    }
    g_live = true;
    UE_LOGI("hook: MinHook initialized%s",
            s == MH_ERROR_ALREADY_INITIALIZED ? " (already; re-arming the facade)" : "");
    return true;
}

bool Install(void* target, void* detour, void** trampoline, bool followJmpImmune) {
    if (!g_live && !Init()) return false;
    if (!target || !detour || !trampoline) {
        UE_LOGE("hook: Install called with null target/detour/trampoline");
        return false;
    }
    // The entry as it stood before our patch read it: what our trampoline expects to return into.
    uint8_t before[kEntryBytes];
    std::memcpy(before, target, kEntryBytes);
    MH_STATUS s = MH_CreateHook(target, detour, trampoline);
    if (s != MH_OK) {
        UE_LOGE("hook: MH_CreateHook(%p) failed (%s)", target, StatusName(s));
        return false;
    }
    // Make the relay follow-jmp-immune while the target is still unpatched (before the enable,
    // so thread-safe). `*trampoline` is the slot base; the relay lives inside it. Best effort: a
    // failure is logged and non-fatal (the classic relay still works absent a co-resident
    // jmp-following hook engine).
    if (followJmpImmune) {
        MakeRelayFollowJmpImmune(*trampoline, detour);
    }
    s = EnableLocked(target);
    if (s != MH_OK) {
        UE_LOGE("hook: MH_EnableHook(%p) failed (%s)", target, StatusName(s));
        // The one legitimate hook removal in this process, and the gate
        // (.github/ci/minhook_free_gate.ps1) allowlists exactly this line. Removing frees the
        // trampoline, a use-after-free anywhere the hook is live, but the enable just failed, so
        // the target was never patched and no thread can be inside the trampoline or holding a
        // pointer into it. Leaving a created-but-disabled hook behind would leak the slot instead.
        MH_RemoveHook(target);
        return false;
    }
    // Compare after act, the same contract Enable documents below, and the reason this exists:
    // the entry guard is check-then-act, Install is reachable from threads that never
    // synchronise with the game thread, and an arm that lands after Shutdown's blanket disable
    // would survive with no second teardown to lift it. Teardown wins in every interleaving.
    if (g_retired) {
        DisableLocked(target);
        UE_LOGW("hook: install of %p raced Shutdown -- lifted again (teardown wins)", target);
        return false;
    }
    if (followJmpImmune) {
        const int n = g_entryCount.load(std::memory_order_relaxed);
        if (n < kMaxEntries) {
            EntryShot& e = g_entries[n];
            e.target = target;
            std::memcpy(e.expect, target, kJumpBytes);  // our jump
            std::memcpy(e.expect + kJumpBytes, before + kJumpBytes, kEntryBytes - kJumpBytes);
            const int back = ReturnOffset(*trampoline, target);
            e.checked = back ? (back + 8 < kEntryBytes ? back + 8 : kEntryBytes) : kJumpBytes;
            // Compare after act: a patch that landed between our read and our write is caught here.
            const int at = FirstChange(e);
            if (at >= 0) LogChange(e, at, "at install");
            g_entryCount.store(n + 1, std::memory_order_release);
        }
    }
    UE_LOGI("hook: installed on %p (trampoline %p)", target, *trampoline);
    return true;
}

bool VerifyEntries(const char* when) {
    const int n = g_entryCount.load(std::memory_order_acquire);
    int changed = 0;
    for (int i = 0; i < n; ++i) {
        const int at = FirstChange(g_entries[i]);
        if (at < 0) continue;
        ++changed;
        LogChange(g_entries[i], at, when);
    }
    if (changed == 0)
        UE_LOGI("hook: %d shared entr%s hold our patch as we left it (%s)", n, n == 1 ? "y" : "ies", when);
    return changed == 0;
}

bool Disable(void* target) {
    if (!g_live || !target) return false;
    const MH_STATUS s = DisableLocked(target);
    if (s != MH_OK) {
        UE_LOGW("hook: MH_DisableHook(%p) (%s)", target, StatusName(s));
        return false;
    }
    UE_LOGI("hook: disabled %p (trampoline slot retained on purpose)", target);
    return true;
}

bool Enable(void* target) {
    if (!g_live || !target) return false;
    const MH_STATUS s = EnableLocked(target);
    if (s != MH_OK) {
        UE_LOGW("hook: MH_EnableHook(%p) re-arm (%s)", target, StatusName(s));
        return false;
    }
    // Compare after act. The guard above is check-then-act on its own: this is reachable from
    // the render thread (the capture re-arm) while the game thread is inside Shutdown, so a
    // teardown can begin between the guard and the enable and we would re-arm a patch Shutdown
    // had just lifted. Shutdown sets the latch before its blanket disable, so re-reading it here
    // catches every interleaving: either we see the latch and lift our own patch, or Shutdown's
    // blanket runs after our enable and lifts it; both orders end disabled, as they do in
    // Install. The latch re-read settles the race without a mutex of the facade's own; only the
    // MinHook call holds the loader lock.
    if (g_retired) {
        DisableLocked(target);
        UE_LOGW("hook: re-arm of %p raced Shutdown -- lifted again (teardown wins)", target);
        return false;
    }
    UE_LOGI("hook: re-enabled %p", target);
    return true;
}

void Shutdown() {
    // The latch goes first and unconditionally: before the early return, so a Shutdown that
    // arrives before anything was installed still retires the facade, and before the blanket
    // disable, so a concurrent arm re-reads it.
    g_retired = true;
    if (!g_live.exchange(false)) return;   // one-way; also the double-Shutdown guard
    // Latching retirement first is the whole ordering contract (see Enable and Install): an arm
    // that slips past its own entry guard re-reads the latch after MinHook returns and undoes
    // itself. Lift every patch, free nothing: removing a hook or uninitialising both free
    // trampoline slots, and MinHook writes a free-list pointer over a slot's first bytes as it
    // does so, over the stolen prologue a thread may be about to return through; measured, this
    // runs seconds before process detach, so a dying process does not close that window, and
    // the OS reclaims the slots at exit. Shutdown runs from the game thread's window procedure
    // (coop/session/shutdown), and its blanket disable freezes the other threads under the loader
    // lock like every enable and disable; nothing in a freeze waits on another thread, since
    // MinHook allocates before it suspends and frees after it resumes.
    DisableLocked(MH_ALL_HOOKS);
    UE_LOGI("hook: all patches lifted (trampolines retained -- MinHook stays initialized)");
}

}  // namespace ue_wrap::hook
