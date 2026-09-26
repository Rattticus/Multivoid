// coop/dev/midnight_drill.h -- drill: a midnight where the day rollover can be watched (ini
// midnight_drill=off|awake|asleep|cheat|mode5|malformed|joinwindow / env VOTVCOOP_MIDNIGHT_DRILL; BOTH
// peers, with rollover_watch on, on a fresh host world, since a save can carry an active event).
// Every phase waits on a state its peer can read, never on a clock; a refused step ends the arm INVALID.
// The host arms once a client's join is over (the slot world-ready, its bracket closed). awake sets the
// clock to 0.999 of the day; asleep sets 0.98 while awake (a jump can start an event, which refuses
// sleep), waits for no event and goes to bed, as the client does once joined; the host prints its runway
// at every set, and each peer clears its music flags before the midnight. cheat: the joined client writes
// a day onto its own clock three times, as the cheat menu does, and says how each next tick ended. mode5
// spawns game mode 5's master on each peer; the client says whether its own reset kept running.
// malformed: the host puts three clock samples with a NaN day. joinwindow: the host sets 0.9999 once it
// has taken a joiner's world, so the midnight falls inside the join, and prints its hash digest; the
// client prints its own once joined, and equal digests PASS. The evidence: rollover_watch's DAY, OUTPUTS
// and DIGEST lines, the run counts, the client's log.
#pragma once

namespace coop::net { class Session; }

namespace coop::dev::midnight_drill {

// True unless the row is `off`.
bool IsEnabled();

// Cache the session, for the role. Called from the subsystems Install fanout every pump tick;
// idempotent. No-op when off.
void Install(coop::net::Session* session);

// Advance this peer's phase. Every pump tick in a world. Game thread.
void Tick();

// The session ended: back to the first phase.
void OnDisconnect();

}  // namespace coop::dev::midnight_drill
