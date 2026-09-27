// coop/dev/server_drill.h -- [dev] a server box's break is the host's, and a client's repair of it runs there.
//   HOST   -- breaks a box with no upgrades on its own verb (canBreak is certain there) and stamps its repair type
//             with a band value no roll makes (123): run -- once a client is in its world, then its DONE line when the
//             box ends fixed by the client's repair; join -- in a joiner's window, after its world was taken and before
//             it is ready, so only the connect snapshot carries it.
//   CLIENT -- run: finds the box broken with the band type (the row's mirror, type included); its own breakServer on
//             a healthy box is refused; it walks by the director to the box's bay, runs the repair widget's end(true)
//             with the box set, and its own fix must not run here but reach the host, whose row then shows the box
//             fixed. join: finds the box broken with the band type after its world is ready.
// "[SERVER-DRILL] FAIL" is the lane failing a step (--fail-marker), "[SERVER-DRILL] ABANDONED" the drill unable to do
// its part (--dead-marker). Run with server_drill=run|join and --done-marker "[SERVER-DRILL] client DONE".

#pragma once

namespace coop::net { class Session; }

namespace coop::dev::server_drill {

// Game thread, once per pump tick; two short string compares when off.
void Tick(coop::net::Session* session);

// The per-session half: a rejoin's peers run their legs again.
void OnDisconnect();

}  // namespace coop::dev::server_drill
