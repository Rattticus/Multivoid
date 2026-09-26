// coop/creatures/kerfus_brain.cpp -- see coop/creatures/kerfus_brain.h.

#include "coop/creatures/kerfus_brain.h"

#include "coop/net/session.h"
#include "coop/props/rider_hold.h"

#include "ue_wrap/actors/kerfus.h"
#include "ue_wrap/core/cached_obj_ref.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/script_gate.h"
#include "ue_wrap/engine/engine.h"

#include <atomic>
#include <cstddef>
#include <unordered_map>

namespace coop::kerfus_brain {
namespace {

namespace E  = ue_wrap::engine;
namespace UK = ue_wrap::kerfus;
namespace sg = ue_wrap::script_gate;


// The brain's bodies (p_kerfus.cpp, bp_cfg): the tick (energy drain and charge, the wheel's
// torque, the stuck jump), the two timer events that re-path and unstick it, the movement functions,
// the server job, the laptop's jump, the haunting, and the cord events, whose one effect is `charging`.
// The water events set only the body's damping, and upd() only its looks and sounds, so both stay.
constexpr const wchar_t* kBrain[] = {
    L"ReceiveTick", L"checkJump", L"updatePath", L"movePawnTo", L"findBrokenServer", L"task",
    L"jump", L"RCjump", L"possess", L"possessTimer", L"cordPlugged", L"cordUnplugged",
};
constexpr size_t kBrainCount = sizeof(kBrain) / sizeof(kBrain[0]);
constexpr int kTagBrainBase = 0x4B465300;  // 'KFS' + the row
constexpr int kTagServerFix = 0x4B4653FF;

std::atomic<coop::net::Session*> g_session{nullptr};
bool g_watched[kBrainCount] = {};
bool g_fixWatched = false;
bool g_saidLive = false;

// Per session: the refusals by row, and whether a row's first refusal was said.
unsigned long long g_refused[kBrainCount] = {};
bool g_said[kBrainCount] = {};
unsigned long long g_fixRefused = 0;

// The navigation pawns this client stilled, by Kerfus: the Kerfus and the pawn's movement, by slot and serial, and
// whether this lane holds that movement (rider_hold.h), false when its owner had stopped it already.
struct Stilled {
    ue_wrap::CachedObjRef kerfus;
    ue_wrap::CachedObjRef movement;
    bool held = false;
};
std::unordered_map<void*, Stilled> g_stilled;

bool OnClient() {
    auto* s = g_session.load(std::memory_order_acquire);
    return s && s->running() && s->role() == coop::net::Role::Client;
}

// The brain's navigation pawn (UK::NavPawn), welded onto the Kerfus's body, moves while the body does not simulate
// -- asleep, frozen, parked -- and every second the brain's checkJump, refused here, puts it back on the body. On
// a client it would fall without end, and the body's centre of mass with it. So a client seats it where the
// host's brain keeps it, on the body's origin, upright, and holds its movement still for the session: from the
// first refused tick after it spawned, one map lookup per tick after that. A scheduler kill
// (docs/coop-sync-doctrine.md, step 4) that removes a second author: the pawn's place is the host brain's, which
// holds it at the origin. Nothing restarts it while the hold is out -- the pawn's Blueprint has no graph, checkJump
// holds the Kerfus's one write to its movement, and a receiver's park takes a hold of its own beside this one --
// and the session's end gives it back.
void StillPawn(void* k) {
    auto it = g_stilled.find(k);
    if (it != g_stilled.end()) {
        if (it->second.kerfus.Get() == k && it->second.movement.Alive()) return;
        if (it->second.held) coop::rider_hold::Give(it->second.movement.Raw());  // a Kerfus or pawn that died
        g_stilled.erase(it);
    }
    void* pawn = UK::NavPawn(k);
    void* movement = pawn ? E::GetCharacterMovementComponent(pawn) : nullptr;
    ue_wrap::FVector at{};
    if (!movement || !E::TryGetActorLocation(k, at)) return;  // not spawned yet: the next tick asks again
    E::SetActorLocation(pawn, at);
    E::SetActorRotation(pawn, ue_wrap::FRotator{0.f, 0.f, 0.f});
    Stilled& st = g_stilled[k];
    st.kerfus.Set(k);
    st.movement.Set(movement);
    st.held = coop::rider_hold::Take(movement);
    UE_LOGI("kerfus_brain: stilled the Kerfus %p's navigation pawn on this client -- seated on the body, its "
            "movement %s", k, st.held ? "held" : "stopped already by its owner");
}

sg::Verdict OnBrainPre(const sg::Call& c) {
    if (!OnClient()) return sg::Verdict::Run;
    const size_t row = static_cast<size_t>(c.tag - kTagBrainBase);
    if (row == 0 && c.object) StillPawn(c.object);  // the tick
    if (row < kBrainCount) {
        ++g_refused[row];
        if (!g_said[row]) {
            g_said[row] = true;
            UE_LOGI("kerfus_brain: refused the Kerfus's %ls on this client -- the host runs its brain (said once "
                    "a session)", kBrain[row]);
        }
    }
    return sg::Verdict::Cancel;
}

// A server box's fix() called from a Kerfus's own frame. The server job's bodies are refused above,
// so this fires only for one that began before its watch went live; the fix is the host's to make.
sg::Verdict OnServerFixPre(const sg::Call& c) {
    if (!OnClient() || !c.callerObject) return sg::Verdict::Run;
    if (!UK::IsKerfus(c.callerObject)) return sg::Verdict::Run;
    ++g_fixRefused;
    UE_LOGW("kerfus_brain: refused a server fix() from a Kerfus's own body on this client (#%llu) -- a server "
            "job that began before the brain's watch went live", g_fixRefused);
    return sg::Verdict::Cancel;
}

void Register() {
    for (size_t i = 0; i < kBrainCount; ++i)
        if (!g_watched[i])
            g_watched[i] = sg::WatchClassName(UK::kClassName, kBrain[i], kTagBrainBase + static_cast<int>(i),
                                              &OnBrainPre, nullptr);
    if (!g_fixWatched)
        g_fixWatched = sg::WatchClassName(L"serverBox_C", L"fix", kTagServerFix, &OnServerFixPre, nullptr);
}

}  // namespace

void Install(coop::net::Session* session) {
    g_session.store(session, std::memory_order_release);
    Register();
}

void Tick() {
    if (g_saidLive) return;
    Register();
    sg::ResolvePendingNames();
    for (size_t i = 0; i < kBrainCount; ++i)
        if (!g_watched[i] || !sg::ClassNameWatchLive(UK::kClassName, kBrain[i], kTagBrainBase + static_cast<int>(i)))
            return;
    if (!g_fixWatched || !sg::ClassNameWatchLive(L"serverBox_C", L"fix", kTagServerFix)) return;
    g_saidLive = true;
    UE_LOGI("kerfus_brain: the Kerfus's %zu brain bodies and the server-fix guard are watched -- a client refuses "
            "them", kBrainCount);
}

void OnDisconnect() {
    unsigned long long total = g_fixRefused;
    for (size_t i = 0; i < kBrainCount; ++i) total += g_refused[i];
    if (total)
        UE_LOGI("kerfus_brain: session end -- %llu brain bodies refused on this client (tick %llu, server fixes %llu)",
                total, g_refused[0], g_fixRefused);
    for (size_t i = 0; i < kBrainCount; ++i) { g_refused[i] = 0; g_said[i] = false; }
    g_fixRefused = 0;
    // The holds go back: the brain runs here once the session is over.
    for (const auto& entry : g_stilled)
        if (entry.second.held) coop::rider_hold::Give(entry.second.movement.Raw());
    g_stilled.clear();
}

}  // namespace coop::kerfus_brain
