// coop/dev/world_roll_drill.h -- drill: the day's world rolls, each a host roll that must cross and a client's
// own that must be refused (ini world_roll_drill=off, or eye_, jelly_ or redsky_ with host, client or join / env
// VOTVCOOP_WORLD_ROLL_DRILL; BOTH peers, a fresh host world). Every phase waits on a state its peer can read.
// _host: once a client's join is over (the slot world-ready, its bracket closed) the host runs the roll's verb,
// and the client's copy must follow within 20 s; _client: the joined client runs its own verb, as its roll would,
// and the gate must refuse it, its copy unchanged; _join: the host runs the verb before any client's world is
// ready, and the joiner's copy must show it. The eye is the sky's noon setEye(true). The jellyfish is the path's
// 18:00 spawn, whose seven fish must come as mirrors and leave with the host's run. The red sky is the gamemode's
// noon toggle: redsky_host starts one and ends it at once, and the client's copy must see both edges, counted at
// the event's own set, so two in one tick are both seen; redsky_join's joiner then runs its own toggle, as its
// noon would with a red sky live, which the gate must refuse, its sky still red. A refused step ends the arm
// INVALID. The evidence: each arm's DONE line on the client, each peer's fish average every 5 s and each red sky set.
#pragma once

namespace coop::net { class Session; }

namespace coop::dev::world_roll_drill {

// True unless the row is `off`.
bool IsEnabled();

// Cache the session, for the role, and, once, for the red sky arms, watch redSkyEvent_C.set at the gate. Called
// from the dev lanes' Install fanout every pump tick; idempotent. No-op when off.
void Install(coop::net::Session* session);

// Advance this peer's arm. Every pump tick in a world. Game thread.
void Tick();

// The session ended: back to the first phase.
void OnDisconnect();

}  // namespace coop::dev::world_roll_drill
