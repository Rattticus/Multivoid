// coop/dev/world_first_check.h -- [dev] client_world_first's verdict: whether the join's world is a new one.
//
// The arm's client boots a world of its own, then joins from inside it. The join is right only if its world-ready
// comes in the host's world, loaded after this one was left; a world-ready in the booted world is a join that never
// loaded the host's. Armed once the booted world stands, judged once, at this client's world-ready. Game thread.

#pragma once

namespace coop::dev::world_first_check {

// The booted world stands: it is held as an identity, the world a join that never left it still stands in.
void Arm();

// Per session tick: once this client has announced world-ready, one line, "-- FAIL" (the rig's fail marker) while the
// booted world is still the current one, PASS when another world stands. A single bool read when not armed.
void Tick();

}  // namespace coop::dev::world_first_check
