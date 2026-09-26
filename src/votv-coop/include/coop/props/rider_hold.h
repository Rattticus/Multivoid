// coop/props/rider_hold.h -- the movement of a Character welded onto a prop, held still by this peer's lanes.
// Two lanes hold one: a receiver's park (prop_park.h), while the prop's body does not simulate, and a client's
// stilling of the Kerfus's navigation pawn (coop/creatures/kerfus_brain.h), for the session. Both would flip the
// same switch, the movement's tick, and whichever went second would record a stop it did not make and could undo
// the other's: a park given back re-enabled a pawn the client meant to keep still. So the switch has one owner
// here, and each lane takes a hold and gives it back: the tick stops at the first hold and runs again at the last
// give-back, in any order. A movement that did not tick when first held is not held at all: whoever stopped it
// keeps it, as a remote player's puppet keeps its movement off. Game thread, every function.

#pragma once

namespace coop::rider_hold {

// Hold `movement` still. False, holding nothing, when it did not tick and no hold here had stopped it.
bool Take(void* movement);

// Give back one hold; the last one lets it tick again. A movement that died is forgotten. Null-safe.
void Give(void* movement);

// The session ended: every movement still held ticks again, and the holds are forgotten.
void OnDisconnect();

}  // namespace coop::rider_hold
