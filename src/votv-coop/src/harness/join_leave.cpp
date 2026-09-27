// harness/join_leave.cpp -- see harness/join_leave.h.

#include "harness/join_leave.h"

#include "coop/net/session.h"
#include "coop/session/join_progress.h"
#include "coop/session/shutdown.h"
#include "ue_wrap/core/game_thread.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/engine/engine_save.h"     // ReturnToMainMenu: the game mode's transition to the menu
#include "ue_wrap/engine/world_identity.h"
#include "ue_wrap/world/world_singleton.h"  // Gamemode: the travel's receiver

#include <windows.h>

#include <atomic>
#include <memory>

namespace harness::join_leave {
namespace {

namespace GT = ue_wrap::game_thread;
namespace WI = ue_wrap::world_identity;

constexpr ULONGLONG kAskEveryMs = 250;
constexpr ULONGLONG kDispatchBoundMs = 30 * 1000;
constexpr ULONGLONG kMenuBoundMs = 60 * 1000;  // of time the game thread spends running tasks, not of a load

// One ask, shared with the task posted for it. Whichever side moves it off Pending first decides: the task
// claims it before it travels, and a leave that ends meanwhile withdraws it, so a task that runs late never
// travels for a join that is gone.
enum Ask : int { kPending, kClaimed, kDispatched, kRefused, kNoGamemode, kWithdrawn };

bool g_active = false;
coop::net::Config g_cfg;
ULONGLONG g_beganMs = 0;
ULONGLONG g_dispatchedMs = 0;  // 0 until the travel dispatched
ULONGLONG g_nextAskMs = 0;
ULONGLONG g_lastStepMs = 0;              // the menu bound's clock: the time between two steps counts
ULONGLONG g_liveMs = 0;                  // only when the game thread ran a task in it
unsigned long long g_lastTasks = 0;
std::shared_ptr<std::atomic<int>> g_ask;  // the ask in flight, or null
unsigned g_noGamemode = 0;
bool g_degradedSaid = false;

// A pending ask is withdrawn; a claimed one is travelling on the game thread now and is left to finish.
void End() {
    if (g_ask) {
        int pending = kPending;
        g_ask->compare_exchange_strong(pending, kWithdrawn, std::memory_order_acq_rel);
        g_ask.reset();
    }
    g_active = false;
    g_cfg = coop::net::Config{};
}

// The travel runs with the detour live, as the player's own quit to the menu does: no session runs yet, so no lane's
// interceptor has anything to do in the teardown.
void PostAsk() {
    auto ask = std::make_shared<std::atomic<int>>(kPending);
    g_ask = ask;
    GT::Post([ask] {
        int pending = kPending;
        if (!ask->compare_exchange_strong(pending, kClaimed, std::memory_order_acq_rel)) return;  // withdrawn
        if (coop::shutdown::IsShuttingDown()) {
            ask->store(kWithdrawn, std::memory_order_release);
            return;
        }
        if (!ue_wrap::world_singleton::Gamemode()) {
            ask->store(kNoGamemode, std::memory_order_release);
            return;
        }
        ask->store(ue_wrap::engine::ReturnToMainMenu() ? kDispatched : kRefused, std::memory_order_release);
    });
}

}  // namespace

bool Begin(const coop::net::Config& cfg) {
    if (WI::Degraded()) {
        if (!g_degradedSaid) {
            g_degradedSaid = true;
            UE_LOGW("harness: the world reader is degraded, so a client join cannot tell whether it starts inside a "
                    "world -- it goes on as from the menu");
        }
        return false;
    }
    if (WI::CurrentWorldKind() != WI::WorldKind::Gameplay) return false;
    g_active = true;
    g_cfg = cfg;
    g_beganMs = ::GetTickCount64();
    g_dispatchedMs = 0;
    g_nextAskMs = g_beganMs;
    g_ask.reset();
    g_noGamemode = 0;
    UE_LOGI("harness: a client join from inside a world -- leaving it for the menu first");
    return true;
}

Outcome Step(coop::net::Config& out) {
    if (!g_active) return Outcome::Idle;
    const ULONGLONG now = ::GetTickCount64();
    if (coop::shutdown::IsShuttingDown() || !coop::join_progress::Active()) {
        const bool left = g_dispatchedMs != 0;
        const bool asking = !left && g_ask && g_ask->load(std::memory_order_acquire) == kClaimed;
        UE_LOGI("harness: the join ended during the leave (%s) -- no session starts",
                left ? "the travel to the menu had dispatched"
                     : asking ? "an ask of the travel was running, which may still travel" : "the world was not left");
        End();
        return left ? Outcome::CancelledLeft : Outcome::Cancelled;
    }
    if (!g_dispatchedMs && WI::CurrentWorldKind() == WI::WorldKind::Other) {
        UE_LOGI("harness: the world was left before the travel dispatched -- the join goes on from the menu");
        out = g_cfg;
        End();
        return Outcome::AtMenu;
    }
    if (!g_dispatchedMs) {
        if (g_ask) {
            const int a = g_ask->load(std::memory_order_acquire);
            if (a == kClaimed) return Outcome::Waiting;  // on the game thread now, travelling or not
            if (a != kPending) {
                g_ask.reset();
                if (a == kDispatched) {
                    g_dispatchedMs = now;
                    g_lastStepMs = now;
                    g_liveMs = 0;
                    g_lastTasks = GT::TasksRun();
                    UE_LOGI("harness: the travel to the menu dispatched %llu ms into the leave (%u asks found no "
                            "game mode first)", static_cast<unsigned long long>(now - g_beganMs), g_noGamemode);
                    return Outcome::Waiting;
                }
                if (a == kNoGamemode && g_noGamemode++ == 0)
                    UE_LOGI("harness: this world's game mode is not in the object index yet -- the travel is "
                            "asked again every 250 ms");
                if (a == kRefused) {  // the game mode stands and refused: asking again changes nothing
                    UE_LOGE("harness: the game mode refused the travel to the menu (its reason above) -- the join "
                            "from this world cannot start");
                    End();
                    return Outcome::Failed;
                }
                g_nextAskMs = now + kAskEveryMs;
            }
        }
        // Bounded whether or not the game thread answers: End withdraws an ask it has not taken up.
        if (now - g_beganMs > kDispatchBoundMs) {
            UE_LOGE("harness: the travel to the menu did not dispatch within 30 s (%u asks found no game mode) -- "
                    "the join from this world cannot start", g_noGamemode);
            End();
            return Outcome::Failed;
        }
        if (!g_ask && now >= g_nextAskMs) PostAsk();
        return Outcome::Waiting;
    }
    if (WI::CurrentWorldKind() == WI::WorldKind::Other) {
        UE_LOGI("harness: at the menu %llu ms after the travel dispatched -- the join goes on from there",
                static_cast<unsigned long long>(now - g_dispatchedMs));
        out = g_cfg;
        End();
        return Outcome::AtMenu;
    }
    const unsigned long long tasks = GT::TasksRun();
    if (tasks != g_lastTasks) g_liveMs += now - g_lastStepMs;
    g_lastTasks = tasks;
    g_lastStepMs = now;
    if (g_liveMs > kMenuBoundMs) {
        UE_LOGE("harness: the menu did not stand after 60 s of the game thread running since the travel -- the join "
                "cannot start");
        End();
        return Outcome::Failed;
    }
    return Outcome::Waiting;
}

bool Active() { return g_active; }

}  // namespace harness::join_leave
