// coop/props/prop_park.cpp -- see coop/props/prop_park.h.

#include "coop/props/prop_park.h"

#include "coop/props/rider_hold.h"

#include "ue_wrap/core/cached_obj_ref.h"
#include "ue_wrap/core/hot_path_guard.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_attach.h"
#include "ue_wrap/engine/engine_physics.h"

#include <unordered_map>
#include <utility>
#include <vector>

namespace coop::prop_park {
namespace {

namespace E = ue_wrap::engine;

constexpr int kMaxRiders = 4;  // a Kerfus carries one

// A Character attached to a parked prop: the component its movement moves, the movement, and whether this park
// holds it still.
struct Rider {
    ue_wrap::CachedObjRef updated;
    ue_wrap::CachedObjRef movement;
    bool held = false;
};

// A parked prop, from its first park to its give-back: the prop, and the Characters attached to it when that park
// walked it. By the prop's pointer, held by slot and serial, so a record whose prop died is found stale. A park of
// a parked prop -- a re-latch each tick the game turns the body back on, a stream handing it to a hand -- tests
// only these, and walks nothing.
struct Record {
    ue_wrap::CachedObjRef actor;
    std::vector<Rider> riders;
};
std::unordered_map<void*, Record> g_parked;

void GiveBack(Record& rec) {
    for (Rider& r : rec.riders) {
        if (!r.held) continue;
        coop::rider_hold::Give(r.movement.Raw());
        r.held = false;
    }
}

// The live record of `actor`, or null. A stale one goes, its holds given back.
Record* Find(void* actor) {
    auto it = g_parked.find(actor);
    if (it == g_parked.end()) return nullptr;
    if (it->second.actor.Get() == actor) return &it->second;
    GiveBack(it->second);
    g_parked.erase(it);
    return nullptr;
}

// The first park's record: the Characters attached to `actor` now, each with the component its movement moves.
Record& Walk(void* actor) {
    Record& rec = g_parked[actor];
    rec.actor.Set(actor);
    E::AttachedCharacter found[kMaxRiders];
    const int n = E::AttachedCharactersOf(actor, found, kMaxRiders);
    for (int i = 0; i < n; ++i) {
        if (!found[i].updated) continue;
        Rider r;
        r.updated.Set(found[i].updated);
        r.movement.Set(found[i].movement);
        rec.riders.push_back(std::move(r));
    }
    return rec;
}

void RootSimulate(void* actor, void* mesh, bool on) {
    if (mesh) E::SetComponentSimulatePhysics(mesh, on);
    else if (actor) E::SetActorSimulatePhysics(actor, on);
}

}  // namespace

void Park(void* actor, void* mesh) {
    UE_ASSERT_GAME_THREAD("prop_park::Park");
    if (!actor) return;
    Record* known = Find(actor);
    Record& rec = known ? *known : Walk(actor);
    // The physics receiver's park, whole (docs/coop-sync-doctrine.md, step 4): stopping the body hands a welded
    // Character back to its own movement, a second author of the body's shape while the stream authors its pose.
    // Held, the Character rides where it rode when the park began, as the body's simulation held it.
    // Which Characters the body's simulation holds still now: a welded one, whose capsule answers with the body's
    // state. The body stops; each whose capsule stops with it is woken, and held.
    bool still[kMaxRiders] = {};
    const size_t n = rec.riders.size();  // at most kMaxRiders: the walk takes no more
    for (size_t i = 0; i < n; ++i) {
        void* updated = rec.riders[i].updated.Get();
        still[i] = !rec.riders[i].held && updated && E::IsComponentSimulatingPhysics(updated);
    }
    RootSimulate(actor, mesh, false);
    for (size_t i = 0; i < n; ++i) {
        Rider& r = rec.riders[i];
        void* updated = r.updated.Get();
        if (!still[i] || !updated || E::IsComponentSimulatingPhysics(updated)) continue;
        if (!coop::rider_hold::Take(r.movement.Get())) continue;  // stopped by its owner, not by a hold
        r.held = true;
        UE_LOGI("prop_park: parked %p -- a welded Character's movement is held while the body does not simulate",
                actor);
    }
}

void Unpark(void* actor, void* mesh, bool rootSimulates) {
    UE_ASSERT_GAME_THREAD("prop_park::Unpark");
    if (!actor) return;
    if (rootSimulates) RootSimulate(actor, mesh, true);
    if (Record* rec = Find(actor)) {
        GiveBack(*rec);
        g_parked.erase(actor);
    }
}

void OnDisconnect() {
    UE_ASSERT_GAME_THREAD("prop_park::OnDisconnect");
    for (auto& entry : g_parked) GiveBack(entry.second);
    g_parked.clear();
}

}  // namespace coop::prop_park
