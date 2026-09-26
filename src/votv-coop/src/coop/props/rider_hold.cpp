// coop/props/rider_hold.cpp -- see coop/props/rider_hold.h.

#include "coop/props/rider_hold.h"

#include "ue_wrap/core/cached_obj_ref.h"
#include "ue_wrap/core/hot_path_guard.h"
#include "ue_wrap/engine/engine.h"

#include <unordered_map>

namespace coop::rider_hold {
namespace {

namespace E = ue_wrap::engine;

// A movement held still, by its pointer: held by slot and serial, so an entry whose Character died is found
// stale; and how many holds are out on it. A Kerfus carries one: a handful at most.
struct Held {
    ue_wrap::CachedObjRef movement;
    int holds = 0;
};
std::unordered_map<void*, Held> g_held;

// The live entry of `movement`, or null; a stale one goes.
Held* Find(void* movement) {
    auto it = g_held.find(movement);
    if (it == g_held.end()) return nullptr;
    if (it->second.movement.Get() == movement) return &it->second;
    g_held.erase(it);
    return nullptr;
}

}  // namespace

bool Take(void* movement) {
    UE_ASSERT_GAME_THREAD("rider_hold::Take");
    if (!movement) return false;
    if (Held* h = Find(movement)) {
        ++h->holds;
        return true;
    }
    if (!E::IsComponentTickEnabled(movement)) return false;
    E::SetComponentTickEnabled(movement, false);
    Held& h = g_held[movement];
    h.movement.Set(movement);
    h.holds = 1;
    return true;
}

void Give(void* movement) {
    UE_ASSERT_GAME_THREAD("rider_hold::Give");
    Held* h = movement ? Find(movement) : nullptr;
    if (!h || --h->holds > 0) return;
    E::SetComponentTickEnabled(movement, true);
    g_held.erase(movement);
}

void OnDisconnect() {
    UE_ASSERT_GAME_THREAD("rider_hold::OnDisconnect");
    for (const auto& entry : g_held)
        if (void* movement = entry.second.movement.Get()) E::SetComponentTickEnabled(movement, true);
    g_held.clear();
}

}  // namespace coop::rider_hold
