// coop/interactables/dish_calib_sync.cpp -- see coop/interactables/dish_calib_sync.h.

#include "coop/interactables/dish_calib_sync.h"

#include "coop/net/session.h"

#include "ue_wrap/desk/dish.h"

#include <atomic>

namespace coop::dish_calib_sync {
namespace {

namespace D = ue_wrap::dish;

std::atomic<coop::net::Session*> g_session{nullptr};

// Shared: the calibration diff-poll baseline (all peers; primed by snapshot and wire
// applies).
float g_prevCalib[coop::net::kMaxDishes] = {};
bool g_haveCalibBaseline = false;

}  // namespace

void Install(coop::net::Session* session) {
    g_session.store(session, std::memory_order_release);
}

// All peers: the symmetric calibration diff poll.
void Poll(coop::net::Session* s) {
    D::DishRow rows[coop::net::kMaxDishes];
    const int32_t n = D::ReadAllRows(rows, coop::net::kMaxDishes);
    if (n <= 0) return;
    if (!g_haveCalibBaseline) {
        for (int32_t i = 0; i < n; ++i)
            if (rows[i].index >= 0 && rows[i].index < coop::net::kMaxDishes)
                g_prevCalib[rows[i].index] = rows[i].calibration;
        g_haveCalibBaseline = true;
        return;
    }
    coop::net::DishCalibPayload p{};
    for (int32_t i = 0; i < n; ++i) {
        const auto& r = rows[i];
        if (r.index < 0 || r.index >= coop::net::kMaxDishes) continue;
        if (r.calibration != g_prevCalib[r.index] && p.count < coop::net::kMaxDishes) {
            auto& e = p.entries[p.count++];
            e.index = static_cast<uint8_t>(r.index);
            e.valueQ = coop::net::QuantCalib(r.calibration);
            g_prevCalib[r.index] = r.calibration;
        }
    }
    if (p.count > 0) {
        s->SendReliable(coop::net::ReliableKind::DishCalib, &p, sizeof(p));
    }
}

void ApplySnapshot(const coop::net::DishSnapshotPayload& p, int32_t count) {
    for (int32_t i = 0; i < count; ++i) {
        const float calib = coop::net::DequantCalib(p.rows[i].calibQ);
        D::WriteCalibration(i, calib);
        g_prevCalib[i] = calib;  // prime -- a wire apply must never re-diff
    }
    g_haveCalibBaseline = true;
}

void OnDishCalib(const coop::net::DishCalibPayload& p, uint8_t senderSlot) {
    auto* s = g_session.load(std::memory_order_acquire);
    if (!s) return;
    if (!D::EnsureResolved()) return;
    const int32_t n = p.count <= coop::net::kMaxDishes ? p.count : coop::net::kMaxDishes;
    for (int32_t i = 0; i < n; ++i) {
        const auto& e = p.entries[i];
        if (e.index >= coop::net::kMaxDishes) continue;
        const float v = coop::net::DequantCalib(e.valueQ);
        D::WriteCalibration(e.index, v);
        g_prevCalib[e.index] = v;  // apply + prime, GT-atomic (echo-proof)
    }
    if (s->role() == coop::net::Role::Host) {
        // Relay in arrival order, the lane's total order.
        for (int slot = 1; slot < static_cast<int>(coop::net::kMaxPeers); ++slot) {
            if (slot == senderSlot || !s->IsSlotReady(slot)) continue;
            s->SendReliableToSlot(slot, coop::net::ReliableKind::DishCalib, &p, sizeof(p),
                                  senderSlot > 0 ? senderSlot : 0);
        }
    }
}

void Reset() {
    g_haveCalibBaseline = false;
}

}  // namespace coop::dish_calib_sync
