// coop/interactables/dish_calib_sync.h -- the dishes' precision (dish.calibration), the calibration axis of the
// dish sync.
//
// Every peer diff-polls its dishes' precision once a second and broadcasts the changed ones' absolute values; the
// host applies a client's batch and relays it to the other clients, never back to its sender. A joiner's values
// come in the dish snapshot (coop/interactables/dish_sync), which primes the baseline here.

#pragma once

#include "coop/net/protocol.h"

#include <cstdint>

namespace coop::net { class Session; }

namespace coop::dish_calib_sync {

void Install(coop::net::Session* session);

// Game thread, once a second from dish_sync's slow tick: its dishes resolved, the session connected.
void Poll(coop::net::Session* session);

// CLIENT: the join snapshot's calibration column, rows [0, count), applied and taken as the baseline.
void ApplySnapshot(const coop::net::DishSnapshotPayload& p, int32_t count);

// Reliable applier (event_dispatch_signal).
void OnDishCalib(const coop::net::DishCalibPayload& p, uint8_t senderSlot);

// A session's end and a desk generation change: the baseline primes again at the next poll.
void Reset();

}  // namespace coop::dish_calib_sync
