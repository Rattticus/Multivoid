// coop/dev/drive_drill_verbs.cpp -- see drive_drill_verbs.h.

#include "drive_drill_verbs.h"  // co-located private header (src tree, not include/)

#include "ue_wrap/actors/inventory.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_nav.h"  // FindNavPath
#include "ue_wrap/world/world_singleton.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace coop::dev::drive_drill_verbs {
namespace {

namespace E = ue_wrap::engine;
namespace R = ue_wrap::reflection;

float Flat(const ue_wrap::FVector& a, const ue_wrap::FVector& b) { return std::hypot(a.X - b.X, a.Y - b.Y); }

void* PlayerFn(void* player, const wchar_t* name) {
    return player ? R::FindDispatchFunctionCached(R::ClassOf(player), name) : nullptr;
}

}  // namespace

std::vector<ue_wrap::FVector> ReachableRing(void* player, void* target, int points, float ringCm, float reachCm) {
    ue_wrap::FVector from{}, at{};
    if (!E::TryGetActorLocation(player, from) || !E::TryGetActorLocation(target, at)) return {};
    std::vector<std::pair<float, ue_wrap::FVector>> found;
    for (int i = 0; i < points; ++i) {
        const float a = 6.2831853f * static_cast<float>(i) / static_cast<float>(points);
        ue_wrap::FVector p = from;  // at the height the player stands at
        p.X = at.X + ringCm * std::cos(a);
        p.Y = at.Y + ringCm * std::sin(a);
        std::vector<ue_wrap::FVector> route;
        if (!E::FindNavPath(player, from, p, route) || route.empty() || Flat(route.back(), p) > reachCm) continue;
        float len = 0.f;
        for (size_t k = 1; k < route.size(); ++k) len += Flat(route[k - 1], route[k]);
        found.emplace_back(len, p);
    }
    std::stable_sort(found.begin(), found.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
    std::vector<ue_wrap::FVector> out;
    for (const auto& f : found) out.push_back(f.second);
    return out;
}

bool Pocket(void* player, void* prop) {
    void* fn = PlayerFn(player, L"putObjectInventory2");
    ue_wrap::ParamFrame f(fn);
    if (!fn || !f.valid() || !f.Set(L"InputPin", prop) || !f.Set(L"noNotify", true) || !ue_wrap::Call(player, f))
        return false;
    return f.Get<bool>(L"return");
}

void* TakeOut(const std::wstring& cls, const std::wstring& key) {
    ue_wrap::inventory::LivePersonalStore store;
    if (!ue_wrap::inventory::ReadLivePersonalStore(store)) return nullptr;
    int32_t index = -1;
    for (size_t i = 0; i < store.records.size(); ++i)
        if (store.records[i].className == cls && store.records[i].key == key) index = static_cast<int32_t>(i);
    void* gm = ue_wrap::world_singleton::Gamemode();
    const int32_t off = gm ? R::FindPropertyOffset(R::ClassOf(gm), L"playerContainer") : -1;
    void* container = off >= 0 ? *reinterpret_cast<void* const*>(static_cast<const uint8_t*>(gm) + off) : nullptr;
    if (index < 0 || !container || !R::IsLive(container)) return nullptr;
    void* fn = R::FindDispatchFunctionCached(R::ClassOf(container), L"getObject");
    ue_wrap::ParamFrame f(fn);
    const ue_wrap::FVector none{};
    if (!fn || !f.valid() || !f.Set<int32_t>(L"index", index) || !f.Set(L"customLoc", false) ||
        !f.SetRaw(L"loc", &none, sizeof(none)) || !ue_wrap::Call(container, f))
        return nullptr;
    void* out = f.Get<void*>(L"OutputPin");
    return out && R::IsLive(out) ? out : nullptr;
}

bool Hold(void* player, void* prop) {
    void* fn = PlayerFn(player, L"Hold Object");
    ue_wrap::ParamFrame f(fn);
    const uint32_t useHold = 0;  // take `manual`, not the grab slot
    if (!fn || !f.valid() || !f.Set(L"useHold", useHold) || !f.Set(L"manual", prop) || !ue_wrap::Call(player, f))
        return false;
    return f.Get<uint32_t>(L"collected") != 0;
}

bool Throw(void* player) {
    void* fn = PlayerFn(player, L"throwHoldingProp");
    ue_wrap::ParamFrame f(fn);
    return fn && f.valid() && ue_wrap::Call(player, f);
}

}  // namespace coop::dev::drive_drill_verbs
