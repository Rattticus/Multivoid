// coop/interactables/dish_sync.h -- the satellite-dish sync lane.
//
// THE ROOT, measured: every dish slew runs a per-peer Blueprint frame loop with per-slew RNG
// in its start delays and speed; the download ARM rolls per-peer RNG polarity when it
// initialises the signal; and calibration has random writers on every peer. So dish poses, an
// armed download's polarity and the calibration all DIVERGE across peers on their own.
//
// So each axis gets ONE author. POSES: a client's dish simulation is PARKED -- its
// dish-moving ticker stopped, with a paired restore on the teardown fanout -- and the host streams
// movers-only rows plus a settle tail, which one applier drives kinematically through an
// interpolation window, so the stream's rate is not visible as stepping, for the stream rows and
// the join seed alike; a client's ping never succeeds on its own machine (desk_ping_sync). ARM:
// the host's raw poll is the only author, and a client applies the host's polarity rather than
// rolling its own. CALIBRATION: the host authors it too, in a lane of its own
// (coop/interactables/dish_calib_sync), polled from this lane's slow tick, with its own join seed.

#pragma once

#include "coop/net/protocol.h"

#include <cstdint>

namespace coop::net { class Session; }

namespace coop::dish_sync {

void Install(coop::net::Session* session);

// Game thread, per pump tick. HOST: the 4 Hz pose sweep (+ settle tail) + the
// 4 Hz arm poll. CLIENT: drain + apply DishPose batches and drive the LerpWindow
// mirror interp; the 1 Hz park latch (tickers + the cue reconciler). ALL peers:
// the 1 Hz calibration poll (dish_calib_sync).
void Tick();

// Reliable appliers (event_dispatch_state).
void OnDishArm(const coop::net::DishArmPayload& p, uint8_t senderSlot);
void OnDishSnapshot(const coop::net::DishSnapshotPayload& p, uint8_t senderSlot);

// HOST: the joiner's connect-replay rows -- DishSnapshot (poses and
// activeDishes) and, when the host machine is armed, a DishArm row (AFTER the
// desk rows + the kind=0 catch row on the same ordered lane).
void QueueConnectBroadcastForSlot(int peerSlot);

// Teardown fanout: the wire-residue sweep (clear OUR mirrored isMoving/
// activeDishes/cues on every shadow-true dish) THEN the ticker restore
// (disher = PE ReceiveBeginPlay), then module
// state reset.
void OnDisconnect();

}  // namespace coop::dish_sync
