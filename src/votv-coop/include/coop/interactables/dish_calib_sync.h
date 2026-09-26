// coop/interactables/dish_calib_sync.h -- the dishes' precision (dish.calibration): the host authors it, a client's
// copy holds the host's values, and a client player's own verbs reach the host as intents.
//
// Its writers are nine Blueprint bodies (ue_wrap/desk/dish.h names them): some run on every peer (a mirrored
// lightning strike's hit, a slewing dish's own loss), some on the machine that starts them (the virus event, the
// cheat menu), two at a player's own verb. So the host's poll sends its changed dishes once a second, as floats (the
// toolgun's tool writes any value), and all of them when its baseline primes; a joiner gets all of them at its world-
// ready. A client sends no batch and the host refuses one. A client's copy holds the values that came from the host
// for the session, and whatever moved it off one is put back: at the client's poll, and before mainGamemode.setPrec
// averages the dishes into the rate the client's desk computes its download with. MTA puts a remote ped's synced
// health back every pulse whatever the local game did to it (CClientPed::LockHealth, reapplied in StreamedInPulse).
// The two verbs a client player performs (ue_wrap/desk/dish_writers) run on its copy, and at their exit the dishes
// they changed go to the host as an intent; the host performs what it can and sends every named dish's value to all,
// as the server box's upgrade lane confirms each op (coop/interactables/server_upgrade_sync).

#pragma once

#include "coop/net/protocol.h"

#include <cstdint>

namespace coop::net { class Session; }

namespace coop::dish_calib_sync {

// Keeps the session; registers the three watches once a process. Called every pump tick.
void Install(coop::net::Session* session);

// Game thread, every pump tick: settles the watches once, with one line each, live or dead.
void Tick();

// Game thread, once a second from dish_sync's slow tick: its dishes resolved, the session connected.
void Poll(coop::net::Session* session);

// HOST: every dish's precision to the joiner at `peerSlot`, its seed.
void QueueConnectBroadcastForSlot(int peerSlot);

// Reliable appliers (event_dispatch_signal): a client holds the host's batch; the host performs a client's intent.
void OnDishCalib(const coop::net::DishCalibPayload& p, uint8_t senderSlot);
void OnDishCalibIntent(const coop::net::DishCalibPayload& p, uint8_t senderSlot);

// A new desk (a level reload): the host's baseline primes again and sends every dish. A client's holds are the
// host's values by dish index and stay; its next world-ready brings a new seed.
void OnDeskReplaced();

// The session's end: nothing held, nothing sent.
void OnSessionEnd();

// What the lane did this session, for a drill. The last intent the host performed names its dish and value.
struct Counts {
    uint64_t putBack = 0;          // CLIENT: dishes put back to the host's value
    uint64_t intentsSent = 0;      // CLIENT: dishes its player's verbs sent to the host
    uint64_t intentsApplied = 0;   // HOST: dishes a client's intent set
    uint64_t intentsRefused = 0;   // HOST: named dishes it did not set, sent back at its own value
    int32_t  lastIndex = -1;       // HOST: the last dish an intent set
    float    lastValue = 0.f;
};
Counts LaneCounts();

}  // namespace coop::dish_calib_sync
