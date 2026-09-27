// coop/dev/desk_crossing_drill.h -- drill: the main desk's detection needle crosses on the host alone (ini
// desk_crossing_drill=run|join / env VOTVCOOP_DESK_CROSSING_DRILL; BOTH peers).
//
// run: two legs, each on a caught signal the host forms and downloads: planeteater's, whose object renderer spawns
// the signal object actor fullyProcessed lives on, then looker_behind's, whose crossing saves. The host holds its
// step (its multiplier at 0) with the needle closer below 1 than the client's smallest step, lets three of its own
// play cycles pass, then lets its step run, and lets two more pass before the next leg. Each peer counts the loop
// resumes whose step crossed, the step and play cycles the game ran, and the autoSave bodies entered and run. Per leg
// the host says its step crossed once; the client that its own step never crossed, that it ran its desk's path after
// the step once, and on looker_behind that its autoSave body never ran while the host's saved the one row both peers
// hold; and at the crossing canDL holds on both, and on planeteater fullyProcessed. join: the host crosses on
// planeteater before the client's world is ready; the client relays no catch of its own from its world load's
// restore, reads the host's needle at 1, and finds canDL and fullyProcessed, its own step never crossing. Lines
// tagged [DESK-CROSSING-DRILL]; 'client DONE PASS|FAIL' ends it; ABANDONED is a step that could not run.

#pragma once

namespace coop::net { class Session; }

namespace coop::dev::desk_crossing_drill {

// Advance this peer's legs. Every pump tick in a world; a latched read when off. Game thread.
void Tick(coop::net::Session* session);

// The session ended: the legs start over, and a held host step is let go.
void OnDisconnect();

}  // namespace coop::dev::desk_crossing_drill
