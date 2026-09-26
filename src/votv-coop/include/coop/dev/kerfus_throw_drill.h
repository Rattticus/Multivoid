// coop/dev/kerfus_throw_drill.h -- drill: the host grabs the plain kerfur (the Kerfus) and lets it go or
// throws it, off and then on, then the client grabs it twice; each peer says whether its Kerfus stayed one body
// (ini kerfus_throw_drill=1 / env VOTVCOOP_KERFUS_THROW_DRILL; BOTH peers).
//
// Once a client's join is over the host runs six throws of the save's Kerfus: off, a release by the use key and
// two throws by the fire key; then on, the same three. Each sets the state by the Kerfus's own E-press verb, walks
// to it with the director, grabs it by the use key's chain, turns toward a nav-reachable pile, holds it and lets it
// go, then waits for it to rest. A seventh grab, let go by the use key, checks the last. The client samples its copy
// every 50 ms: at each host grab, how far the copy jumps to the host's hand; how far its centre of mass sits from
// the body's origin; how far the navigation pawn welded on it moved on the body. After the seventh release it says
// 'client DONE PASS|FAIL' and grabs the Kerfus twice itself, a use-key drop each, while the host samples its own
// Kerfus the same way through the two parks its receiver makes: then 'host DONE PASS|FAIL'. INVALID is a step that
// could not run.

#pragma once

namespace coop::net { class Session; }

namespace coop::dev::kerfus_throw_drill {

// Cache the session, for the role. Called every pump tick from the dev wiring; idempotent. No-op when off.
void Install(coop::net::Session* session);

// Advance this peer's phase. Every pump tick in a world. Game thread.
void Tick();

// The session ended: back to the first phase.
void OnDisconnect();

}  // namespace coop::dev::kerfus_throw_drill
